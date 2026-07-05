#ifndef PLATFORM_RUNTIME_H
#define PLATFORM_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

// Starts the ESP32 runtime composition root. This is the boundary where shared
// platform setup happens before handing execution to long-running services.
// The runtime may compose telemetry bridging plus controller transport, but it
// does not assign robot-drive meaning or take safety-control ownership away
// from the Pico.
void platform_runtime_start(void);

#ifdef __cplusplus
}
#endif

#endif
