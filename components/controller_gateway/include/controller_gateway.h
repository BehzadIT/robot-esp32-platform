#ifndef CONTROLLER_GATEWAY_H
#define CONTROLLER_GATEWAY_H

#include <stdbool.h>

#include "controller_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the controller-side adapter layer only. This does not map inputs to
// robot commands, and it does not touch Pico transport.
bool controller_gateway_start(void);

// Bluepad32/BTstack owns the run loop. Call this only after all other ESP32
// runtime services are initialized because it does not return during normal
// operation.
void controller_gateway_run_loop(void);

// Returns the latest normalized controller snapshot if one has been published.
// The copied snapshot is a stable handoff point for later transport work.
bool controller_gateway_copy_latest(controller_snapshot_v1_t* out_snapshot);

#ifdef __cplusplus
}
#endif

#endif
