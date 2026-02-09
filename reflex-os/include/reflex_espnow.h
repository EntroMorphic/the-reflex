/**
 * reflex_espnow.h — ESP-NOW Receiver for GIE Integration
 *
 * Lightweight ESP-NOW receiver. WiFi runs in STA mode without
 * connecting to any AP. Received packets are stored in a volatile
 * struct that the main loop can poll.
 *
 * The receive callback runs in the WiFi task context (not ISR),
 * so it's safe to write to BSS but should be fast.
 *
 * Usage:
 *   espnow_receiver_init();
 *   // ... later ...
 *   espnow_state_t state;
 *   if (espnow_get_latest(&state)) {
 *       // state.rssi, state.pattern_id, state.sequence are valid
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

/* ── Receiver state (polled by main loop) ── */
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

    /* Write state atomically-ish (single writer, main loop is reader) */
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
 * Get total received packet count.
 */
static inline uint32_t espnow_rx_count(void) {
    return _espnow_state.rx_count;
}

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_ESPNOW_H */
