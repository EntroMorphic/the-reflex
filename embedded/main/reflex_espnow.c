/*
 * reflex_espnow.c — ESP-NOW Receiver Implementation
 *
 * Contains the ring buffer state, receive callback, and all functions
 * that were previously static inline in the header. Separated from
 * the header to prevent silent duplicate storage if the header is
 * included by multiple translation units.
 *
 * See reflex_espnow.h for the public API.
 */

#include "reflex_espnow.h"

/* ── Ring buffer state (single writer = WiFi task, single reader = main loop) ── */
volatile espnow_rx_entry_t _espnow_ring[ESPNOW_RING_SIZE];
volatile uint32_t _espnow_ring_head = 0;  /* Next write position (writer only) */
volatile uint32_t _espnow_ring_tail = 0;  /* Next read position (reader only) */
volatile uint32_t _espnow_ring_drops = 0; /* Packets dropped when ring full */

/* ── Receiver state (polled by main loop — legacy, kept for Tests 9/10) ── */
volatile espnow_state_t _espnow_state = {0};

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

esp_err_t espnow_receiver_init(void) {
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

bool espnow_get_latest(espnow_state_t *out) {
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

int espnow_drain(espnow_rx_entry_t *out, int max_entries) {
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

void espnow_ring_flush(void) {
    _espnow_ring_tail = _espnow_ring_head;
}

uint32_t espnow_rx_count(void) {
    return _espnow_state.rx_count;
}

uint32_t espnow_ring_drops(void) {
    return _espnow_ring_drops;
}
