# ESP32 Code Documentation Guide

## Purpose
Document the ESP32 repo so transport behavior stays maintainable without
blurring the subsystem boundary.

Audience:
- engineers changing ESP32 transport or diagnostics code;
- integrators tracing behavior across ESP32, Pico, and Android; and
- future maintainers verifying that ESP32 remains transport-only.

## Where Documentation Belongs
- Source comments: non-obvious C behavior a maintainer must understand while
  editing headers or source.
- Markdown docs: repo identity, implemented capabilities, multi-file transport
  behavior, and durable limitations or verification notes.
- Parent shared docs: system-wide ownership, safety, and architecture.

## What To Document
- exported functions and public headers;
- framing, CRC, heartbeat, and freshness behavior;
- failure and recovery behavior;
- timing or buffer assumptions;
- ownership boundaries where a reader might assume control authority.

Use header comments for contracts and source comments for `why`.

## What Not To Document
- obvious C mechanics, assignments, or loops;
- comments that imply the ESP32 owns motor control, safety stops, or drive
  arbitration; and
- duplicated shared-architecture text better kept in parent docs.

## C Style
- Use structured API comments in public headers.
- State what a function starts, publishes, depends on, and explicitly does not
  own.
- Keep source comments focused on protocol shape, heartbeat intent,
  resynchronization, and assumptions easy to break during refactors.
- When code or protocol shape comes from an external source, cite it near the
  relevant code.

## Robotics Rules
- ESP32 may own telemetry transport, controller connectivity, normalization,
  and forwarding.
- ESP32 must not own motor authority, stop/fault authority, or real-time
  drivetrain behavior.
- Document degraded paths, not only happy paths:
  - no snapshot available
  - short UART write
  - repeated-sequence heartbeat
  - framing recovery
- If heartbeat cadence, payload layout, framing, or controller freshness
  changes, update Markdown docs in the same change.

## Repo Examples
- [controller_transport.h](../components/controller_transport/include/controller_transport.h)
  shows the right header-level contract: normalized snapshots only, no robot
  meaning.
- [controller_transport.c](../components/controller_transport/controller_transport.c)
  shows a good `why` comment for repeated-sequence heartbeat behavior.
- [controller_snapshot.h](../components/controller_snapshot/include/controller_snapshot.h)
  shows a good boundary comment: normalized ranges here, robot meaning on the
  Pico.

## Review Checklist
- Public headers describe the exported contract.
- Source comments explain protocol, timing, recovery, or boundary assumptions.
- Comments explain `why`, not obvious syntax.
- No comment grants drivetrain or safety ownership to the ESP32.
- Protocol or heartbeat changes update Markdown docs too.

## References
- Doxygen docblocks:
  https://www.doxygen.nl/manual/docblocks.html
