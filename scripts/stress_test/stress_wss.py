import ssl
import json
import time
import asyncio
import argparse
import os
from dataclasses import dataclass, field
from datetime import datetime
from config import WSS_URI, TOPICS, CAMERA_ID, REPORT_DIR


@dataclass
class ClientStats:
    client_id: int
    topics: list
    frames_received: dict = field(default_factory=dict)
    frame_sizes: dict = field(default_factory=dict)
    start_time: float = 0.0
    end_time: float = 0.0
    total_frames: int = 0
    total_bytes: int = 0
    errors: list = field(default_factory=list)
    connect_time: float = 0.0
    first_frame_time: float = 0.0


def parse_binary_frame(data):
    if len(data) < 4:
        return None, data
    header_len = int.from_bytes(data[:4], "little")
    if header_len == 0 or header_len > len(data) - 4:
        return None, data
    try:
        header = json.loads(data[4 : 4 + header_len].decode("utf-8"))
    except Exception:
        header = {"type": "unknown_binary"}
    payload = data[4 + header_len :]
    return header, payload


async def wss_client(client_id, uri, topics, duration, delay_ms=0):
    import websockets

    stats = ClientStats(client_id=client_id, topics=topics)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE

    if delay_ms > 0:
        await asyncio.sleep(delay_ms / 1000.0)

    stats.start_time = time.time()
    try:
        async with websockets.connect(
            uri,
            ssl=ctx,
            max_size=16 * 1024 * 1024,
            ping_interval=20,
            ping_timeout=60,
            close_timeout=5,
        ) as ws:
            stats.connect_time = time.time() - stats.start_time
            sub_msg = json.dumps({"action": "subscribe", "topics": topics})
            await ws.send(sub_msg)

            deadline = time.time() + duration
            while time.time() < deadline:
                try:
                    msg = await asyncio.wait_for(ws.recv(), timeout=5.0)
                    now = time.time()
                    if stats.first_frame_time == 0.0:
                        stats.first_frame_time = now

                    stats.total_frames += 1
                    if isinstance(msg, bytes):
                        stats.total_bytes += len(msg)
                        header, _ = parse_binary_frame(msg)
                        dtype = "unknown_binary"
                        if header:
                            dtype = header.get("type", header.get("data_type", "unknown_binary"))
                        stats.frames_received[dtype] = stats.frames_received.get(dtype, 0) + 1
                        sizes = stats.frame_sizes.setdefault(dtype, [])
                        sizes.append(len(msg))
                    elif isinstance(msg, str):
                        stats.total_bytes += len(msg)
                        try:
                            data = json.loads(msg)
                            dtype = data.get("type", "unknown_json")
                        except Exception:
                            dtype = "raw_json"
                        stats.frames_received[dtype] = stats.frames_received.get(dtype, 0) + 1
                        sizes = stats.frame_sizes.setdefault(dtype, [])
                        sizes.append(len(msg))

                except asyncio.TimeoutError:
                    pass
                except websockets.exceptions.ConnectionClosed as e:
                    stats.errors.append(f"ConnectionClosed: code={e.code} reason={e.reason}")
                    break

            try:
                unsub_msg = json.dumps({"action": "unsubscribe", "topics": topics})
                await ws.send(unsub_msg)
            except Exception:
                pass

    except Exception as e:
        stats.errors.append(f"ConnectError: {e}")

    stats.end_time = time.time()
    return stats


