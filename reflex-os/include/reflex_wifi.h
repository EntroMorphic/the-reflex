/**
 * reflex_wifi.h - WiFi as Channels for ESP32-C6
 *
 * Network events are channel signals:
 * - Connection state changes -> status channel
 * - Received packets -> RX channel
 * - Transmit requests -> TX channel
 *
 * WiFi 6 (802.11ax) on ESP32-C6 becomes just another set of channels.
 */

#ifndef REFLEX_WIFI_H
#define REFLEX_WIFI_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "reflex.h"

// ESP-IDF WiFi includes
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// WiFi Channel States
// ============================================================

typedef enum {
    WIFI_STATE_IDLE = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_GOT_IP,
    WIFI_STATE_ERROR
} wifi_channel_state_t;

// ============================================================
// WiFi Channel Structure
// ============================================================

typedef struct {
    // Status channel - signals state changes
    reflex_channel_t status;

    // IP address (when connected)
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;

    // Connection info
    char ssid[33];
    int8_t rssi;

    // Stats
    uint32_t connect_count;
    uint32_t disconnect_count;
    uint32_t packets_rx;
    uint32_t packets_tx;
} reflex_wifi_channel_t;

// Global WiFi channel instance
static reflex_wifi_channel_t* _wifi_channel = NULL;

// ============================================================
// Event Handlers (internal)
// ============================================================

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (!_wifi_channel) return;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                reflex_signal(&_wifi_channel->status, WIFI_STATE_CONNECTING);
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_CONNECTED:
                _wifi_channel->connect_count++;
                reflex_signal(&_wifi_channel->status, WIFI_STATE_CONNECTED);
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                _wifi_channel->disconnect_count++;
                reflex_signal(&_wifi_channel->status, WIFI_STATE_DISCONNECTED);
                // Auto-reconnect
                esp_wifi_connect();
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
            _wifi_channel->ip_addr = event->ip_info.ip.addr;
            _wifi_channel->netmask = event->ip_info.netmask.addr;
            _wifi_channel->gateway = event->ip_info.gw.addr;
            reflex_signal(&_wifi_channel->status, WIFI_STATE_GOT_IP);
        }
    }
}

// ============================================================
// WiFi Channel API
// ============================================================

/**
 * Initialize WiFi channel and connect to AP
 *
 * @param wifi      WiFi channel structure
 * @param ssid      Network SSID
 * @param password  Network password
 * @return          ESP_OK on success
 */
static inline esp_err_t wifi_channel_init(reflex_wifi_channel_t* wifi,
                                           const char* ssid,
                                           const char* password) {
    memset(wifi, 0, sizeof(*wifi));
    strncpy(wifi->ssid, ssid, sizeof(wifi->ssid) - 1);
    _wifi_channel = wifi;

    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Configure station
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Signal initial state
    reflex_signal(&wifi->status, WIFI_STATE_IDLE);

    // Start WiFi (will trigger STA_START event)
    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

/**
 * Wait for WiFi to get IP address (blocking)
 *
 * @param wifi          WiFi channel
 * @param timeout_ms    Timeout in milliseconds (0 = forever)
 * @return              true if connected, false if timeout
 */
static inline bool wifi_wait_connected(reflex_wifi_channel_t* wifi, uint32_t timeout_ms) {
    uint32_t start = reflex_cycles();
    uint32_t timeout_cycles = timeout_ms * 160000;  // 160MHz

    while (wifi->status.value != WIFI_STATE_GOT_IP) {
        if (timeout_ms > 0 && (reflex_cycles() - start) > timeout_cycles) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

/**
 * Check if connected and has IP
 */
static inline bool wifi_is_connected(reflex_wifi_channel_t* wifi) {
    return wifi->status.value == WIFI_STATE_GOT_IP;
}

/**
 * Get current state
 */
static inline wifi_channel_state_t wifi_get_state(reflex_wifi_channel_t* wifi) {
    return (wifi_channel_state_t)wifi->status.value;
}

/**
 * Get IP address as string
 */
static inline void wifi_get_ip_str(reflex_wifi_channel_t* wifi, char* buf, size_t len) {
    uint8_t* ip = (uint8_t*)&wifi->ip_addr;
    snprintf(buf, len, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

/**
 * Get RSSI (signal strength)
 */
static inline int8_t wifi_get_rssi(reflex_wifi_channel_t* wifi) {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        wifi->rssi = ap_info.rssi;
    }
    return wifi->rssi;
}

/**
 * Disconnect from AP
 */
static inline void wifi_disconnect(reflex_wifi_channel_t* wifi) {
    esp_wifi_disconnect();
    reflex_signal(&wifi->status, WIFI_STATE_DISCONNECTED);
}

// ============================================================
// UDP Channel (simple datagram communication)
// ============================================================

#include "lwip/sockets.h"

typedef struct {
    int sock;                       // Socket descriptor
    struct sockaddr_in dest_addr;   // Destination address
    reflex_channel_t tx_channel;    // Outbound signal
    reflex_channel_t rx_channel;    // Inbound signal
} reflex_udp_channel_t;

/**
 * Initialize UDP channel
 */
static inline esp_err_t udp_channel_init(reflex_udp_channel_t* udp,
                                          const char* dest_ip,
                                          uint16_t dest_port,
                                          uint16_t local_port) {
    memset(udp, 0, sizeof(*udp));

    // Create socket
    udp->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp->sock < 0) {
        return ESP_FAIL;
    }

    // Set non-blocking
    int flags = fcntl(udp->sock, F_GETFL, 0);
    fcntl(udp->sock, F_SETFL, flags | O_NONBLOCK);

    // Bind to local port
    struct sockaddr_in local_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(local_port),
    };
    if (bind(udp->sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        close(udp->sock);
        return ESP_FAIL;
    }

    // Set destination
    udp->dest_addr.sin_family = AF_INET;
    udp->dest_addr.sin_port = htons(dest_port);
    inet_pton(AF_INET, dest_ip, &udp->dest_addr.sin_addr);

    return ESP_OK;
}

/**
 * Send UDP packet (signal TX channel)
 */
static inline int udp_send(reflex_udp_channel_t* udp, const void* data, size_t len) {
    int sent = sendto(udp->sock, data, len, 0,
                      (struct sockaddr*)&udp->dest_addr, sizeof(udp->dest_addr));
    if (sent > 0) {
        reflex_signal(&udp->tx_channel, sent);
    }
    return sent;
}

/**
 * Receive UDP packet (check RX channel)
 * Returns number of bytes received, 0 if none available
 */
static inline int udp_recv(reflex_udp_channel_t* udp, void* buf, size_t max_len) {
    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);

    int received = recvfrom(udp->sock, buf, max_len, 0,
                            (struct sockaddr*)&src_addr, &addr_len);
    if (received > 0) {
        reflex_signal(&udp->rx_channel, received);
    }
    return received > 0 ? received : 0;
}

/**
 * Close UDP channel
 */
static inline void udp_channel_close(reflex_udp_channel_t* udp) {
    if (udp->sock >= 0) {
        close(udp->sock);
        udp->sock = -1;
    }
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_WIFI_H
