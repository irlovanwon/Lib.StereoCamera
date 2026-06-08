# Lib.StereoCamera

## Introduction

StereoCamera is a software module for stereo camera integration and 3D vision processing. It provides a unified interface to communicate with multiple stereo camera brands, process stereo images, and output 3D data.

## Module Architecture

| Module | Description | Document |
|--------|-------------|----------|
| API Module | Two-layer API for camera SDK and client communication | [API_module.md](API_module.md) |
| GUI | Management and monitoring interface for all modules | [GUI_module.md](GUI_module.md) |
| Data Module | Data storage, buffering, logging, and config management | [data_module.md](data_module.md) |
| Stereo Vision Algorithm | Disparity map and 3D data computation from stereo images | [TBD] |
| Calibration | 2D camera calibration and 3D stereo calibration | [TBD] |

### API Overview

| API | From | To | Description |
|-----|------|----|-------------|
| API 1 | Camera SDK | StereoCamera Module | Hardware abstraction layer for multiple camera brands |
| API 2 | StereoCamera Module | Client Software | Unified interface for native downstream applications (HTTPS + ZMQ) |
| API 3 | StereoCamera Module | Web Client | Web interface for browser-based clients (HTTPS + WebSocket) |

## Programming Language & Environment

| Item | Value |
|------|-------|
| Language | C++ 17 |
| Build System | CMake |

## Design Principles

1. **Multi-brand compatibility** — The API supports multiple camera SDK brands. Compatibility across hardware is a priority.
2. **Graceful feature degradation** — The module exposes common stereo camera APIs. When a specific brand does not support an API, a validity field indicates whether the attribute is available.
3. **Independent module** — StereoCamera operates as a standalone module. It communicates with camera SDKs and other software via HTTPS, message queues, or other IoT interfaces.

## Related Documents

- [API Module](API_module.md)
- [Commands](commands.md)
- [GUI Module](GUI_module.md)
- [Data Module](data_module.md)
- [Parameters](parameter.md)
- [Data for Communication](data.md)
- [Development Rules](development_rule.md)
- [Server Guide](server.md)
- [Architecture Blueprint](architecture_blueprint.txt)

---

# Custom Instructions for OpenCode

## Code Generation Constraints
- CRITICAL: Never rewrite whole files, large classes, or unchanged logic.
- Always use concise placeholder comments (e.g., `// ... existing camera loop code ...`) to skip over blocks of code that do not require modifications.
- Output modifications purely as targeted snippets or precise diff code blocks. Specify exactly which function is being modified.
- Avoid conversational introductions, pleasantries, or concluding summary paragraphs. Output structural code immediately.
- If an architectural rule is violated, flag it concisely instead of generating an alternative massive file rewrite.

## Project Workflow

This local workspace contains **design and planning documents only**.
Software development happens on remote Linux servers.

| Server | IP | Role | Remote Path | Local Doc |
|--------|----|------|-------------|-----------|
| ubuntu-home01 | 100.86.203.33 | Development | `/home/irlovan/Downloads/Project/One` | This workspace |
| EDGE01 | 100.85.117.73 | Deployment | `/home/user/ECIDS/StereoCamera` | This workspace |

- Do NOT generate C++ or source code in this workspace — it is for `.md` docs only.
- For code tasks, guide the user to execute on the remote server, or SSH into it.
- See `server.md` for full connection details and git repo mapping.