def print_report(test_name, all_stats, duration, topics, num_clients):
    ts = datetime.now().strftime("%Y%m%d-%H%M%S-%f")[:21]
    print("\n" + "=" * 80)
    print(f"STRESS TEST REPORT - {test_name}")
    print("=" * 80)
    print(f"Timestamp     : {ts}")
    print(f"Duration      : {duration}s")
    print(f"Clients       : {num_clients}")
    print(f"Topics        : {topics}")
    print("-" * 80)

    total_frames = 0
    total_bytes = 0
    total_errors = 0
    per_type_totals = {}

    for s in all_stats:
        elapsed = s.end_time - s.start_time if s.end_time > s.start_time else 1
        fps = s.total_frames / elapsed
        mb = s.total_bytes / (1024 * 1024)
        print(
            f"  Client {s.client_id:>3d}: {s.total_frames:>6d} frames, "
            f"{fps:>6.1f} FPS, {mb:>7.2f} MB, "
            f"connect={s.connect_time*1000:.0f}ms, "
            f"errors={len(s.errors)}"
        )
        if s.errors:
            for e in s.errors[:5]:
                print(f"           ! {e}")
        total_frames += s.total_frames
        total_bytes += s.total_bytes
        total_errors += len(s.errors)
        for dtype, cnt in s.frames_received.items():
            per_type_totals[dtype] = per_type_totals.get(dtype, 0) + cnt

    print("-" * 80)
    overall_elapsed = max(s.end_time for s in all_stats) - min(s.start_time for s in all_stats)
    overall_elapsed = overall_elapsed if overall_elapsed > 0 else 1
    print(f"Total frames : {total_frames}")
    print(f"Total data   : {total_bytes / (1024*1024):.2f} MB")
    print(f"Overall FPS  : {total_frames / overall_elapsed:.1f} (aggregate)")
    print(f"Total errors : {total_errors}")
    print(f"\nPer-type breakdown:")
    for dtype, cnt in sorted(per_type_totals.items()):
        print(f"  {dtype:<25s}: {cnt:>6d} frames")

    result = "PASS" if total_errors == 0 and total_frames > 0 else "FAIL"
    print(f"\nResult: {result}")
    print("=" * 80)

    os.makedirs(REPORT_DIR, exist_ok=True)
    report_file = os.path.join(REPORT_DIR, f"{test_name.replace(' ', '_')}_{ts}.json")
    report = {
        "test_name": test_name,
        "timestamp": ts,
        "duration": duration,
        "clients": num_clients,
        "topics": topics,
        "total_frames": total_frames,
        "total_bytes": total_bytes,
        "total_errors": total_errors,
        "per_type": per_type_totals,
        "result": result,
        "clients_detail": [
            {
                "id": s.client_id,
                "frames": s.total_frames,
                "bytes": s.total_bytes,
                "errors": [e for e in s.errors],
                "frames_per_type": s.frames_received,
            }
            for s in all_stats
        ],
    }
    with open(report_file, "w") as f:
        json.dump(report, f, indent=2)
    print(f"Report saved: {report_file}")
    return result


async def test_sustained_throughput(num_clients, duration, topics, uri):
    print(f"\n>>> TEST: Sustained Throughput - {num_clients} clients, {duration}s")
    tasks = [wss_client(i, uri, topics, duration) for i in range(num_clients)]
    results = await asyncio.gather(*tasks)
    return print_report("Sustained_Throughput", results, duration, topics, num_clients)


async def test_multi_client_load(num_clients_list, duration, topics, uri):
    all_results = {}
    for n in num_clients_list:
        print(f"\n>>> TEST: Multi-Client Load - {n} clients, {duration}s")
        tasks = [wss_client(i, uri, topics, duration) for i in range(n)]
        results = await asyncio.gather(*tasks)
        r = print_report(f"Multi_Client_Load_{n}", results, duration, topics, n)
        all_results[n] = r
    return all_results


async def test_rapid_connect_disconnect(cycles, topics, uri):
    import websockets

    print(f"\n>>> TEST: Rapid Connect/Disconnect - {cycles} cycles")
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    errors = []
    for i in range(cycles):
        try:
            async with websockets.connect(uri, ssl=ctx, max_size=16 * 1024 * 1024) as ws:
                await ws.send(json.dumps({"action": "subscribe", "topics": topics}))
                msg = await asyncio.wait_for(ws.recv(), timeout=3.0)
                await ws.send(json.dumps({"action": "unsubscribe", "topics": topics}))
        except Exception as e:
            errors.append(f"Cycle {i}: {e}")
    print(f"  Cycles: {cycles}, Errors: {len(errors)}")
    if errors:
        for e in errors[:10]:
            print(f"  ! {e}")
    result = "PASS" if len(errors) == 0 else "FAIL"
    print(f"  Result: {result}")
    return result


async def test_type_switching(duration, uri, camera_id):
    import websockets

    print(f"\n>>> TEST: Type Switching - {duration}s")
    all_types = ["StereoImage", "DepthMap", "PointCloud", "IMU", "Magnetometer", "Barometer"]
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    errors = []
    total_received = 0

    try:
        async with websockets.connect(uri, ssl=ctx, max_size=16 * 1024 * 1024) as ws:
            deadline = time.time() + duration
            idx = 0
            while time.time() < deadline:
                subset = all_types[idx % len(all_types) : idx % len(all_types) + 2]
                if len(subset) < 2:
                    subset = all_types[:2]
                topics = [f"{camera_id}/{dt}" for dt in subset]
                await ws.send(json.dumps({"action": "subscribe", "topics": topics}))
                recv_start = time.time()
                while time.time() - recv_start < 2.0 and time.time() < deadline:
                    try:
                        msg = await asyncio.wait_for(ws.recv(), timeout=1.0)
                        total_received += 1
                    except asyncio.TimeoutError:
                        break
                await ws.send(json.dumps({"action": "unsubscribe", "topics": topics}))
                await asyncio.sleep(0.5)
                idx += 1
    except Exception as e:
        errors.append(str(e))

    print(f"  Frames received: {total_received}, Errors: {len(errors)}")
    result = "PASS" if len(errors) == 0 and total_received > 0 else "FAIL"
    print(f"  Result: {result}")
    return result


