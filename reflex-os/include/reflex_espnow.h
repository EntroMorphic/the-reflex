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
 * The receive callback runs in the WiFi task context (not ISR),
 * so it's safe to write to BSS but should be fast.
 *
 * Usage (stream mode):
 *   espnow_receiver_init();
 *   // ... later ...
 *   espnow_rx_entry_t buf[32];
 *   int n = espnow_drain(buf, 32);
 *   for (int i = 0; i < n; i++) {
 *       // buf[i].rx_timestamp_us, buf[i].rssi, buf[i].pkt are valid
 *   }
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

/* ── Ring buffer (single writer = WiFi task, single reader = main loop) ── */
static volatile espnow_rx_entry_t _espnow_ring[ESPNOW_RING_SIZE];
static volatile uint32_t _espnow_ring_head = 0;  /* Next write position (writer only) */
static volatile uint32_t _espnow_ring_tail = 0;  /* Next read position (reader only) */
static volatile uint32_t _espnow_ring_drops = 0; /* Packets dropped when ring full */

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

static volatile espnow_state_t _espnow_state = {0};

/* ── Receive callback (called from WiFi task context) ── */
static void espnow_recv_cb(const esp_now_recv_info_t *info,
                            const uint8_t *data, int data_len) {
    if (data_len < (int)sizeof(espnow_packet_t)) return;

    const espnow_packet_t *pkt = (const espnow_packet_t *)data;
    int64_t now_us = esp_timer_get_time();

    /* ── Push into ring buffer ── */
    uint32_t head = _espnow_ring_head;
    uint32_t next = (head + 1) & (ESPNOW_RING_SIZE - 1);
    if (next == _espnow_ring_tail) {
        /* Ring full — drop oldest (advance tail) */
        _espnow_ring_tail = (_espnow_ring_tail + 1) & (ESPNOW_RING_SIZE - 1);
        _espnow_ring_drops++;
    }
    _espnow_ring[head].rx_timestamp_us = now_us;
    _espnow_ring[head].rssi = info->rx_ctrl->rssi;
    memcpy((void *)&_espnow_ring[head].pkt, pkt, sizeof(espnow_packet_t));
    _espnow_ring_head = next;

    /* ── Legacy single-state update (for get_latest) ── */
    _espnow_state.rssi = info->rx_ctrl->rssi;
    _espnow_state.pattern_id = pkt->pattern_id;
    _espnow_state.sequence = pkt->sequence;
    _espnow_state.timestamp_ms = pkt->timestamp_ms;
    memcpy((void *)_espnow_state.payload, pkt->payload, 8);
    memcpy((void *)_espnow_state.src_mac, info->src_addr, 6);
    _espnow_state.rx_count++;
    _espnow_state.update_seq++;  /* Signal new data available */
}

/**
 * Initialize WiFi (STA, no connection) + ESP-NOW receiver.
 * Call once from app_main before tests or after GIE init.
 *
 * Returns ESP_OK on success.
 */
static inline esp_err_t espnow_receiver_init(void) {
    /* NVS (required for WiFi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return ret;

    /* Network interface + event loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    /* WiFi init — STA mode, no connection */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Match sender's channel */
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));

    /* ESP-NOW init + register receive callback */
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    return ESP_OK;
}

/**
 * Check if new data is available and copy it out.
 * Returns true if state was updated since last call.
 * NOTE: This is the legacy poll API — drops intermediate packets.
 * For stream processing, use espnow_drain() instead.
 */
static inline bool espnow_get_latest(espnow_state_t *out) {
    uint32_t current_seq = _espnow_state.update_seq;
    if (current_seq == out->last_rx_seq) return false;

    /* Copy volatile state */
    out->rssi = _espnow_state.rssi;
    out->pattern_id = _espnow_state.pattern_id;
    out->sequence = _espnow_state.sequence;
    out->timestamp_ms = _espnow_state.timestamp_ms;
    memcpy(out->payload, (const void *)_espnow_state.payload, 8);
    memcpy(out->src_mac, (const void *)_espnow_state.src_mac, 6);
    out->rx_count = _espnow_state.rx_count;
    out->last_rx_seq = current_seq;
    return true;
}

/**
 * Drain all buffered packets into caller's array.
 * Returns the number of packets copied (0 if none available).
 * Each entry has the real arrival timestamp from esp_timer_get_time().
 *
 * max_entries: size of the output buffer.
 * If more packets are buffered than max_entries, only max_entries are
 * returned; remaining stay in the ring for the next drain call.
 */
static inline int espnow_drain(espnow_rx_entry_t *out, int max_entries) {
    int count = 0;
    uint32_t tail = _espnow_ring_tail;
    uint32_t head = _espnow_ring_head;

    while (tail != head && count < max_entries) {
        out[count].rx_timestamp_us = _espnow_ring[tail].rx_timestamp_us;
        out[count].rssi = _espnow_ring[tail].rssi;
        memcpy(&out[count].pkt, (const void *)&_espnow_ring[tail].pkt,
               sizeof(espnow_packet_t));
        tail = (tail + 1) & (ESPNOW_RING_SIZE - 1);
        count++;
    }
    _espnow_ring_tail = tail;
    return count;
}

/**
 * Flush the ring buffer (discard all unread packets).
 * Call before starting a timed collection phase.
 */
static inline void espnow_ring_flush(void) {
    _espnow_ring_tail = _espnow_ring_head;
}

/**
 * Get total received packet count.
 */
static inline uint32_t espnow_rx_count(void) {
    return _espnow_state.rx_count;
}

/**
 * Get number of packets dropped due to ring overflow.
 */
static inline uint32_t espnow_ring_drops(void) {
    return _espnow_ring_drops;
}

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_ESPNOW_H */
