# ESP32 Telemetry Bridge

## Purpose
Describe the ESP32 subsystem as currently implemented and constrain its future role.

## Current status
- `Implemented`: station-mode Wi-Fi client with reconnecting WebSocket bridge and minimal health endpoint.
- `Implemented`: multiple clients supported in the current implementation.
- `Implemented`: connect-time ready banner plus JSON status for bench clients.
- `Current state`: bounded RAM backlog remains in memory for bridge diagnostics, but connect-time replay is not implemented.
- `Design decision`: ESP32 remains passive and must not take over control duties.
- `Project note`: the ESP32 codebase now lives in the broader `robot-esp32-platform` repo even though telemetry bridging is still the only implemented runtime capability.

## Implemented behavior
- Connects to Wi-Fi using configured SSID/password.
- Configures UART2 at `115200`.
- Reads UART through the ESP-IDF UART event queue and assembles line-oriented text frames.
- Stores a bounded in-memory backlog of recent lines and important bridge warnings.
- Broadcasts live lines to WebSocket clients on `/ws`.
- On new WebSocket connection, sends a text readiness line and a JSON readiness packet before joining the live stream.
- Exposes `GET /healthz` with bridge readiness and telemetry counters.
- Emits bridge-generated warning/status lines only for important transitions such as UART detected, UART idle, truncation, drops, and UART driver fault events.

## Boundaries
- No motor control.
- No control-loop timing.
- No robot safety logic.
- No command arbitration.

## Bench use
PC-side log viewing is intended through a WebSocket client such as `websocat`.
- Default live view: `websocat ws://<bridge-ip>/ws`

## Sources
- Code: [telemetry_bridge.c](../components/telemetry_bridge/telemetry_bridge.c)
