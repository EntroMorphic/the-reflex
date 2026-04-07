/**
 * reflex_mesh.h - IEEE 802.15.4 Mesh for Holographic Intelligence
 *
 * Broadcasts hidden states between nodes at ~100Hz.
 * The mesh IS the distributed brain.
 *
 * Protocol:
 *   - 16-byte hologram packets
 *   - Broadcast (no addressing, all nodes see all)
 *   - ~100Hz update rate (10ms period)
 *   - ~13kbps actual bandwidth used
 */

#ifndef REFLEX_MESH_H
#define REFLEX_MESH_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "reflex_hologram.h"

// ESP-IDF IEEE 802.15.4 support
#include "esp_ieee802154.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================

#define MESH_CHANNEL            15      // IEEE 802.15.4 channel (11-26)
#define MESH_PAN_ID             0x4846  // "HF" for Holographic
#define MESH_BROADCAST_ADDR     0xFFFF  // Broadcast address

#define MESH_TX_POWER           20      // dBm (max for C6)
#define MESH_BROADCAST_PERIOD_MS 10     // 100 Hz

// Frame structure
#define MESH_FRAME_TYPE         0x01    // Data frame
#define MESH_FRAME_VERSION      0x00    // IEEE 802.15.4-2006

// ============================================================
// Mesh State
// ============================================================

typedef void (*mesh_receive_callback_t)(const hologram_packet_t* pkt, int8_t rssi);

typedef struct {
    // Configuration
    uint8_t node_id;
    uint16_t pan_id;
    uint8_t channel;
    
    // State
    bool initialized;
    bool enabled;
    
    // Receive callback
    mesh_receive_callback_t on_receive;
    
    // Statistics
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t rx_errors;
    int8_t last_rssi;
    
} mesh_state_t;

// Global mesh state
static mesh_state_t g_mesh = {0};

// ============================================================
// IEEE 802.15.4 Frame Building
// ============================================================

/**
 * Build IEEE 802.15.4 frame with hologram payload
 * Returns frame length
 */
static inline int mesh_build_frame(
    uint8_t* frame,
    int max_len,
    const hologram_packet_t* payload
) {
    if (max_len < 32) return -1;
    
    int idx = 0;
    
    // Frame Control (2 bytes)
    // Bits 0-2: Frame type (001 = data)
    // Bit 3: Security enabled (0)
    // Bit 4: Frame pending (0)
    // Bit 5: ACK request (0 for broadcast)
    // Bit 6: PAN ID compression (1)
    // Bits 7-9: Reserved
    // Bits 10-11: Dest addr mode (10 = 16-bit)
    // Bits 12-13: Frame version (00)
    // Bits 14-15: Src addr mode (10 = 16-bit)
    uint16_t fc = 0x8841;  // Data, PAN compress, 16-bit addrs
    frame[idx++] = fc & 0xFF;
    frame[idx++] = (fc >> 8) & 0xFF;
    
    // Sequence number (1 byte)
    static uint8_t seq = 0;
    frame[idx++] = seq++;
    
    // Destination PAN ID (2 bytes)
    frame[idx++] = g_mesh.pan_id & 0xFF;
    frame[idx++] = (g_mesh.pan_id >> 8) & 0xFF;
    
    // Destination address (2 bytes) - broadcast
    frame[idx++] = MESH_BROADCAST_ADDR & 0xFF;
    frame[idx++] = (MESH_BROADCAST_ADDR >> 8) & 0xFF;
    
    // Source address (2 bytes)
    uint16_t src_addr = 0x1000 | g_mesh.node_id;
    frame[idx++] = src_addr & 0xFF;
    frame[idx++] = (src_addr >> 8) & 0xFF;
    
    // Payload (16 bytes)
    memcpy(&frame[idx], payload, sizeof(hologram_packet_t));
    idx += sizeof(hologram_packet_t);
    
    // FCS will be added by hardware
    
    return idx;
}

/**
 * Parse received frame, extract hologram payload
 * Returns true if valid hologram packet
 */
static inline bool mesh_parse_frame(
    const uint8_t* frame,
    int len,
    hologram_packet_t* payload,
    uint8_t* src_node_id
) {
    // Minimum frame: 2 (FC) + 1 (seq) + 2 (PAN) + 2 (dst) + 2 (src) + 16 (payload) = 25
    if (len < 25) return false;
    
    // Check frame type
    uint16_t fc = frame[0] | (frame[1] << 8);
    if ((fc & 0x07) != 0x01) return false;  // Not a data frame
    
    // Extract source address
    uint16_t src_addr = frame[7] | (frame[8] << 8);
    *src_node_id = src_addr & 0xFF;
    
    // Extract payload (starts at byte 9)
    memcpy(payload, &frame[9], sizeof(hologram_packet_t));
    
    // Verify checksum
    return hologram_verify_packet(payload);
}

