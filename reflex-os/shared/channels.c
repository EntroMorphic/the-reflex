/**
 * channels.c - Channel instances
 *
 * These are placed in a memory region accessible by both cores.
 * On ESP32-C6, this is just regular SRAM (unified memory).
 */

#include "channels.h"

/**
 * Channel instances
 *
 * Initialized to zero. Sequence starts at 0, increments on each signal.
 */
reflex_channel_t ctrl_channel = {0};
reflex_channel_t telem_channel = {0};
reflex_channel_t ack_channel = {0};
reflex_channel_t debug_channel = {0};
reflex_channel_t error_channel = {0};

/**
 * Initialize all channels
 */
void channels_init(void) {
    // Channels are statically initialized to zero
    // This function exists for explicit initialization if needed

    ctrl_channel.sequence = 0;
    ctrl_channel.value = 0;
    ctrl_channel.timestamp = 0;
    ctrl_channel.flags = 0;

    telem_channel.sequence = 0;
    telem_channel.value = 0;
    telem_channel.timestamp = 0;
    telem_channel.flags = 0;

    ack_channel.sequence = 0;
    ack_channel.value = 0;
    ack_channel.timestamp = 0;
    ack_channel.flags = 0;

    debug_channel.sequence = 0;
    debug_channel.value = 0;
    debug_channel.timestamp = 0;
    debug_channel.flags = 0;

    error_channel.sequence = 0;
    error_channel.value = 0;
    error_channel.timestamp = 0;
    error_channel.flags = 0;
}
