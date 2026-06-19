# StereoCamera — Design Document

## 1. Overview

StereoCamera is a standalone C++17 module that abstracts multiple stereo camera hardware brands behind a unified API, processes stereo images into 3D data, and streams results to N downstream clients.

See also: [API Module](API_module.md) | [Commands](commands.md) | [GUI Module](GUI_module.md) | [Data Module](data_module.md) | [Parameters](parameter.md) | [Data Types](data.md)

## 2. Tech Stack

| Layer | Technology |
|-------|-----------|
| Core Language | C++17 |
| Build System | CMake 3.16+ |
| Messaging | ZeroMQ (ZMQ) — IPC + TCP |
| Admin Protocol | HTTPS (TLS mandatory, OpenSSL) |
| HTTP Client | libcurl |
| Serialization | JSON (nlohmann-json for config), ZMQ frames for data |
| WebSocket | libwebsocket or similar — WSS server for API 3 |
| Testing | Google Test (gtest) |
| Config Format | JSON, `config/` directory |
| Target Platform | Linux aarch64 (EDGE01), Linux x86_64 (ubuntu-home01) |

## 3. Module Architecture

```
   ┌──────────────┐   API 1    ┌──────────────────────────┐   API 2   ┌──────────────┐
   │  Camera SDK  │◄──────────►│    StereoCamera Module   │◄─────────►│    Client    │
   │  (N brands)  │  HTTPS+ZMQ │                          │  HTTPS+ZMQ│  Software    │
   └──────────────┘            │  ┌────────────────────┐  │           └──────────────┘
                                │  │ Data Module        │  │
   ┌──────────────┐            │  │  - Buffer          │  │           ┌──────────────┐
   │     GUI      │◄──────────►│  │  - Storage         │  │           │  Client N    │
   │  (React)     │ HTTPS+WSS  │  │  - Logging         │  │           │  (subscribes)│
   └──────────────┘            │  │  - Config Mgmt     │  │           └──────────────┘
                                │  └────────────────────┘  │
   ┌──────────────┐            │  ┌────────────────────┐  │
   │  Web Client  │◄──────────►│  │ Stereo Vision Algo │  │
   │  (Browser)   │  API 3     │  │  - Disparity Map   │  │
   └──────────────┘ HTTPS+WSS  │  │  - Depth Map       │  │
                                │  │  - Point Cloud     │  │
                                │  └────────────────────┘  │
                                │  ┌────────────────────┐  │
                                │  │ Calibration        │  │
                                │  │  - 2D / 3D         │  │
                                │  └────────────────────┘  │
                                └──────────────────────────┘
```

## 4. Source Code Structure

```
StereoCamera/
├── CMakeLists.txt
├── config/
│   └── default.json
├── include/stereo_camera/
│   ├── common/          Types.h, Parameter.h, Logger.h, Response.h
│   ├── api/             CameraSDKClient.h, ClientHandler.h, AdminServer.h, WebServer.h, WebSocketHandler.h
│   ├── data/            DataBuffer.h, DataStorage.h, ConfigManager.h
│   ├── vision/          DisparityMap.h, DepthMap.h, PointCloud.h
│   └── calibration/     Calibration2D.h, Calibration3D.h
├── src/                 (matching .cpp for each header)
│   └── main.cpp
├── tests/
│   ├── test_parameter.cpp
│   ├── test_data_buffer.cpp
│   └── test_config_manager.cpp
├── library/             (shared reusable code, empty placeholder)
└── build/               (generated)
```

### 4.1 Module Map

