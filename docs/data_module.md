# Data Module

## Introduction

This module handles data storage, buffering, logging, and configuration file management for the StereoCamera module.

## Data Storage & Buffering

After capturing data from API 1 and before sending to API 2, this module buffers captured data and persists it to storage.

### Buffering Architecture

#### Design Goals

- Multiple clients may request the same data simultaneously. The buffer retains data so API 2 can transfer it efficiently without re-fetching from the camera SDK.
- **Bounded memory** — fixed-depth ring buffers prevent RAM overflow regardless of client count or data rate.
- **Zero-copy** — `std::shared_ptr<DataBundle>` shared ownership avoids per-client payload copies.
- **Per-SDK slotting** — buffer keyed by `(camera_id, DataType)`, not just `DataType`, so clients can subscribe to specific cameras.

#### Buffer Key Scheme

```
Key = (camera_id, DataType)

Example slots:
  ("cam1", StereoImage)  → RingBuffer, depth=3
  ("cam1", DepthMap)     → RingBuffer, depth=3
  ("cam2", StereoImage)  → RingBuffer, depth=3
  ("cam3", IMU)          → RingBuffer, depth=3
```

Slots are created dynamically when a camera SDK starts capturing a data type, and removed when the SDK disconnects.

#### Ring Buffer

Each slot contains a fixed-size ring buffer:

- **O(1) push** — write index wraps via power-of-2 bitmask, overwrites oldest frame
- **O(1) read** — read latest frame by index
- **No shifting** — unlike `vector::erase(begin())`, no element relocation
- **Default depth: 3 frames** — provides 1 frame being-written + 1 frame being-read + 1 frame jitter tolerance

#### Shared Pointer Ownership

```
Dealer thread:  ZMQ recv() → construct shared_ptr<DataBundle> → ring.push()
PUB thread:     ring.get_latest() → shared_ptr ref → ZMQ send (zero-copy)
```

- One `DataBundle` allocation per frame received from camera SDK
- All subscribers share the same `shared_ptr` — refcount prevents premature free
- Payload freed when last consumer (PUB send) releases its reference
- **Client count does NOT affect RAM usage**

#### Memory Budget (no disk storage)

Assume ~4 MB per frame (stereo image pair @ 1280×720):

| Depth | 3 SDKs, 1 DataType | 3 SDKs, 6 DataTypes |
|-------|--------------------|--------------------|
| 2 frames | ~24 MB | ~144 MB |
| 3 frames (default) | ~36 MB | ~216 MB |

#### Anti-Overflow Mechanisms

| Mechanism | Description |
|-----------|-------------|
| Fixed ring size | Hard cap, overwrite oldest when full. Never grows beyond budget |
| `shared_ptr` refcount | Payload freed immediately when last consumer releases |
| ZMQ SNDHWM=1 | If network buffers full, `send()` blocks/drops — backpressure to Dealer threads |
| No intermediate copies | Dealer → `shared_ptr` → ring → PUB. One alloc, one free per frame |
| Stale frame skip | If PUB detects write_index far ahead of read_index, skip to latest, log warning |

#### Dynamic Lifecycle

- Slots created when `CameraSDKClient::start_capture()` is called for a given SDK + DataType
- Slots destroyed when SDK disconnects or module disposes
- Dealer thread per SDK starts/stops based on subscriber reference count (not hardcoded)

### Storage

- Designed to support multiple data storage types (extensible virtual base class `DataStorage`).
- When no disk storage is required, the ring buffer alone serves all clients.

## Log System

- Provides logging for system events and user events.

## Configuration File Management

- Provides storage and management for all configuration files in the StereoCamera module.
- All currently active parameters shall be saved and synced to a config file. See [parameter.md](parameter.md).

### Camera SDK Configuration

Camera SDKs are defined in `config/default.json`:

```json
{
  "camera_sdks": [
    { "id": "cam1", "base_url": "https://192.168.1.10:8443", "zmq_endpoint": "ipc:///tmp/cam1" },
    { "id": "cam2", "base_url": "https://192.168.1.11:8443", "zmq_endpoint": "ipc:///tmp/cam2" }
  ],
  "buffer": {
    "max_frames_per_slot": 3
  },
  "pub": {
    "endpoint": "tcp://*:5556"
  }
}
```

- N camera SDKs defined → N `CameraSDKClient` instances created at startup
- Number of SDKs and clients is fully dynamic, not hardcoded
