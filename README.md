# Robot ESP32 Platform

This repository contains the ESP32 firmware platform for the robot project.

## Current primary capability
- Passive UART-to-WebSocket telemetry bridge for Pico runtime logs.

## Current boundary
- The ESP32 is not the motion controller.
- The Pico remains the owner of robot control, safety, and timing-sensitive behavior.

## Planned structure
- `main/` keeps startup wiring minimal.
- `components/telemetry_bridge/` contains the implemented telemetry capability.
- `components/config/`, `components/network/`, and `components/diagnostics/` reserve clean boundaries for future platform capabilities.

## Canonical project context
- Shared architecture and subsystem boundaries live in `../docs` when this repo
  is checked out inside the parent `ai-robot` workspace.
- ESP32-specific implementation documentation lives in `docs/`.