| Module | Source Dir | Responsibility |
|--------|-----------|----------------|
| **Common** | `common/` | Shared types (`DataType`, `Timestamp`, `DataBundle`), parameter management, response codes, logging |
| **API** | `api/` | Three-layer API: CameraSDKClient (API 1 HTTPS client), ClientHandler + AdminServer (API 2 HTTPS server), WebServer + WebSocketHandler (API 3 HTTPS+WSS server) |
| **Data** | `data/` | Ring buffer (`DataBuffer`), file storage (`DataStorage`), JSON config sync (`ConfigManager`) |
| **Vision** | `vision/` | Disparity map, depth map, point cloud generation |
| **Calibration** | `calibration/` | 2D intrinsic calibration, 3D stereo calibration |
| **GUI** | (separate project) | React + Three.js frontend, served by Nginx |

## 5. API Contracts

### 5.1 API 1 — Camera SDK ↔ Module

**Admin (HTTPS):** Module = HTTPS Client (libcurl), Camera SDK = HTTPS Server

See [commands.md](commands.md) for full command definitions.

**Data (ZMQ):** Dealer↔Dealer, max 1 simultaneous connection. Timestamp-grouped bundles.

### 5.2 API 2 — Module ↔ Client Software

**Admin (HTTPS):** Module = HTTPS Server (OpenSSL), Client = HTTPS Client

See [commands.md](commands.md) for full command definitions. Init is idempotent.

**Data (ZMQ):** PUB/SUB, supports N simultaneous clients. Clients subscribe to channels by data type.

### 5.3 API 3 — Module ↔ Web Client

**Admin (HTTPS):** Module = HTTPS Server, Web Client = HTTPS Client

See [commands.md](commands.md) for full command definitions. Init is idempotent.

**Data (WebSocket/WSS):** Module = WebSocket Server, supports N simultaneous web clients.

| Property | Value |
|----------|-------|
| Protocol | WSS (WebSocket over TLS) |
| Data Transfer | Binary frames (images, point cloud), JSON frames (status, control) |
| Topic Scheme | `<camera_id>/<DataType>` |
| Subscription | JSON message: `{ "action": "subscribe", "topics": [...] }` |

See [API_module.md](API_module.md) for full API 3 specification.

## 6. Data Types

| Data Type | Description |
|-----------|-------------|
| Stereo Images | Left + right image pairs per frame |
| Depth Map | Per-pixel depth derived from stereo |
| Point Cloud | 3D spatial data (X, Y, Z) |
| IMU Data | Acceleration + angular velocity |
| Disparity Map | Pixel-level L/R displacement |
| Confidence/Error Map | Per-pixel confidence for depth |

## 7. Parameters

Every parameter carries: `{ value, is_readonly: bool, is_available: bool }`

| Parameter | Type | Unit | R/W | Default |
|-----------|------|------|-----|---------|
| FPS | Integer | fps | R/W | 30 |
| Exposure Time | Integer | nanoseconds | R/W | 10000000 |
| Gain | Float | — | R/W | 1.0 |
| Auto-Exposure | Enum (On/Off) | — | R/W | Off |
| Auto-Gain | Enum (On/Off) | — | R/W | Off |

## 8. Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| `ParameterManager` with `is_readonly` + `is_available` | Enables graceful degradation across camera brands |
| `DataBuffer` as per-type ring buffer | Thread-safe, bounded memory, serves N clients without re-fetch |
| `DataStorage` as virtual base class | Extensible to database, network storage, etc. |
| pImpl pattern for `CameraSDKClient` and `AdminServer` | Hides libcurl/OpenSSL details from headers |
| JSON for config, ZMQ frames for data | JSON is human-readable for config; ZMQ frames are zero-copy for high-throughput data |
| Static library (`libstereo_camera_lib.a`) | Single binary `stereo_camera_node` for easy deployment |
| Separate WebSocket server (API 3) alongside ZMQ PUB (API 2) | Both read from same buffer; decouples native (ZMQ) and web (WSS) client paths |

## 9. Response Codes

| Code | Name | Meaning |
|------|------|---------|
| 0 | Success | Operation completed |
| 1 | Error | General failure |
| 2 | NotReady | Precondition not met (e.g., not connected) |
| 3 | AlreadyInit | Already initialized (idempotent success) |
| 4 | InvalidParam | Parameter name or value invalid |
| 5 | Unavailable | Feature not supported by current camera |

