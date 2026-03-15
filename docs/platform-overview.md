# Platform Overview

## Purpose
Describe the repo identity and the current implemented ESP32 role.

## Repo identity
This repo is the robot ESP32 platform firmware workspace. The repo name is intentionally broader than the currently implemented capability so future ESP32 features can be added without renaming the project again.

## Current implemented capability
The only implemented runtime capability today is a passive UART-to-WebSocket telemetry bridge:
- Pico UART logs enter on ESP32 UART2.
- The ESP32 forwards lines to WebSocket clients on `/ws`.

## Ownership boundary
- Pico owns drivetrain control, safety behavior, and time-sensitive logic.
- ESP32 currently owns telemetry transport only.
- Future ESP32 features must preserve this boundary unless the canonical architecture docs are intentionally changed.
