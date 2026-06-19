# Parameters

## Introduction

Parameters for stereo camera configuration, accessible via API 1 and API 2. See [API_module.md](API_module.md).

## Design Principles

- Each parameter has a field indicating whether it is **read-only** or **read & write**.
- Each parameter has a field indicating whether it is **available** for the connected camera SDK.

## Details

| Parameter | Type | Unit | R/W | Default | Description |
|-----------|------|------|-----|---------|-------------|
| FPS | Integer | fps | R/W | 30 | Frames per second |
| Exposure Time | Integer | nanoseconds | R/W | 10000000 | Camera exposure duration |
| Gain | Float | — | R/W | 1.0 | Camera gain value |
| Auto-Exposure | Enum (On/Off) | — | R/W | Off | Enable or disable auto-exposure |
| Auto-Gain | Enum (On/Off) | — | R/W | Off | Enable or disable auto-gain |
