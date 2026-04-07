/**
 * reflex_espnow.h — ESP-NOW Receiver for GIE Integration
 *
 * Lightweight ESP-NOW receiver. WiFi runs in STA mode without
 * connecting to any AP. Two consumption modes:
 *
 *   1. Poll (legacy): espnow_get_latest() — returns most recent packet,
 *      drops intermediate arrivals. OK for link-up checks.
 *
 *   2. Stream: espnow_drain() — returns ALL packets since last drain,
 *      each with real arrival timestamp. No drops. Use this for
 *      classification / timing-sensitive work.
 *
 * Implementation is in reflex_espnow.c.
 */

#ifndef REFLEX_ESPNOW_H
#define REFLEX_ESPNOW_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Packet format (must match sender) ── */
typedef struct __attribute__((packed)) {
    uint8_t  pattern_id;
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint8_t  payload[8];
} espnow_packet_t;

/* ── Ring buffer entry (one per received packet) ── */
typedef struct {
    int64_t  rx_timestamp_us;  /* esp_timer_get_time() at arrival */
    int8_t   rssi;
    espnow_packet_t pkt;
} espnow_rx_entry_t;

#define ESPNOW_RING_SIZE  64  /* Power of 2, holds ~6s at 10 Hz */

/* ── Receiver state (polled by main loop — legacy, kept for Tests 9/10) ── */
typedef struct {
    int8_t   rssi;           /* Signal strength of last packet (dBm) */
    uint8_t  pattern_id;     /* Pattern ID from last packet */
    uint32_t sequence;       /* Sequence number from last packet */
    uint32_t timestamp_ms;   /* Sender timestamp from last packet */
    uint8_t  payload[8];     /* Payload from last packet */
    uint32_t rx_count;       /* Total packets received */
    uint32_t last_rx_seq;    /* Sequence number we read (for change detection) */
    uint8_t  src_mac[6];     /* Source MAC of last packet */
    volatile uint32_t update_seq;  /* Incremented on each receive */
} espnow_state_t;

/* ── State (defined in reflex_espnow.c) ── */
extern volatile espnow_rx_entry_t _espnow_ring[ESPNOW_RING_SIZE];
extern volatile uint32_t _espnow_ring_head;
extern volatile uint32_t _espnow_ring_tail;
extern volatile uint32_t _espnow_ring_drops;
extern volatile espnow_state_t _espnow_state;

/**
 * Initialize WiFi (STA, no connection) + ESP-NOW receiver.
 * Call once from app_main before tests or after GIE init.
 *
 * Returns ESP_OK on success.
 */
esp_err_t espnow_receiver_init(void);

/**
 * Check if new data is available and copy it out.
 * Returns true if state was updated since last call.
 * NOTE: This is the legacy poll API — drops intermediate packets.
 * For stream processing, use espnow_drain() instead.
 */
bool espnow_get_latest(espnow_state_t *out);

/**
 * Drain all buffered packets into caller's array.
 * Returns the number of packets copied (0 if none available).
 * Each entry has the real arrival timestamp from esp_timer_get_time().
 *
 * max_entries: size of the output buffer.
 * If more packets are buffered than max_entries, only max_entries are
 * returned; remaining stay in the ring for the next drain call.
 */
int espnow_drain(espnow_rx_entry_t *out, int max_entries);

/**
 * Flush the ring buffer (discard all unread packets).
 * Call before starting a timed collection phase.
 */
void espnow_ring_flush(void);

/**
 * Get total received packet count.
 */
uint32_t espnow_rx_count(void);

/**
 * Get number of packets dropped due to ring overflow.
 */
uint32_t espnow_ring_drops(void);

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_ESPNOW_H */
