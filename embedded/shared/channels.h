/**
 * channels.h - System channel definitions
 *
 * All inter-core communication channels defined here.
 * Both HP and LP cores include this header.
 */

#ifndef CHANNELS_H
#define CHANNELS_H

#include "reflex.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * System Channels
 *
 * These are the communication channels between HP and LP cores.
 * All placed in shared memory accessible by both cores.
 */

// Control channel: LP → HP
// LP sends tick/commands, HP executes control loop
extern reflex_channel_t ctrl_channel;

// Telemetry channel: HP → LP
// HP sends sensor readings and state, LP logs/transmits
extern reflex_channel_t telem_channel;

// Ack channel: HP → LP
// HP acknowledges command receipt (for synchronous operation)
extern reflex_channel_t ack_channel;

// Debug channel: HP → LP
// HP sends debug values, LP prints to UART
extern reflex_channel_t debug_channel;

// Error channel: HP → LP
// HP signals errors, LP handles recovery
extern reflex_channel_t error_channel;

/**
 * Channel initialization
 */
void channels_init(void);

/**
 * Error codes for error_channel
 */
#define REFLEX_ERR_NONE         0
#define REFLEX_ERR_TIMEOUT      1
#define REFLEX_ERR_SENSOR       2
#define REFLEX_ERR_ACTUATOR     3
#define REFLEX_ERR_OVERRUN      4

/**
 * Debug codes for debug_channel
 */
#define REFLEX_DBG_LATENCY      0x01  // value = latency in cycles
#define REFLEX_DBG_LOOP_COUNT   0x02  // value = loop iteration count
#define REFLEX_DBG_SENSOR_RAW   0x03  // value = raw sensor reading

#ifdef __cplusplus
}
#endif

#endif // CHANNELS_H