## 10. Thread Model

### 10.1 Thread Map

| Thread | Count | Responsibility |
|--------|-------|---------------|
| Main | 1 | Event loop, signal handling, config sync on shutdown |
| Admin HTTPS Server | 1 | Accept loop, spawns per-request handler for API 2 |
| ZMQ API 1 Dealer | N (1 per active SDK) | Receives data from camera SDK. Spawned/stopped by subscriber refcount |
| ZMQ API 2 PUB | 1 | Publishes data to N subscribers. Runs while any Dealer is active |
| WebSocket Server (API 3) | 1 | Accept loop for WSS connections, manages N web client sessions |
| Vision Processing | 0–N | Optional, disparity/depth/point cloud computation pipeline |

**Total: 4 + N + V threads** (1 main + 1 admin + N Dealers + 1 PUB + 1 WebSocket server + 0–N Vision)

All shared state protected by `std::mutex` (e.g., `DataBuffer`).

### 10.2 Dynamic Thread Lifecycle

Dealer threads are **not hardcoded** — they are managed by subscriber reference counting:

```
struct SDKSlot {
    std::string camera_id;
    std::shared_ptr<CameraSDKClient> client;
    std::unique_ptr<std::thread> dealer_thread;
    std::atomic<bool> capturing{false};
    int subscriber_count = 0;
    RingBuffer<shared_ptr<DataBundle>> buffer;
};
```

- `subscriber_count++` on `start_capture`, `--` on `stop_capture`
- Dealer thread starts when count goes 0→1, stops when it goes 1→0
- PUB thread runs while at least one Dealer is active

### 10.3 Thread Data Flow

```
 SDK-1 (30fps) ──Dealer──► Thread-recv-sdk1 ──► Ring[SDK1][DataType] ──┐
                                                                       │
 SDK-2 (30fps) ──Dealer──► Thread-recv-sdk2 ──► Ring[SDK2][DataType] ──┼──► PUB Thread ──► ZMQ SUB clients
                                                                       │
 SDK-N (30fps) ──Dealer──► Thread-recv-sdkN ──► Ring[SDKN][DataType] ──┤
                                                                       └──► WS Thread ────► WebSocket clients
```

- Client A subscribes to topic `cam1/StereoImage` → receives SDK-1 frames only
- Client B subscribes to `cam2/StereoImage` + `cam3/StereoImage` → receives SDK-2 + SDK-3 frames
- ZMQ PUB/SUB handles fan-out natively — module does not track subscriber count for data delivery

### 10.4 Health Monitoring

Each `CameraSDKClient` instance supports `check_status()`. A periodic timer (main thread or dedicated) calls `check_status()` per SDK to detect disconnections and trigger reconnect.

### 10.5 Buffering Design

See [Data Module](data_module.md) for full buffering specification.

#### Key Design Principles

1. **Per-SDK slotting** — Buffer key = `(camera_id, DataType)`, not just `DataType`
2. **Zero-copy shared ownership** — `std::shared_ptr<DataBundle>` in ring buffer; N clients share same payload
3. **Bounded ring buffer** — O(1) push/overwrite, fixed depth, configurable (default 3 frames)
4. **No disk dependency** — Without storage, buffer depth of 2–3 frames is sufficient (33–100ms at 30fps)
5. **Client count does not affect RAM** — `shared_ptr` refcount; ZMQ handles network fan-out

#### RAM Budget (no disk storage)

Assume ~4 MB per frame (stereo pair @ 1280×720):

| Config | 3 SDKs, 1 DataType | 3 SDKs, 6 DataTypes |
|--------|--------------------|--------------------|
| 2 frames | ~24 MB | ~144 MB |
| 3 frames (default) | ~36 MB | ~216 MB |
| 30 frames (old) | ~360 MB | ~2.16 GB |

