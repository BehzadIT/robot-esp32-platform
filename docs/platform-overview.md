# Platform Overview

## Purpose
Describe the repo identity and the current implemented ESP32 role.

## Repo identity
This repo is the robot ESP32 platform firmware workspace. The repo name is intentionally broader than the currently implemented capability so future ESP32 features can be added without renaming the project again.

## Current implemented capability
The currently implemented runtime capabilities are:
- Pico UART logs enter on ESP32 UART2.
- The ESP32 forwards lines to WebSocket clients on `/ws`.
- The current WebSocket server supports multiple connected clients.
- Bluepad32 receives controller input over Bluetooth, normalizes the snapshot,
  and forwards it to the Pico over the dedicated controller UART.
- The controller UART transport sends frames on input change and also sends a
  heartbeat copy of the latest snapshot at least every `100 ms` even when
  input is steady, so Pico-side freshness logic can distinguish `same state`
  from `no packet`.

## Target gateway capability
The next clean-start system architecture expands this repo from telemetry bridge
to robot wireless gateway:
- serve robot WebSocket endpoints from the ESP32 instead of requiring Pico
  Wi-Fi/CYW43 for the main C++ control firmware;
- preserve the existing `/ws` telemetry path until bench tools are migrated;
- add structured stats and diagnostics endpoints for Android and Linux clients;
- optionally receive Android operator-intent commands and forward bounded DTOs
  to the Pico over UART;
- keep command/control traffic independent from best-effort log broadcasting.

Prefer one ESP32 HTTP/WebSocket server with multiple routes such as `/ws/logs`,
`/ws/stats`, and later `/ws/control`, rather than multiple independent servers.

## Ownership boundary
- Pico owns drivetrain control, safety behavior, and time-sensitive logic.
- ESP32 owns telemetry transport, controller-input transport, robot wireless
  gateway support, and bounded command transport when enabled.
- Future ESP32 features must preserve this boundary unless the canonical architecture docs are intentionally changed.
