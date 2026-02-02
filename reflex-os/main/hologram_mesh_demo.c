/**
 * hologram_mesh_demo.c - Holographic Intelligence: 3-Node Mesh Demo
 *
 * Each node IS the brain, not a part of it.
 * The interference pattern between nodes is the intelligence.
 *
 * Flash this to all 3 C6 devices, changing NODE_ID for each:
 *   Node 1: /dev/ttyACM0
 *   Node 2: /dev/ttyACM1
 *   Node 3: /dev/ttyACM2
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_ieee802154.h"
#include "esp_attr.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "reflex.h"
#include "reflex_gpio.h"
#include "reflex_hologram.h"

static const char* TAG = "hologram";

// ════════════════════════════════════════════════════════════════════════════
// CONFIGURATION - CHANGE NODE_ID FOR EACH DEVICE!
// ════════════════════════════════════════════════════════════════════════════

#ifndef NODE_ID
#define NODE_ID             3       // *** CHANGE THIS: 1, 2, or 3 ***
#endif

#define PIN_LED             8       // Onboard LED
#define RADIO_CHANNEL       15      // IEEE 802.15.4 channel
#define RADIO_PAN_ID        0x4846  // "HF" = Holographic

// ════════════════════════════════════════════════════════════════════════════
// Global State
// ════════════════════════════════════════════════════════════════════════════

static hologram_node_t g_node;
static QueueHandle_t g_rx_queue;
static volatile uint32_t g_tx_count = 0;
static volatile uint32_t g_rx_count = 0;
static volatile uint32_t g_rx_errors = 0;
static volatile int8_t g_last_rssi = 0;

// Frame buffer for transmission
static uint8_t g_tx_frame[32] __attribute__((aligned(4)));

// ════════════════════════════════════════════════════════════════════════════
// IEEE 802.15.4 Callbacks (called from ISR context!)
// ════════════════════════════════════════════════════════════════════════════

// Static buffer for received frames - MUST be static to avoid stack in ISR
static uint8_t s_rx_frame[128];
static esp_ieee802154_frame_info_t s_rx_info;
static volatile bool s_rx_pending = false;
static volatile bool s_need_rx_restart = false;
static volatile bool s_tx_done = false;

void IRAM_ATTR esp_ieee802154_receive_done(uint8_t *frame, esp_ieee802154_frame_info_t *frame_info) {
    // Minimal ISR: just copy data and set flags
    uint8_t len = frame[0];
    if (len > 0 && len < 127 && !s_rx_pending) {
        memcpy(s_rx_frame, frame, len + 1);
        s_rx_info = *frame_info;
        s_rx_pending = true;
    }
    
    // Tell driver we're done with the frame
    esp_ieee802154_receive_handle_done(frame);
    
    // Signal task to restart receive (don't call esp_ieee802154_receive from ISR!)
    s_need_rx_restart = true;
}

void IRAM_ATTR esp_ieee802154_receive_sfd_done(void) {
}

void IRAM_ATTR esp_ieee802154_transmit_done(const uint8_t *frame, const uint8_t *ack, esp_ieee802154_frame_info_t *ack_frame_info) {
    g_tx_count++;
    s_tx_done = true;
}

void IRAM_ATTR esp_ieee802154_transmit_failed(const uint8_t *frame, esp_ieee802154_tx_error_t error) {
    s_tx_done = true;
}

void IRAM_ATTR esp_ieee802154_transmit_sfd_done(uint8_t *frame) {
}

void IRAM_ATTR esp_ieee802154_cca_done(bool channel_free) {
}

void IRAM_ATTR esp_ieee802154_energy_detect_done(int8_t power) {
}

// ════════════════════════════════════════════════════════════════════════════
// Radio Functions
// ════════════════════════════════════════════════════════════════════════════

static void radio_init(void) {
    ESP_LOGI(TAG, "Initializing IEEE 802.15.4 radio...");
    
    ESP_ERROR_CHECK(esp_ieee802154_enable());
    ESP_ERROR_CHECK(esp_ieee802154_set_channel(RADIO_CHANNEL));
    ESP_ERROR_CHECK(esp_ieee802154_set_panid(RADIO_PAN_ID));
    ESP_ERROR_CHECK(esp_ieee802154_set_short_address(0x1000 | NODE_ID));
    ESP_ERROR_CHECK(esp_ieee802154_set_txpower(20));  // Max power
    ESP_ERROR_CHECK(esp_ieee802154_set_promiscuous(true));  // Receive all
    
    // Start receiving
    ESP_ERROR_CHECK(esp_ieee802154_receive());
    
    ESP_LOGI(TAG, "Radio initialized: ch=%d, pan=0x%04X, addr=0x%04X",
             RADIO_CHANNEL, RADIO_PAN_ID, 0x1000 | NODE_ID);
}

static void radio_broadcast(const hologram_packet_t* pkt) {
    // Build IEEE 802.15.4 frame
    int idx = 0;
    
    // Length (will be filled at end)
    g_tx_frame[idx++] = 0;
    
    // Frame Control: Data frame, PAN compression, 16-bit addresses
    g_tx_frame[idx++] = 0x41;  // Data frame, no security, no pending, no ack, PAN compress
    g_tx_frame[idx++] = 0x88;  // 16-bit dst, 16-bit src
    
    // Sequence number
    static uint8_t seq = 0;
    g_tx_frame[idx++] = seq++;
    
    // Destination PAN ID
    g_tx_frame[idx++] = RADIO_PAN_ID & 0xFF;
    g_tx_frame[idx++] = (RADIO_PAN_ID >> 8) & 0xFF;
    
    // Destination address (broadcast)
    g_tx_frame[idx++] = 0xFF;
    g_tx_frame[idx++] = 0xFF;
    
    // Source address
    g_tx_frame[idx++] = NODE_ID;
    g_tx_frame[idx++] = 0x10;
    
    // Payload (hologram packet)
    memcpy(&g_tx_frame[idx], pkt, sizeof(hologram_packet_t));
    idx += sizeof(hologram_packet_t);
    
    // Set length (excluding length byte itself, including FCS which HW adds)
    g_tx_frame[0] = idx - 1 + 2;  // +2 for FCS
    
    // Transmit (no CCA for broadcast)
    esp_ieee802154_transmit(g_tx_frame, false);
}

// ════════════════════════════════════════════════════════════════════════════
// Main Demo
// ════════════════════════════════════════════════════════════════════════════

static void print_status(void) {
    hologram_stats_t stats = hologram_get_stats(&g_node);
    
    char entropy_map[65];
    hologram_print_entropy_map(&g_node, entropy_map, sizeof(entropy_map));
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  NODE %d STATUS (tick %lu)\n", NODE_ID, (unsigned long)stats.tick_count);
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  Neighbors:     %d active\n", stats.neighbor_count);
    printf("  Confidence:    %.1f%%\n", stats.confidence * 100.0f);
    printf("  Crystallized:  %d bits\n", stats.crystallized_bits);
    printf("\n");
    printf("  Hidden:        0x%016llx\n", (unsigned long long)g_node.cfc.hidden);
    printf("  Constructive:  0x%016llx\n", (unsigned long long)g_node.constructive);
    printf("  Destructive:   0x%016llx\n", (unsigned long long)g_node.destructive);
    printf("\n");
    printf("  Radio TX: %lu  RX: %lu  Errors: %lu  RSSI: %d dBm\n",
           (unsigned long)g_tx_count, (unsigned long)g_rx_count,
           (unsigned long)g_rx_errors, g_last_rssi);
    printf("\n");
    printf("  Entropy: %s\n", entropy_map);
    printf("═══════════════════════════════════════════════════════════════\n\n");
    fflush(stdout);
}

// Process received frame (called from task context, not ISR)
static void process_rx_frame(void) {
    uint8_t len = s_rx_frame[0];
    if (len < 25) {
        g_rx_errors++;
        return;
    }
    
    // Extract source node ID from source address (byte 8, low nibble)
    uint8_t src_node = s_rx_frame[8] & 0x0F;
    
    // Ignore our own transmissions
    if (src_node == NODE_ID) {
        return;
    }
    
    // Extract hologram packet (starts at byte 10)
    hologram_packet_t pkt;
    memcpy(&pkt, &s_rx_frame[10], sizeof(hologram_packet_t));
    
    // Verify and process
    if (hologram_verify_packet(&pkt)) {
        g_rx_count++;
        g_last_rssi = s_rx_info.rssi;
        hologram_receive(&g_node, &pkt);
    } else {
        g_rx_errors++;
    }
}

static void hologram_task(void* arg) {
    uint32_t tick = 0;
    uint32_t last_broadcast = 0;
    uint32_t last_status = 0;
    
    // Initialize radio from task context (safer stack-wise)
    vTaskDelay(pdMS_TO_TICKS(100));  // Let system settle
    radio_init();
    
    while (1) {
        tick++;
        
        // Restart receive if needed (from task context, not ISR)
        if (s_need_rx_restart) {
            s_need_rx_restart = false;
            esp_ieee802154_receive();
        }
        
        // Restart receive after TX completes
        if (s_tx_done) {
            s_tx_done = false;
            esp_ieee802154_receive();
        }
        
        // Process any received frames (from ISR flag)
        if (s_rx_pending) {
            process_rx_frame();
            s_rx_pending = false;
        }
        
        // Create input based on node ID (each node has different "perspective")
        uint64_t local_input = 0;
        local_input |= (uint64_t)NODE_ID;
        local_input |= ((uint64_t)(tick & 0xFF) << 8);
        local_input |= ((uint64_t)(esp_random() & 0xFF) << 16);
        
        // Run hologram tick
        uint64_t output = hologram_tick(&g_node, local_input);
        
        // LED: ON if we have neighbors and good confidence
        bool led_on = (g_node.neighbor_count > 0) && (g_node.confidence > 0.3f);
        
        // Blink pattern based on confidence
        if (g_node.confidence > 0.7f) {
            // High confidence: solid (based on output bit 0)
            led_on = (output & 0x01) != 0;
        } else if (g_node.confidence > 0.3f) {
            // Medium: slow blink
            led_on = ((tick / 500) % 2) == 0;
        } else if (g_node.neighbor_count > 0) {
            // Low confidence but have neighbors: fast blink
            led_on = ((tick / 100) % 2) == 0;
        } else {
            // No neighbors: very fast blink
            led_on = ((tick / 50) % 2) == 0;
        }
        
        gpio_write(PIN_LED, led_on ? 0 : 1);
        
        // Broadcast every 10ms (100 Hz)
        if ((tick - last_broadcast) >= 10) {
            hologram_packet_t tx_pkt;
            hologram_create_packet(&g_node, &tx_pkt);
            radio_broadcast(&tx_pkt);
            last_broadcast = tick;
        }
        
        // Status every 5 seconds
        if ((tick - last_status) >= 5000) {
            print_status();
            last_status = tick;
        }
        
        // 1ms tick
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Entry Point
// ════════════════════════════════════════════════════════════════════════════

void app_main(void) {
    // Initialize NVS (required for PHY calibration)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    printf("\n\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("       HOLOGRAPHIC INTELLIGENCE - 3-NODE MESH DEMO              \n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("  ╔═══════════════════════════════════════════════════════════╗\n");
    printf("  ║                      NODE %d                               ║\n", NODE_ID);
    printf("  ╚═══════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Each node IS the brain, not a part of it.\n");
    printf("  The interference pattern is the intelligence.\n");
    printf("\n");
    fflush(stdout);
    
    // Initialize GPIO
    gpio_set_output(PIN_LED);
    gpio_write(PIN_LED, 1);
    
    // Create RX queue
    g_rx_queue = xQueueCreate(16, sizeof(hologram_packet_t));
    
    // Initialize hologram node
    printf("  Initializing hologram engine...\n");
    hologram_init(&g_node, NODE_ID, esp_random() ^ (NODE_ID * 0x12345678));
    printf("    Node state: %lu bytes\n", (unsigned long)sizeof(hologram_node_t));
    
    // Radio will be initialized from task context
    
    printf("\n");
    printf("  Waiting for other nodes...\n");
    printf("  LED patterns:\n");
    printf("    Very fast blink = No neighbors\n");
    printf("    Fast blink = Low confidence\n");
    printf("    Slow blink = Medium confidence\n");
    printf("    Solid = High confidence\n");
    printf("\n");
    fflush(stdout);
    
    // Start main task with larger stack
    xTaskCreate(hologram_task, "hologram", 8192, NULL, 5, NULL);
}
