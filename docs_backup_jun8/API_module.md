# API Module

## Shared Data API Rules

Both API 1 and API 2 share the following data API rules.

### Communication Mode

| Mode | Description |
|------|-------------|
| Mode 1 | Inter-Process (IPC) |
| Mode 2 | Inter-Machine / Network |

### Protocol

| Protocol | Mode 1 | Mode 2 |
|----------|--------|--------|
| ZMQ | IPC | Network (TCP) |

---

## API 1: Camera SDK ↔ StereoCamera Module

The API between the camera SDK and the StereoCamera module.

### Admin API

Protocol: **HTTPS** — StereoCamera module is the HTTPS client; camera SDK is the HTTPS server.

#### Commands

See [commands.md](commands.md) for full command definitions.

Direction: StereoCamera Module → Camera SDK (requests); Camera SDK → StereoCamera Module (responses).

### Data API

Data transfer between camera SDK and StereoCamera module.

| Property | Value |
|----------|-------|
| Socket Pattern | Dealer-to-Dealer |
| Connection | Maximum one simultaneous connection |

#### Design Principles

- Each queue can transfer multiple data types. Each command can start one or multiple queues.
- Timestamp-related data uses the same queue to transfer multiple data in one frame (e.g., 2 stereo images + calculated point cloud).

---

## API 2: StereoCamera Module ↔ Client Software

The API between the StereoCamera module and client software (native clients using ZMQ).

### Admin API

Protocol: **HTTPS** — StereoCamera module is the HTTPS server; client software is the HTTPS client.

#### Commands

See [commands.md](commands.md) for full command definitions.

Direction: Client Software → StereoCamera Module (requests); StereoCamera Module → Client Software (responses).

Init is idempotent: if already initialized by another client, returns success directly.

### Data API

Data transfer between client software and the StereoCamera module.

| Property | Value |
|----------|-------|
| Socket Pattern | PUB/SUB |
| Clients | Supports multiple simultaneous clients |

#### Design Principles

- Each queue can transfer multiple data types. Each command can start one or multiple queues.
- Timestamp-related data uses the same queue to transfer multiple data in one frame (e.g., 2 stereo images + calculated point cloud).
- Multiple clients can acquire data simultaneously. Clients subscribe to specified channels for the data they need.

---

## API 3: StereoCamera Module ↔ Web Client

The API between the StereoCamera module and any web-based client (browser, GUI, etc.).

### Admin API

Protocol: **HTTPS** — StereoCamera module is the HTTPS server; web client is the HTTPS client.

#### Commands

See [commands.md](commands.md) for full command definitions.

Direction: Web Client → StereoCamera Module (requests); StereoCamera Module → Web Client (responses).

Init is idempotent: if already initialized by another client, returns success directly.

### Data API

Real-time data streaming from StereoCamera module to web clients.

| Property | Value |
|----------|-------|
| Protocol | WebSocket (WSS) |
| Server Role | StereoCamera module is the WebSocket server |
| Clients | Supports multiple simultaneous web clients |

#### Subscription

After WebSocket handshake, clients send a JSON subscribe message to receive data:

```json
{ "action": "subscribe", "topics": ["cam1/StereoImage", "cam1/DepthMap"] }
```

To unsubscribe:

```json
{ "action": "unsubscribe", "topics": ["cam1/StereoImage"] }
```

#### Topic Scheme

Topic format: `<camera_id>/<DataType>`

Examples: `cam1/StereoImage`, `cam2/DepthMap`, `cam3/IMU`

See [data.md](data.md) for all available data types.

#### Data Transfer

- Binary frames for image and point cloud data (efficient transfer)
- JSON frames for control messages and status updates
- Timestamp-grouped data bundles on the same topic (e.g., L+R stereo images + point cloud)

#### Design Principles

- Each WebSocket connection supports subscribing to multiple topics
- Multiple web clients can subscribe to the same or different topics simultaneously
- Data served from the Data Module buffer — no re-fetch from camera SDK
- Client count does not affect camera SDK load
