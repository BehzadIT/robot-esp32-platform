# Robot ESP32 Platform

This repository contains the ESP32 firmware platform for the robot project.

## Current primary capabilities
- UART-to-WebSocket telemetry bridge for Pico runtime logs.
- Phase 1B Xbox controller gateway integration using Bluepad32 in the real
  ESP32 runtime.
- Dedicated ESP32-to-Pico controller UART transport for normalized controller
  snapshots.

## Current boundary
- The ESP32 is not the motion controller.
- The Pico remains the owner of robot control, safety, and timing-sensitive behavior.
- The ESP32 controller path is limited to controller connectivity, normalization,
  diagnostics, and transport. It does not own robot mapping or motor authority.

## Planned structure
- `main/` keeps startup wiring minimal.
- `components/platform_runtime/` owns startup composition for runtime services.
- `components/telemetry_bridge/` contains the telemetry capability.
- `components/controller_gateway/` contains Bluepad32 lifecycle, controller
  normalization, and sampled diagnostics.
- `components/controller_transport/` contains the dedicated ESP32-to-Pico UART
  sender for framed controller snapshots.
- `components/controller_snapshot/` defines the stable normalized controller DTO.
- `components/config/`, `components/network/`, and `components/diagnostics/`
  provide shared platform support.

## Canonical project context
- Shared architecture and subsystem boundaries live in `../docs` when this repo
  is checked out inside the parent `ai-robot` workspace.
- ESP32-specific implementation documentation lives in `docs/`.
