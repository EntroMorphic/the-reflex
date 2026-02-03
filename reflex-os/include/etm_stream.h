/**
 * etm_stream.h - Binary streaming protocol for ETM events
 *
 * Streams structured events over USB serial to host for Rerun visualization.
 *
 * Protocol (binary, little-endian):
 *   Header: 0xAA 0x55 (sync)
 *   Type:   1 byte (event type)
 *   Len:    1 byte (payload length)
 *   Payload: variable (0-255 bytes)
 *   CRC:    1 byte (XOR of type + len + payload)
 */

#ifndef ETM_STREAM_H
#define ETM_STREAM_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Protocol constants
#define ETM_SYNC_1          0xAA
#define ETM_SYNC_2          0x55

// Event types
#define ETM_EVT_PCNT_UPDATE     0x01  // 4x int16 counts
#define ETM_EVT_THRESHOLD       0x02  // uint8 channel, uint8 thresh_id, int16 count
#define ETM_EVT_PATTERN_START   0x03  // uint8 pattern_id
#define ETM_EVT_PATTERN_END     0x04  // uint8 pattern_id, uint32 duration_us
#define ETM_EVT_DMA_EOF         0x05  // uint8 channel
#define ETM_EVT_ETM_EVENT       0x06  // uint8 event_id, uint8 task_id
#define ETM_EVT_STATE_CHANGE    0x07  // uint8 old_state, uint8 new_state
#define ETM_EVT_CYCLE_COMPLETE  0x08  // uint32 cycle_num, uint32 duration_us
#define ETM_EVT_HEARTBEAT       0x10  // uint32 uptime_ms, uint32 cycles
#define ETM_EVT_TEXT_LOG        0x20  // variable length text

// Pattern IDs
#define PATTERN_A   0  // Sparse
#define PATTERN_B   1  // Medium
#define PATTERN_C   2  // Dense

// State IDs
#define STATE_IDLE      0
#define STATE_PATTERN_A 1
#define STATE_PATTERN_B 2
#define STATE_PATTERN_C 3
#define STATE_WAITING   4

// Threshold IDs
#define THRESH_A    0  // 32
#define THRESH_B    1  // 64
#define THRESH_C    2  // 128

// ============================================================
// Streaming Buffer
// ============================================================

#define ETM_STREAM_BUF_SIZE 64

typedef struct {
    uint8_t buf[ETM_STREAM_BUF_SIZE];
    uint8_t len;
} etm_stream_packet_t;

// ============================================================
// Packet Building Functions
// ============================================================

/**
 * Initialize a packet with sync bytes and event type
 */
static inline void etm_stream_init(etm_stream_packet_t *pkt, uint8_t event_type) {
    pkt->buf[0] = ETM_SYNC_1;
    pkt->buf[1] = ETM_SYNC_2;
    pkt->buf[2] = event_type;
    pkt->buf[3] = 0;  // Length placeholder
    pkt->len = 4;     // Header complete
}

/**
 * Add bytes to payload
 */
static inline void etm_stream_add_bytes(etm_stream_packet_t *pkt, const void *data, uint8_t len) {
    if (pkt->len + len < ETM_STREAM_BUF_SIZE - 1) {
        memcpy(&pkt->buf[pkt->len], data, len);
        pkt->len += len;
    }
}

/**
 * Add uint8 to payload
 */
static inline void etm_stream_add_u8(etm_stream_packet_t *pkt, uint8_t val) {
    etm_stream_add_bytes(pkt, &val, 1);
}

/**
 * Add int16 to payload (little-endian)
 */
static inline void etm_stream_add_i16(etm_stream_packet_t *pkt, int16_t val) {
    etm_stream_add_bytes(pkt, &val, 2);
}

/**
 * Add uint32 to payload (little-endian)
 */
static inline void etm_stream_add_u32(etm_stream_packet_t *pkt, uint32_t val) {
    etm_stream_add_bytes(pkt, &val, 4);
}

/**
 * Finalize packet: set length and compute CRC
 * Returns total packet size
 */
static inline uint8_t etm_stream_finalize(etm_stream_packet_t *pkt) {
    // Set payload length (excluding header and CRC)
    uint8_t payload_len = pkt->len - 4;
    pkt->buf[3] = payload_len;
    
    // Compute CRC: XOR of type + len + payload
    uint8_t crc = pkt->buf[2] ^ pkt->buf[3];
    for (int i = 4; i < pkt->len; i++) {
        crc ^= pkt->buf[i];
    }
    pkt->buf[pkt->len] = crc;
    pkt->len++;
    
    return pkt->len;
}

// ============================================================
// High-Level Event Functions
// ============================================================