async def test_backpressure(duration, uri, camera_id):
    import websockets

    print(f"\n>>> TEST: Backpressure & Queue Overflow - {duration}s")
    topics = [f"{camera_id}/StereoImage", f"{camera_id}/PointCloud"]
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    total_frames = 0
    delayed_frames = 0
    errors = []

    try:
        async with websockets.connect(uri, ssl=ctx, max_size=16 * 1024 * 1024) as ws:
            await ws.send(json.dumps({"action": "subscribe", "topics": topics}))
            deadline = time.time() + duration
            slow_mode = False
            toggle_interval = 10.0
            last_toggle = time.time()

            while time.time() < deadline:
                if time.time() - last_toggle > toggle_interval:
                    slow_mode = not slow_mode
                    last_toggle = time.time()

                try:
                    if slow_mode:
                        await asyncio.sleep(0.5)
                        msg = await asyncio.wait_for(ws.recv(), timeout=1.0)
                        delayed_frames += 1
                    else:
                        msg = await asyncio.wait_for(ws.recv(), timeout=2.0)
                    total_frames += 1
                except asyncio.TimeoutError:
                    pass
    except Exception as e:
        errors.append(str(e))

    print(f"  Total frames: {total_frames}, Delayed frames: {delayed_frames}")
    result = "PASS" if len(errors) == 0 and total_frames > 0 else "FAIL"
    print(f"  Result: {result}")
    return result


async def test_large_payload(duration, uri, camera_id):
    print(f"\n>>> TEST: Large Payload Stress - {duration}s")
    topics = [f"{camera_id}/StereoImage", f"{camera_id}/PointCloud", f"{camera_id}/DepthMap"]
    stats = await wss_client(0, uri, topics, duration)
    elapsed = stats.end_time - stats.start_time if stats.end_time > stats.start_time else 1
    mb = stats.total_bytes / (1024 * 1024)
    print(f"  Frames: {stats.total_frames}, Data: {mb:.2f} MB, FPS: {stats.total_frames/elapsed:.1f}")
    for dtype, sizes in stats.frame_sizes.items():
        avg = sum(sizes) / len(sizes) if sizes else 0
        mx = max(sizes) if sizes else 0
        print(f"  {dtype}: avg={avg/1024:.1f}KB, max={mx/1024:.1f}KB, count={len(sizes)}")
    result = "PASS" if len(stats.errors) == 0 and stats.total_frames > 0 else "FAIL"
    print(f"  Result: {result}")
    return result


async def main():
    parser = argparse.ArgumentParser(description="API 3 WSS Stress Test")
    parser.add_argument("--test", choices=[
        "all", "sustained", "multi", "rapid", "switching", "backpressure", "payload"
    ], default="all")
    parser.add_argument("--clients", type=int, default=5)
    parser.add_argument("--duration", type=int, default=60)
    parser.add_argument("--uri", type=str, default=WSS_URI)
    parser.add_argument("--camera", type=str, default=CAMERA_ID)
    args = parser.parse_args()

    topics = [f"{args.camera}/{dt}" for dt in
              ["StereoImage", "DepthMap", "PointCloud", "IMU", "Magnetometer", "Barometer"]]
    results = {}

    if args.test in ("all", "sustained"):
        results["sustained"] = await test_sustained_throughput(args.clients, args.duration, topics, args.uri)
    if args.test in ("all", "multi"):
        results["multi"] = await test_multi_client_load([1, 3, 5, 10], args.duration, topics, args.uri)
    if args.test in ("all", "rapid"):
        results["rapid"] = await test_rapid_connect_disconnect(20, topics, args.uri)
    if args.test in ("all", "switching"):
        results["switching"] = await test_type_switching(args.duration, args.uri, args.camera)
    if args.test in ("all", "backpressure"):
        results["backpressure"] = await test_backpressure(args.duration, args.uri, args.camera)
    if args.test in ("all", "payload"):
        results["payload"] = await test_large_payload(args.duration, args.uri, args.camera)

    print("\n" + "=" * 80)
    print("SUMMARY")
    print("=" * 80)
    for name, r in results.items():
        print(f"  {name:<20s}: {r}")
    all_pass = all(v == "PASS" for v in results.values())
    print(f"\nOverall: {'PASS' if all_pass else 'FAIL'}")
    print("=" * 80)


if __name__ == "__main__":
    asyncio.run(main())
