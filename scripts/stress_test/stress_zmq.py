import zmq
import json
import time
import threading
import argparse
import os
from datetime import datetime
from config import ZMQ_PUB_ENDPOINTS, REPORT_DIR


class ZMQSubscriber:
    def __init__(self, client_id, endpoint, topic_filter=b"", duration=60):
        self.client_id = client_id
        self.endpoint = endpoint
        self.topic_filter = topic_filter
        self.duration = duration
        self.total_frames = 0
        self.total_bytes = 0
        self.frames_per_topic = {}
        self.errors = []
        self.start_time = 0
        self.end_time = 0
        self.latencies = []

    def run(self):
        ctx = zmq.Context()
        sock = ctx.socket(zmq.SUB)
        try:
            sock.connect(self.endpoint)
            sock.setsockopt(zmq.SUBSCRIBE, self.topic_filter)
            sock.setsockopt(zmq.RCVTIMEO, 2000)
            self.start_time = time.time()
            deadline = self.start_time + self.duration

            while time.time() < deadline:
                try:
                    parts = sock.recv_multipart()
                    self.total_frames += 1
                    frame_bytes = sum(len(p) for p in parts)
                    self.total_bytes += frame_bytes
                    if len(parts) >= 1:
                        try:
                            header = json.loads(parts[0].decode("utf-8"))
                            dtype = header.get("type", "unknown")
                            ts_sec = header.get("ts_sec", 0)
                            ts_nsec = header.get("ts_nsec", 0)
                            if ts_sec > 0:
                                pub_time = ts_sec + ts_nsec / 1e9
                                latency = time.time() - pub_time
                                if latency > 0:
                                    self.latencies.append(latency)
                            self.frames_per_topic[dtype] = self.frames_per_topic.get(dtype, 0) + 1
                        except Exception:
                            self.frames_per_topic["raw"] = self.frames_per_topic.get("raw", 0) + 1
                except zmq.Again:
                    pass
                except zmq.ZMQError as e:
                    self.errors.append(str(e))
                    break
        except Exception as e:
            self.errors.append(f"ConnectError: {e}")
        finally:
            sock.close()
            ctx.term()
            self.end_time = time.time()


def run_zmq_stress(endpoints, num_clients, duration):
    all_subscribers = []
    for endpoint_name, endpoint_addr in endpoints.items():
        for i in range(num_clients):
            sub = ZMQSubscriber(
                client_id=len(all_subscribers),
                endpoint=endpoint_addr,
                topic_filter=b"",
                duration=duration,
            )
            all_subscribers.append(sub)

    threads = [threading.Thread(target=sub.run) for sub in all_subscribers]
    print(f"\n>>> Starting {len(all_subscribers)} ZMQ SUB clients across {len(endpoints)} endpoints for {duration}s")
    start = time.time()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.time() - start
    print_report("ZMQ_Multi_Client_Stress", all_subscribers, elapsed, endpoints, num_clients)
    return all_subscribers


def print_report(test_name, subscribers, elapsed, endpoints, num_clients):
    ts = datetime.now().strftime("%Y%m%d-%H%M%S-%f")[:21]
    print("\n" + "=" * 80)
    print(f"STRESS TEST REPORT - {test_name}")
    print("=" * 80)
    print(f"Timestamp     : {ts}")
    print(f"Duration      : {elapsed:.1f}s")
    print(f"Clients       : {len(subscribers)}")
    print(f"Endpoints     : {list(endpoints.keys())}")
    print("-" * 80)

    total_frames = 0
    total_bytes = 0
    total_errors = 0
    all_latencies = []
    per_type_totals = {}

    for s in subscribers:
        s_elapsed = s.end_time - s.start_time if s.end_time > s.start_time else 1
        fps = s.total_frames / s_elapsed
        mb = s.total_bytes / (1024 * 1024)
        print(
            f"  Client {s.client_id:>3d}: {s.total_frames:>6d} frames, "
            f"{fps:>6.1f} FPS, {mb:>7.2f} MB, errors={len(s.errors)}"
        )
        if s.errors:
            for e in s.errors[:5]:
                print(f"           ! {e}")
        total_frames += s.total_frames
        total_bytes += s.total_bytes
        total_errors += len(s.errors)
        all_latencies.extend(s.latencies)
        for dtype, cnt in s.frames_per_topic.items():
            per_type_totals[dtype] = per_type_totals.get(dtype, 0) + cnt

    print("-" * 80)
    print(f"Total frames : {total_frames}")
    print(f"Total data   : {total_bytes / (1024*1024):.2f} MB")
    print(f"Overall FPS  : {total_frames / elapsed:.1f} (aggregate)")
    print(f"Total errors : {total_errors}")

    if all_latencies:
        avg_lat = sum(all_latencies) / len(all_latencies) * 1000
        min_lat = min(all_latencies) * 1000
        max_lat = max(all_latencies) * 1000
        sorted_lat = sorted(all_latencies)
        p99_lat = sorted_lat[int(len(sorted_lat) * 0.99)] * 1000
        print(f"\nLatency (ms) : avg={avg_lat:.1f}, min={min_lat:.1f}, max={max_lat:.1f}, p99={p99_lat:.1f}")

    print(f"\nPer-type breakdown:")
    for dtype, cnt in sorted(per_type_totals.items()):
        print(f"  {dtype:<25s}: {cnt:>6d} frames")

    result = "PASS" if total_errors == 0 and total_frames > 0 else "FAIL"
    print(f"\nResult: {result}")
    print("=" * 80)

    os.makedirs(REPORT_DIR, exist_ok=True)
    report_file = os.path.join(REPORT_DIR, f"{test_name}_{ts}.json")
    report = {
        "test_name": test_name,
        "timestamp": ts,
        "duration": elapsed,
        "clients": len(subscribers),
        "endpoints": list(endpoints.keys()),
        "total_frames": total_frames,
        "total_bytes": total_bytes,
        "total_errors": total_errors,
        "per_type": per_type_totals,
        "latency_ms": {
            "avg": sum(all_latencies) / len(all_latencies) * 1000 if all_latencies else 0,
            "min": min(all_latencies) * 1000 if all_latencies else 0,
            "max": max(all_latencies) * 1000 if all_latencies else 0,
            "p99": sorted(all_latencies)[int(len(sorted(all_latencies)) * 0.99)] * 1000 if all_latencies else 0,
        },
        "result": result,
    }
    with open(report_file, "w") as f:
        json.dump(report, f, indent=2)
    print(f"Report saved: {report_file}")
    return result


def main():
    parser = argparse.ArgumentParser(description="API 2 ZMQ SUB Stress Test")
    parser.add_argument("--test", choices=["all", "multi"], default="all")
    parser.add_argument("--clients", type=int, default=5)
    parser.add_argument("--duration", type=int, default=60)
    args = parser.parse_args()

    results = {}
    if args.test in ("all", "multi"):
        run_zmq_stress(ZMQ_PUB_ENDPOINTS, args.clients, args.duration)
        results["multi_endpoint"] = "DONE"

    print("\n" + "=" * 80)
    print("SUMMARY")
    for name, r in results.items():
        print(f"  {name:<20s}: {r}")
    print("=" * 80)


if __name__ == "__main__":
    main()
