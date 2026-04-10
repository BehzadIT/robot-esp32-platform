# AGENTS.md

## Purpose
This repo contains the robot ESP32 platform firmware.

## Instructions
- When working from the parent `ai-robot` workspace, read the shared docs root at
  `../docs`.
- At minimum, review:
  - `../docs/overview/current-architecture.md`
  - `../docs/overview/design-decisions.md`
  - `../docs/control-and-apps/esp32-telemetry-bridge.md`
  - `../docs/interfaces/telemetry-protocol.md`
  - `../docs/electrical/wiring-and-pinout.md`
  - `../docs/verification/known-issues-and-open-questions.md`
- This repository is a submodule of the parent `ai-robot` integration
  workspace, but it keeps its own Git history and release cycle.
- Preserve the subsystem boundary: the ESP32 is a passive log bridge, not the motor controller.
- Treat the repo identity as broader than the currently implemented capability. The current primary capability is the passive telemetry bridge, but future ESP32 capabilities may be added if they preserve the documented ownership boundaries.
- Do not add robot control, safety logic, or control-loop ownership here unless the architecture docs are intentionally changed.
- When making non-trivial decisions, state the relevant best practice, design pattern, technology choice, and architecture tradeoff.
- Before making any non-trivial code or design change, explicitly tell the user:
  - the architectural boundary being preserved or changed
  - the design pattern or implementation style being used
  - the best practice being followed
  - the software engineering reason for the choice
  - the robotics/embedded reason for the choice, especially around ownership, reliability, timing, and fault isolation
- For trivial edits, keep this brief but still mention whether the change is structural, behavioral, or purely local.
- If no named pattern is appropriate, say that directly and explain the simpler rule being followed instead.
- Prefer reliability-oriented telemetry patterns: explicit connection state, recoverable protocol handling, and structured diagnostics.
- Favor boundary-preserving patterns such as adapters, finite state handling, explicit interfaces between transport and hardware-facing concerns, and dependency seams that keep the ESP32 as a passive bridge.
- When proposing architecture changes, explain why the change does or does not belong on the ESP32 instead of the Pico or Android app.
- Code documentation matters in this project. Add clear comments or docstrings for non-obvious behavior, especially around connection lifecycle, recovery, protocol handling, subsystem boundaries, and failure behavior.
- Do not comment obvious implementation steps; focus on the assumptions and behaviors that are easy to break by accident.
- If implementation and docs diverge, update the shared docs and note unresolved issues in verification.
- If the change updates a known-good integrated robot state, update the parent
  repository's submodule pointer after committing this repo.

## Communication Contract
- For meaningful tasks, give the user a short design note before editing or implementing:
  - architecture: what subsystem owns the behavior
  - pattern: state machine, adapter, facade, composition root, or another concrete choice
  - best practice: the operational or maintainability rule being followed
  - reasoning: why that choice improves correctness, diagnosability, or separation of concerns for this robot system
- After implementation, summarize whether the result preserved the passive telemetry-bridge role and call out any tradeoffs, limitations, or follow-up verification needed.
