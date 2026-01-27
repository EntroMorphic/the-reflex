/**
 * reflex_substrate_stream.c - Substrate Discovery Streaming Protocol
 *
 * Streams probe results over serial for Rerun visualization.
 *
 * Copyright (c) 2026 EntroMorphic Research
 * MIT License
 */

#include "reflex_substrate_stream.h"
#include "reflex_substrate.h"
#include <stdio.h>
#include <stdbool.h>

static bool s_streaming_enabled = true;

void substrate_stream_init(void) {
    if (!s_streaming_enabled) return;
    printf("\n%s:INIT\n", SUBSTRATE_STREAM_MARKER);
    fflush(stdout);
}

void substrate_stream_phase(const char* phase_name) {
    if (!s_streaming_enabled) return;
    printf("%s:PHASE:%s\n", SUBSTRATE_STREAM_MARKER, phase_name);
    fflush(stdout);
}

void substrate_stream_probe(const probe_result_t* result) {
    if (!s_streaming_enabled) return;
    printf("%s:PROBE:%08lx,%d,%lu,%lu\n",
           SUBSTRATE_STREAM_MARKER,
           (unsigned long)result->addr,
           (int)result->type,
           (unsigned long)result->read_cycles,
           (unsigned long)result->write_cycles);
    // Don't flush every probe - too slow
}

void substrate_stream_region(uint32_t start, uint32_t end, mem_type_t type, uint32_t avg_cycles) {
    if (!s_streaming_enabled) return;
    printf("%s:REGION:%08lx,%08lx,%d,%lu\n",
           SUBSTRATE_STREAM_MARKER,
           (unsigned long)start,
           (unsigned long)end,
           (int)type,
           (unsigned long)avg_cycles);
    fflush(stdout);
}

void substrate_stream_map_start(void) {
    if (!s_streaming_enabled) return;
    printf("%s:MAP_START\n", SUBSTRATE_STREAM_MARKER);
    fflush(stdout);
}

void substrate_stream_map_end(uint32_t total_probes, uint32_t faults, int regions) {
    if (!s_streaming_enabled) return;
    printf("%s:MAP_END:%lu,%lu,%d\n",
           SUBSTRATE_STREAM_MARKER,
           (unsigned long)total_probes,
           (unsigned long)faults,
           regions);
    fflush(stdout);
}

void substrate_stream_enable(bool enable) {
    s_streaming_enabled = enable;
}

bool substrate_stream_enabled(void) {
    return s_streaming_enabled;
}
