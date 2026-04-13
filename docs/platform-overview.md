# Platform Overview

## Purpose
Describe the repo identity and the current implemented ESP32 role.

## Repo identity
This repo is the robot ESP32 platform firmware workspace. The repo name is intentionally broader than the currently implemented capability so future ESP32 features can be added without renaming the project again.

## Current implemented capability
The currently implemented runtime capabilities are:
- Pico UART logs enter on ESP32 UART2.
- The ESP32 forwards lines to WebSocket clients on `/ws`.
- Bluepad32 receives controller input over Bluetooth, normalizes the snapshot,
  and forwards it to the Pico over the dedicated controller UART.

## Ownership boundary
- Pico owns drivetrain control, safety behavior, and time-sensitive logic.
- ESP32 currently owns telemetry transport plus controller-input transport.
- Future ESP32 features must preserve this boundary unless the canonical architecture docs are intentionally changed.