#### Topic Scheme

ZMQ PUB/SUB topic format: `<camera_id>/<DataType>`

Examples: `cam1/StereoImage`, `cam2/DepthMap`, `cam3/IMU`

## 11. Key Constraints

| ID | Constraint |
|----|-----------|
| C1 | API 1 Data = max 1 simultaneous ZMQ connection per camera SDK |
| C2 | API 2 Data = N simultaneous ZMQ SUB clients supported |
| C3 | All admin communication = HTTPS (TLS mandatory) |
| C4 | Every parameter response includes `is_available` + `is_readonly` flags |
| C5 | Data bundles grouped by timestamp travel on the same queue/channel |
| C6 | Module is fully independent — no direct coupling beyond HTTPS/ZMQ boundaries |
| C7 | StartCapture/StopCapture are per-data-type, not all-or-nothing |
| C8 | Init is idempotent: second Init from another client returns success |
| C9 | GUI single external port (443), path-based reverse proxy routing |
| C10 | Responsive design: must function on mobile through desktop |
| C11 | API 3 Data = N simultaneous WebSocket (WSS) clients supported |
| C12 | API 3 admin = HTTPS (TLS mandatory), same command set as API 2 |

## 12. Implementation Status

### Completed

| Component | Status | Details |
|-----------|--------|---------|
| Project skeleton | Done | CMake build, compiles on EDGE01 (aarch64) |
| Common types | Done | `DataType`, `Timestamp`, `DataBundle`, `ResponseCode` |
| Parameter system | Done | Define, get, set with readonly/available enforcement, JSON serialization |
| Logger | Done | Singleton, level-filtered, timestamped stderr output |
| API 1 Admin (CameraSDKClient) | Done | libcurl HTTPS client, all 9 commands |
| API 2 Admin (ClientHandler) | Done | Session management, all 9 handlers |
| API 2 Admin (AdminServer) | Partial | OpenSSL TLS server, routes 5/9 commands |
| DataBuffer | Done | Thread-safe per-type ring buffer |
| FileStorage | Done | Binary format with JSON header |
| ConfigManager | Done | JSON load/save/sync |
| DepthMap | Done | `depth = (baseline * focal_length) / disparity` |
| PointCloud | Done | Pixel-to-3D projection from depth map |
| Tests | Done | 13 tests, 100% pass (Parameter, DataBuffer, ConfigManager) |

### Not Yet Implemented

| Component | Status | Notes |
|-----------|--------|-------|
| ZMQ API 1 Data (Dealer) | Not started | Receive data from camera SDK |
| ZMQ API 2 Data (PUB/SUB) | Not started | Publish data to N clients |
| API 3 WebServer (HTTPS+WSS) | Not started | HTTPS + WebSocket server for web clients |
| API 3 WebSocketHandler | Not started | Topic subscription, binary/JSON frame handling |
| DisparityMap algorithm | Stub | `compute()` returns zeros |
| Calibration 2D algorithm | Stub | `calibrate()` returns default params |
| Calibration 3D algorithm | Stub | `calibrate()` returns default params |
| AdminServer full routing | Partial | Missing 4 commands in router |
| Logger file output | Not started | Currently stderr only |
| GUI (React + Three.js) | Not started | Separate frontend project |

## 13. Build & Run

```bash
cd /home/user/ECIDS/StereoCamera
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# Run tests
ctest --output-on-failure

# Run node
./stereo_camera_node [config_dir] [cert_path] [key_path]
```

## 14. Deployment

| Server | IP | Role | Path |
|--------|-----|------|------|
| ubuntu-home01 | 100.86.203.33 | Development | `/home/irlovan/Downloads/Project/One` |
| EDGE01 | 100.85.117.73 | Deployment | `/home/user/ECIDS/StereoCamera` |
