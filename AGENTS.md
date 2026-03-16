# AGENTS.md

## Purpose
This repo contains the robot ESP32 platform firmware.

## Instructions
- Before changing code here, read the shared docs root at `robot-project-docs`.
- At minimum, review:
  - `robot-project-docs/overview/current-architecture.md`
  - `robot-project-docs/overview/design-decisions.md`
  - `robot-project-docs/control-and-apps/esp32-telemetry-bridge.md`
  - `robot-project-docs/interfaces/telemetry-protocol.md`
  - `robot-project-docs/electrical/wiring-and-pinout.md`
  - `robot-project-docs/verification/known-issues-and-open-questions.md`
- Preserve the subsystem boundary: the ESP32 is a passive log bridge, not the motor controller.
- Treat the repo identity as broader than the currently implemented capability. The current primary capability is the passive telemetry bridge, but future ESP32 capabilities may be added if they preserve the documented ownership boundaries.
- Do not add robot control, safety logic, or control-loop ownership here unless the architecture docs are intentionally changed.
- When making non-trivial decisions, state the relevant best practice, design pattern, technology choice, and architecture tradeoff.
- Prefer reliability-oriented telemetry patterns: explicit connection state, recoverable protocol handling, and structured diagnostics.
- Code documentation matters in this project. Add clear comments or docstrings for non-obvious behavior, especially around connection lifecycle, recovery, protocol handling, subsystem boundaries, and failure behavior.
- Do not comment obvious implementation steps; focus on the assumptions and behaviors that are easy to break by accident.
- If implementation and docs diverge, update the shared docs and note unresolved issues in verification.