/**
 * Build PCNT_UPDATE packet (4 channel counts)
 */
static inline uint8_t etm_stream_pcnt_update(etm_stream_packet_t *pkt, 
                                              int16_t c0, int16_t c1, 
                                              int16_t c2, int16_t c3) {
    etm_stream_init(pkt, ETM_EVT_PCNT_UPDATE);
    etm_stream_add_i16(pkt, c0);
    etm_stream_add_i16(pkt, c1);
    etm_stream_add_i16(pkt, c2);
    etm_stream_add_i16(pkt, c3);
    return etm_stream_finalize(pkt);
}

/**
 * Build THRESHOLD packet
 */
static inline uint8_t etm_stream_threshold(etm_stream_packet_t *pkt,
                                            uint8_t channel, uint8_t thresh_id,
                                            int16_t count) {
    etm_stream_init(pkt, ETM_EVT_THRESHOLD);
    etm_stream_add_u8(pkt, channel);
    etm_stream_add_u8(pkt, thresh_id);
    etm_stream_add_i16(pkt, count);
    return etm_stream_finalize(pkt);
}

/**
 * Build PATTERN_START packet
 */
static inline uint8_t etm_stream_pattern_start(etm_stream_packet_t *pkt,
                                                uint8_t pattern_id) {
    etm_stream_init(pkt, ETM_EVT_PATTERN_START);
    etm_stream_add_u8(pkt, pattern_id);
    return etm_stream_finalize(pkt);
}

/**
 * Build PATTERN_END packet
 */
static inline uint8_t etm_stream_pattern_end(etm_stream_packet_t *pkt,
                                              uint8_t pattern_id,
                                              uint32_t duration_us) {
    etm_stream_init(pkt, ETM_EVT_PATTERN_END);
    etm_stream_add_u8(pkt, pattern_id);
    etm_stream_add_u32(pkt, duration_us);
    return etm_stream_finalize(pkt);
}

/**
 * Build DMA_EOF packet
 */
static inline uint8_t etm_stream_dma_eof(etm_stream_packet_t *pkt,
                                          uint8_t channel) {
    etm_stream_init(pkt, ETM_EVT_DMA_EOF);
    etm_stream_add_u8(pkt, channel);
    return etm_stream_finalize(pkt);
}

/**
 * Build ETM_EVENT packet
 */
static inline uint8_t etm_stream_etm_event(etm_stream_packet_t *pkt,
                                            uint8_t event_id, uint8_t task_id) {
    etm_stream_init(pkt, ETM_EVT_ETM_EVENT);
    etm_stream_add_u8(pkt, event_id);
    etm_stream_add_u8(pkt, task_id);
    return etm_stream_finalize(pkt);
}

/**
 * Build STATE_CHANGE packet
 */
static inline uint8_t etm_stream_state_change(etm_stream_packet_t *pkt,
                                               uint8_t old_state,
                                               uint8_t new_state) {
    etm_stream_init(pkt, ETM_EVT_STATE_CHANGE);
    etm_stream_add_u8(pkt, old_state);
    etm_stream_add_u8(pkt, new_state);
    return etm_stream_finalize(pkt);
}

/**
 * Build CYCLE_COMPLETE packet
 */
static inline uint8_t etm_stream_cycle_complete(etm_stream_packet_t *pkt,
                                                 uint32_t cycle_num,
                                                 uint32_t duration_us) {
    etm_stream_init(pkt, ETM_EVT_CYCLE_COMPLETE);
    etm_stream_add_u32(pkt, cycle_num);
    etm_stream_add_u32(pkt, duration_us);
    return etm_stream_finalize(pkt);
}

/**
 * Build HEARTBEAT packet
 */
static inline uint8_t etm_stream_heartbeat(etm_stream_packet_t *pkt,
                                            uint32_t uptime_ms,
                                            uint32_t cycles) {
    etm_stream_init(pkt, ETM_EVT_HEARTBEAT);
    etm_stream_add_u32(pkt, uptime_ms);
    etm_stream_add_u32(pkt, cycles);
    return etm_stream_finalize(pkt);
}

/**
 * Build TEXT_LOG packet
 */
static inline uint8_t etm_stream_text_log(etm_stream_packet_t *pkt,
                                           const char *text) {
    etm_stream_init(pkt, ETM_EVT_TEXT_LOG);
    uint8_t len = strlen(text);
    if (len > 50) len = 50;  // Limit text length
    etm_stream_add_bytes(pkt, text, len);
    return etm_stream_finalize(pkt);
}

#ifdef __cplusplus
}
#endif

#endif // ETM_STREAM_H
