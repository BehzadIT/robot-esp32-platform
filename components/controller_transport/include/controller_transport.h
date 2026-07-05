#ifndef CONTROLLER_TRANSPORT_H
#define CONTROLLER_TRANSPORT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts the dedicated ESP32->Pico controller UART transport. This only
// forwards normalized controller snapshots; it does not assign robot meaning.
bool controller_transport_start(void);

#ifdef __cplusplus
}
#endif

#endif