// ============================================================
// ESP-IDF 802.15.4 Callbacks
// ============================================================

static void mesh_rx_done_callback(uint8_t* frame, esp_ieee802154_frame_info_t* frame_info) {
    if (!g_mesh.enabled || !g_mesh.on_receive) return;
    
    hologram_packet_t pkt;
    uint8_t src_node_id;
    
    // frame_info->length includes FCS (2 bytes)
    int payload_len = frame_info->length - 2;
    
    if (mesh_parse_frame(frame, payload_len, &pkt, &src_node_id)) {
        g_mesh.rx_count++;
        g_mesh.last_rssi = frame_info->rssi;
        g_mesh.on_receive(&pkt, frame_info->rssi);
    } else {
        g_mesh.rx_errors++;
    }
    
    // Return to receive mode
    esp_ieee802154_receive();
}

static void mesh_tx_done_callback(const uint8_t* frame, esp_ieee802154_frame_info_t* frame_info) {
    g_mesh.tx_count++;
    
    // Return to receive mode
    esp_ieee802154_receive();
}

// ============================================================
// Initialization
// ============================================================

/**
 * Initialize IEEE 802.15.4 mesh
 */
static inline esp_err_t mesh_init(
    uint8_t node_id,
    mesh_receive_callback_t on_receive
) {
    g_mesh.node_id = node_id;
    g_mesh.pan_id = MESH_PAN_ID;
    g_mesh.channel = MESH_CHANNEL;
    g_mesh.on_receive = on_receive;
    
    // Initialize IEEE 802.15.4
    esp_ieee802154_enable();
    esp_ieee802154_set_panid(g_mesh.pan_id);
    esp_ieee802154_set_short_address(0x1000 | node_id);
    esp_ieee802154_set_channel(g_mesh.channel);
    esp_ieee802154_set_txpower(MESH_TX_POWER);
    
    // Set coordinator (all nodes are equal in our mesh)
    esp_ieee802154_set_coordinator(false);
    
    // Enable promiscuous mode to receive all frames
    esp_ieee802154_set_promiscuous(true);
    
    // Register callbacks
    static esp_ieee802154_callbacks_t callbacks = {
        .rx_done = mesh_rx_done_callback,
        .tx_done = mesh_tx_done_callback,
    };
    esp_ieee802154_register_callbacks(&callbacks);
    
    g_mesh.initialized = true;
    
    return ESP_OK;
}

/**
 * Start mesh (begin receiving)
 */
static inline esp_err_t mesh_start(void) {
    if (!g_mesh.initialized) return ESP_ERR_INVALID_STATE;
    
    g_mesh.enabled = true;
    esp_ieee802154_receive();
    
    return ESP_OK;
}

/**
 * Stop mesh
 */
static inline esp_err_t mesh_stop(void) {
    g_mesh.enabled = false;
    esp_ieee802154_disable();
    return ESP_OK;
}

// ============================================================
// Transmission
// ============================================================

/**
 * Broadcast hologram packet
 */
static inline esp_err_t mesh_broadcast(const hologram_packet_t* pkt) {
    if (!g_mesh.enabled) return ESP_ERR_INVALID_STATE;
    
    static uint8_t frame[64];
    int len = mesh_build_frame(frame, sizeof(frame), pkt);
    if (len < 0) return ESP_ERR_INVALID_ARG;
    
    // Transmit
    esp_ieee802154_transmit(frame, false);  // false = no CCA
    
    return ESP_OK;
}

/**
 * Broadcast from hologram node
 */
static inline esp_err_t mesh_broadcast_node(hologram_node_t* node) {
    hologram_packet_t pkt;
    hologram_create_packet(node, &pkt);
    node->packets_sent++;
    return mesh_broadcast(&pkt);
}

// ============================================================
// Statistics
// ============================================================

typedef struct {
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t rx_errors;
    int8_t last_rssi;
    uint8_t channel;
    bool enabled;
} mesh_stats_t;

static inline mesh_stats_t mesh_get_stats(void) {
    mesh_stats_t stats;
    stats.tx_count = g_mesh.tx_count;
    stats.rx_count = g_mesh.rx_count;
    stats.rx_errors = g_mesh.rx_errors;
    stats.last_rssi = g_mesh.last_rssi;
    stats.channel = g_mesh.channel;
    stats.enabled = g_mesh.enabled;
    return stats;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_MESH_H
