/*
 * espnow_sender.c — ESP-NOW Pattern Sender (Board B)
 *
 * Sends ESP-NOW packets to Board A at a configurable rate.
 * Each packet contains a pattern_id and sequence number.
 * Board A receives these and feeds RSSI + pattern data into the GIE.
 *
 * This is Phase 1 of the two-board experiment:
 *   Board B (this): sends known patterns via ESP-NOW
 *   Board A (GIE board): receives, quantizes RSSI to ternary, feeds CfC
 *
 * No AP connection required. ESP-NOW is peer-to-peer.
 *
 * Board: ESP32-C6FH4 (QFN32) rev v0.2
 * Peer:  Board A MAC B4:3A:45:8A:C4:D4
 */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "esp_timer.h"

/* ── Board A MAC address (the GIE receiver) ── */
static const uint8_t PEER_MAC[ESP_NOW_ETH_ALEN] = {
    0xB4, 0x3A, 0x45, 0x8A, 0xC4, 0xD4
};

/* ── Packet format ── */
typedef struct __attribute__((packed)) {
    uint8_t  pattern_id;     /* Which pattern is active (0-3) */
    uint32_t sequence;       /* Monotonic sequence number */
    uint32_t timestamp_ms;   /* Sender's uptime in ms */
    uint8_t  payload[8];     /* Pattern-specific payload */
} espnow_packet_t;

/* ── Send callback ── */
static volatile uint32_t send_ok = 0;
static volatile uint32_t send_fail = 0;

static void send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        send_ok++;
    } else {
        send_fail++;
    }
}

/* ── WiFi init (station mode, no connection) ── */
static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /* Create STA netif even though we won't connect — required by WiFi driver */
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Fix channel to 1 — both boards must be on the same channel */
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
}

/* ── ESP-NOW init ── */
static void espnow_init(void) {
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(send_cb));

    /* Add Board A as peer */
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, PEER_MAC, ESP_NOW_ETH_ALEN);
    peer.channel = 1;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
}

/* ── Pattern definitions ──
 *
 * 4 patterns with distinct temporal signatures:
 *   Pattern 0: Steady — 10 Hz, constant payload
 *   Pattern 1: Burst  — 3 packets fast, then pause
 *   Pattern 2: Slow   — 2 Hz, constant payload
 *   Pattern 3: Ramp   — 10 Hz, incrementing payload bytes
 */

static void send_pattern_0(uint32_t *seq) {
    /* Steady: 10 Hz, all-zero payload */
    espnow_packet_t pkt = {
        .pattern_id = 0,
        .sequence = (*seq)++,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .payload = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
    };
    esp_now_send(PEER_MAC, (uint8_t *)&pkt, sizeof(pkt));
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void send_pattern_1(uint32_t *seq) {
    /* Burst: 3 packets at 50ms interval, then 500ms pause */
    for (int i = 0; i < 3; i++) {
        espnow_packet_t pkt = {
            .pattern_id = 1,
            .sequence = (*seq)++,
            .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
            .payload = {0xFF, (uint8_t)i, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
        };
        esp_now_send(PEER_MAC, (uint8_t *)&pkt, sizeof(pkt));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
}

static void send_pattern_2(uint32_t *seq) {
    /* Slow: 2 Hz, alternating payload */
    uint8_t alt = (*seq) & 1;
    espnow_packet_t pkt = {
        .pattern_id = 2,
        .sequence = (*seq)++,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .payload = {0xAA, alt, alt, alt, alt, alt, alt, alt}
    };
    esp_now_send(PEER_MAC, (uint8_t *)&pkt, sizeof(pkt));
    vTaskDelay(pdMS_TO_TICKS(500));
}

static void send_pattern_3(uint32_t *seq) {
    /* Ramp: 10 Hz, incrementing payload bytes */
    uint8_t base = (uint8_t)(*seq);
    espnow_packet_t pkt = {
        .pattern_id = 3,
        .sequence = (*seq)++,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .payload = {base, (uint8_t)(base+1), (uint8_t)(base+2), (uint8_t)(base+3),
                    (uint8_t)(base+4), (uint8_t)(base+5), (uint8_t)(base+6), (uint8_t)(base+7)}
    };
    esp_now_send(PEER_MAC, (uint8_t *)&pkt, sizeof(pkt));
    vTaskDelay(pdMS_TO_TICKS(100));
}

/* ── Main ── */
void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(8000));  /* USB re-enumeration delay */

    printf("\n");
    printf("============================================================\n");
    printf("  ESP-NOW PATTERN SENDER (Board B)\n");
    printf("  Peer: Board A " MACSTR "\n", MAC2STR(PEER_MAC));
    printf("============================================================\n\n");

    /* NVS init (required for WiFi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Print our own MAC */
    uint8_t my_mac[6];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    printf("[INIT] My MAC: " MACSTR "\n", MAC2STR(my_mac));

    wifi_init();
    printf("[INIT] WiFi STA started (channel 1, no AP connection)\n");

    espnow_init();
    printf("[INIT] ESP-NOW initialized, peer added\n");
    printf("[INIT] Ready to send patterns.\n\n");
    fflush(stdout);

    uint32_t seq = 0;
    int pattern = 0;
    int64_t pattern_duration_us = 5000000LL;  /* 5 seconds per pattern (equal airtime) */
    int64_t pattern_start_us = esp_timer_get_time();

    /* Send loop: cycle through all 4 patterns with equal airtime.
     * Each pattern gets exactly pattern_duration_us regardless of its
     * per-cycle timing. This prevents P1 (burst+pause = 650ms/cycle)
     * from dominating P3 (steady = 100ms/cycle) in sample counts. */
    while (1) {
        switch (pattern) {
            case 0: send_pattern_0(&seq); break;
            case 1: send_pattern_1(&seq); break;
            case 2: send_pattern_2(&seq); break;
            case 3: send_pattern_3(&seq); break;
        }

        if ((esp_timer_get_time() - pattern_start_us) >= pattern_duration_us) {
            pattern_start_us = esp_timer_get_time();
            int old_pattern = pattern;
            pattern = (pattern + 1) % 4;
            printf("[SEND] Pattern %d -> %d | seq=%lu ok=%lu fail=%lu\n",
                   old_pattern, pattern,
                   (unsigned long)seq, (unsigned long)send_ok,
                   (unsigned long)send_fail);
            fflush(stdout);
        }
    }
}
