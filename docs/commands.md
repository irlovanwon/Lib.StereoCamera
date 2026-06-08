# Commands Reference

Shared admin commands used across API 1, API 2, and API 3.
See [API_module.md](API_module.md) for protocol and interface details.

## Admin Commands

| Command | Description | Precondition |
|---------|-------------|--------------|
| Init | Initialize the SDK/module. Loads resources (RAM, CPU, etc.). | None |
| Dispose | Dispose SDK/module resources. | Initialized |
| Connect | Establish connection. | Initialized |
| Disconnect | Teardown connection. | Connected |
| StartCapture | Start data capturing per data type independently. | Connected |
| StopCapture | Stop data capturing per data type. | Capturing |
| CheckStatus | Check active commands, data transfer status, and hardware/module health. | Any |
| SetParameter | Set a parameter. See [parameter.md](parameter.md). | Connected |
| GetParameter | Get a parameter. See [parameter.md](parameter.md). | Connected |

## Response

| Command | Description |
|---------|-------------|
| Response | Return code indicating success or failure, and detailed results. |

See [design.md](design.md) §9 for response codes.
