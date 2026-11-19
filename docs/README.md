# ESP32 Platform Documentation

This directory contains ESP32-specific implementation documentation for the
robot platform firmware.

Use the parent `../docs` directory only when this repo is checked out inside the
`ai-robot` workspace. The ESP32 repo remains usable as an independent Git
repository.

## Documents
- `platform-overview.md`: repo identity and current ESP32 role.
- `telemetry-bridge.md`: UART-to-WebSocket bridge behavior and diagnostics.
- `code-documentation.md`: how to document ESP32 C transport code, boundaries,
  and robotics-specific assumptions.

## Agent Verification Adapter

`python3 tools/robot_adapter.py doctor --json` discovers ESP-IDF and host tools
without opening a serial port. `verify esp32-default --json` delegates the
tracked build to ESP-IDF. Flash, monitor, WebSocket, and backpressure scenarios
are separately gated and do not grant control authority to the ESP32.

## Parent Context
When available, read:
- `../../docs/overview/current-architecture.md`
- `../../docs/interfaces/telemetry-protocol.md`
- `../../docs/control-and-apps/esp32-telemetry-bridge.md`
