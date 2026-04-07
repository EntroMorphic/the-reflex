/**
 * reflex_substrate_stream.h - Substrate Discovery Streaming Protocol
 *
 * Text-based protocol for streaming probe results to Rerun visualization.
 * Separate from reflex_stream.h (behavioral binary packets).
 *
 * Protocol:
 *   ##SUBSTRATE##:INIT
 *   ##SUBSTRATE##:PHASE:<phase_name>
 *   ##SUBSTRATE##:PROBE:<addr>,<type>,<read_cycles>,<write_cycles>
 *   ##SUBSTRATE##:REGION:<start>,<end>,<type>,<avg_cycles>
 *   ##SUBSTRATE##:MAP_START
 *   ##SUBSTRATE##:MAP_END:<total_probes>,<faults>,<regions>
 *
 * Copyright (c) 2026 EntroMorphic Research
 * MIT License
 */

#ifndef REFLEX_SUBSTRATE_STREAM_H
#define REFLEX_SUBSTRATE_STREAM_H

#include <stdint.h>
#include "reflex_substrate.h"

#ifdef __cplusplus
extern "C" {
#endif

// Stream marker for parser synchronization
#define SUBSTRATE_STREAM_MARKER "##SUBSTRATE##"

/**
 * Initialize streaming (prints sync marker)
 */
void substrate_stream_init(void);

/**
 * Stream a phase change notification
 */
void substrate_stream_phase(const char* phase_name);

/**
 * Stream a single probe result
 */
void substrate_stream_probe(const probe_result_t* result);

/**
 * Stream a region summary
 */
void substrate_stream_region(uint32_t start, uint32_t end, mem_type_t type, uint32_t avg_cycles);

/**
 * Stream map start marker
 */
void substrate_stream_map_start(void);

/**
 * Stream map end with summary
 */
void substrate_stream_map_end(uint32_t total_probes, uint32_t faults, int regions);

/**
 * Enable/disable streaming (default: enabled)
 */
void substrate_stream_enable(bool enable);

/**
 * Check if streaming is enabled
 */
bool substrate_stream_enabled(void);

#ifdef __cplusplus
}
#endif

#endif // REFLEX_SUBSTRATE_STREAM_H
