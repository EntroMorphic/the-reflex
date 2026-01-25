/**
 * reflex_stream.h - Event Stream for External Observation
 *
 * Compact binary packets over serial.
 * Pi4 receives and visualizes in Rerun.
 */

#ifndef REFLEX_STREAM_H
#define REFLEX_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Packet Structure (44 bytes)
// ============================================================

#define STREAM_MAGIC    0x52  // 'R' for Reflex
#define STREAM_VERSION  1

typedef struct __attribute__((packed)) {
    // Header (4 bytes)
    uint8_t  magic;           // 0xRF
    uint8_t  version;         // Protocol version
    uint16_t tick;            // Tick number (wraps at 65535)

    // Layer scores (24 bytes) - scaled to uint8 (0-255)
    uint8_t  slow_scores[8];  // Layer 0 scores
    uint8_t  med_scores[8];   // Layer 1 scores
    uint8_t  fast_scores[8];  // Layer 2 scores

    // Decision (4 bytes)
    uint8_t  chosen_output;   // Which output was chosen
    uint8_t  chosen_state;    // HIGH=1, LOW=0
    uint8_t  agreement;       // Agreement level (0-255)
    uint8_t  disagreement;    // Disagreement level (0-255)

    // Observation (8 bytes)
    int16_t  delta_adc[4];    // ADC deltas

    // Counts (4 bytes)
    uint8_t  output_counts[8]; // Scaled exploration counts

} stream_packet_t;

_Static_assert(sizeof(stream_packet_t) == 48, "Packet size must be 48 bytes");

// ============================================================
// Packet Building
// ============================================================

static inline void stream_pack(
    stream_packet_t* pkt,
    uint32_t tick,
    float slow_scores[8],
    float med_scores[8],
    float fast_scores[8],
    uint8_t chosen_output,
    uint8_t chosen_state,
    float agreement_pct,
    float disagreement_pct,
    int16_t adc_deltas[4],
    uint32_t output_counts[8]
) {
    pkt->magic = STREAM_MAGIC;
    pkt->version = STREAM_VERSION;
    pkt->tick = (uint16_t)(tick & 0xFFFF);

    // Scale scores to 0-255
    for (int i = 0; i < 8; i++) {
        pkt->slow_scores[i] = (uint8_t)(slow_scores[i] > 1000 ? 255 : slow_scores[i] * 0.255f);
        pkt->med_scores[i] = (uint8_t)(med_scores[i] > 1000 ? 255 : med_scores[i] * 0.255f);
        pkt->fast_scores[i] = (uint8_t)(fast_scores[i] > 1000 ? 255 : fast_scores[i] * 0.255f);
    }

    pkt->chosen_output = chosen_output;
    pkt->chosen_state = chosen_state;
    pkt->agreement = (uint8_t)(agreement_pct * 255);
    pkt->disagreement = (uint8_t)(disagreement_pct * 255);

    for (int i = 0; i < 4; i++) {
        pkt->delta_adc[i] = adc_deltas[i];
    }

    // Scale counts to 0-255
    for (int i = 0; i < 8; i++) {
        pkt->output_counts[i] = (uint8_t)(output_counts[i] > 255 ? 255 : output_counts[i]);
    }
}

// ============================================================
// Serial Output
// ============================================================

static inline void stream_send(stream_packet_t* pkt) {
    // Write raw bytes to stdout (serial)
    fwrite(pkt, sizeof(stream_packet_t), 1, stdout);
    fflush(stdout);
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_STREAM_H
