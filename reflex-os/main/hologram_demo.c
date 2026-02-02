/**
 * hologram_demo.c - Holographic Intelligence Demo
 *
 * Each node IS the brain, not a part of it.
 * The interference pattern between nodes is the intelligence.
 *
 * Demo modes:
 *   1. Single node: Test CfC + entropy tracking
 *   2. Multi node: Full holographic mesh (requires 802.15.4)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "driver/gpio.h"

#include "reflex.h"
#include "reflex_gpio.h"
#include "reflex_hologram.h"

// Uncomment to enable mesh (requires 802.15.4 radio)
// #define HOLOGRAM_MESH_ENABLED

#ifdef HOLOGRAM_MESH_ENABLED
#include "reflex_mesh.h"
#endif

static const char* TAG = "hologram";

// ============================================================
// Configuration
// ============================================================

#define PIN_LED             8       // Main LED
#define PIN_BUTTON          9       // User button (active low)
#define NODE_ID             1       // Change for each node: 1, 2, 3

#define TICK_RATE_HZ        1000    // Hologram tick rate
#define BROADCAST_RATE_HZ   100     // Mesh broadcast rate
#define DEMO_DURATION_SEC   30      // Demo duration

// ============================================================
// Global State
// ============================================================

static hologram_node_t g_node;
static volatile bool g_running = true;
static uint32_t g_button_presses = 0;

// ============================================================
// Simulated Neighbors (for single-node testing)
// ============================================================

#ifndef HOLOGRAM_MESH_ENABLED

// Simulate neighbor hidden states for testing
static void simulate_neighbors(hologram_node_t* node, uint64_t local_input) {
    // Simulate 2 neighbors with slightly different perspectives
    
    // Neighbor 1: Sees similar but delayed/shifted
    static uint64_t neighbor1_hidden = 0;
    neighbor1_hidden = (neighbor1_hidden >> 1) | ((local_input & 0x01) << 63);
    
    // Neighbor 2: Sees complement of some bits (different perspective)
    static uint64_t neighbor2_hidden = 0;
    neighbor2_hidden = local_input ^ 0xAAAAAAAAAAAAAAAAULL;
    
    // Inject simulated neighbors
    node->neighbors[0].hidden = neighbor1_hidden;
    node->neighbors[0].node_id = 2;
    node->neighbors[0].confidence = 200;
    node->neighbors[0].last_seen_tick = node->tick_count;
    node->neighbors[0].active = true;
    
    node->neighbors[1].hidden = neighbor2_hidden;
    node->neighbors[1].node_id = 3;
    node->neighbors[1].confidence = 180;
    node->neighbors[1].last_seen_tick = node->tick_count;
    node->neighbors[1].active = true;
    
    node->neighbor_count = 2;
}

#endif

// ============================================================
// Mesh Callbacks
// ============================================================

#ifdef HOLOGRAM_MESH_ENABLED

static void on_mesh_receive(const hologram_packet_t* pkt, int8_t rssi) {
    hologram_receive(&g_node, pkt);
    ESP_LOGD(TAG, "RX from node %d, rssi=%d, conf=%d", 
             pkt->node_id, rssi, pkt->confidence);
}

#endif

// ============================================================
// Demo Functions
// ============================================================

static void demo_single_tick(void) {
    // Read button (active low)
    static bool last_button = true;
    bool button = gpio_read(PIN_BUTTON);
    
    if (!button && last_button) {
        g_button_presses++;
        ESP_LOGI(TAG, "Button pressed! Total: %lu", (unsigned long)g_button_presses);
    }
    last_button = button;
    
    // Create input from button state
    uint64_t local_input = 0;
    if (!button) {
        local_input |= 0xFF;  // Button pressed = high input
    }
    local_input |= ((uint64_t)g_button_presses << 8);  // Encode press count
    
    #ifndef HOLOGRAM_MESH_ENABLED
    // Simulate neighbors for single-node testing
    simulate_neighbors(&g_node, local_input);
    #endif
    
    // Run hologram tick
    uint64_t output = hologram_tick(&g_node, local_input);
    
    // LED reflects confidence and output
    // Blink rate proportional to confidence
    bool led_on = false;
    if (g_node.confidence > CONFIDENCE_HIGH) {
        led_on = (output & 0x01) != 0;  // Solid based on output
    } else if (g_node.confidence > CONFIDENCE_LOW) {
        led_on = ((g_node.tick_count / 100) % 2) == 0;  // Slow blink
    } else {
        led_on = ((g_node.tick_count / 50) % 2) == 0;   // Fast blink
    }
    
    gpio_write(PIN_LED, led_on ? 0 : 1);  // LED is active low
}

static void print_status(void) {
    hologram_stats_t stats = hologram_get_stats(&g_node);
    
    char entropy_map[65];
    hologram_print_entropy_map(&g_node, entropy_map, sizeof(entropy_map));
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  HOLOGRAM NODE %d STATUS (tick %lu)\n", NODE_ID, (unsigned long)stats.tick_count);
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("  Neighbors:       %d active\n", stats.neighbor_count);
    printf("  Confidence:      %.1f%%\n", stats.confidence * 100.0f);
    printf("  Crystallized:    %d bits\n", stats.crystallized_bits);
    printf("  Potential:       %d bits\n", stats.potential_bits);
    printf("\n");
    printf("  Hidden state:    0x%016llx\n", (unsigned long long)g_node.cfc.hidden);
    printf("  Constructive:    0x%016llx\n", (unsigned long long)g_node.constructive);
    printf("  Destructive:     0x%016llx\n", (unsigned long long)g_node.destructive);
    printf("\n");
    printf("  Agreements:      %lu\n", (unsigned long)stats.agreements);
    printf("  Disagreements:   %lu\n", (unsigned long)stats.disagreements);
    printf("  Crystallizations: %lu\n", (unsigned long)stats.crystallizations);
    printf("\n");
    printf("  Entropy map (# = crystallized, . = potential, - = neutral):\n");
    printf("    %s\n", entropy_map);
    printf("\n");
    
    #ifdef HOLOGRAM_MESH_ENABLED
    mesh_stats_t mstats = mesh_get_stats();
    printf("  Mesh TX:         %lu\n", (unsigned long)mstats.tx_count);
    printf("  Mesh RX:         %lu\n", (unsigned long)mstats.rx_count);
    printf("  Mesh errors:     %lu\n", (unsigned long)mstats.rx_errors);
    printf("  Last RSSI:       %d dBm\n", mstats.last_rssi);
    printf("\n");
    #endif
    
    printf("═══════════════════════════════════════════════════════════════\n");
    fflush(stdout);
}

static void demo_benchmark(void) {
    printf("\n  Benchmarking hologram_tick...\n");
    
    uint32_t min_cycles = UINT32_MAX;
    uint32_t max_cycles = 0;
    uint64_t sum_cycles = 0;
    
    #ifndef HOLOGRAM_MESH_ENABLED
    // Pre-populate neighbors for benchmark
    simulate_neighbors(&g_node, 0x12345678);
    #endif
    
    for (int i = 0; i < 10000; i++) {
        uint64_t input = (uint64_t)i | 0xAA00000000000000ULL;
        
        uint32_t t0 = reflex_cycles();
        uint64_t output = hologram_tick(&g_node, input);
        uint32_t cycles = reflex_cycles() - t0;
        (void)output;
        
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
        sum_cycles += cycles;
    }
    
    uint32_t avg_cycles = (uint32_t)(sum_cycles / 10000);
    
    printf("  Results (10,000 iterations):\n");
    printf("    Min: %lu cycles = %lu ns\n", 
           (unsigned long)min_cycles,
           (unsigned long)reflex_cycles_to_ns(min_cycles));
    printf("    Avg: %lu cycles = %lu ns\n",
           (unsigned long)avg_cycles,
           (unsigned long)reflex_cycles_to_ns(avg_cycles));
    printf("    Max: %lu cycles = %lu ns\n",
           (unsigned long)max_cycles,
           (unsigned long)reflex_cycles_to_ns(max_cycles));
    
    uint32_t khz = 1000000 / reflex_cycles_to_ns(avg_cycles);
    printf("    Throughput: ~%lu kHz\n\n", (unsigned long)khz);
}

static void demo_crystallization(void) {
    printf("\n  Testing crystallization (learning from repeated patterns)...\n");
    
    // Reset node
    hologram_init(&g_node, NODE_ID, esp_random());
    
    uint32_t initial_crystallized = __builtin_popcountll(g_node.crystallized);
    
    // Repeatedly send same pattern
    printf("  Sending pattern 0xAAAA 1000 times...\n");
    for (int i = 0; i < 1000; i++) {
        #ifndef HOLOGRAM_MESH_ENABLED
        // Simulate neighbors agreeing on this pattern
        g_node.neighbors[0].hidden = 0xAAAAAAAAAAAAAAAAULL;
        g_node.neighbors[0].active = true;
        g_node.neighbors[0].last_seen_tick = g_node.tick_count;
        g_node.neighbors[1].hidden = 0xAAAAAAAAAAAAAAAAULL;
        g_node.neighbors[1].active = true;
        g_node.neighbors[1].last_seen_tick = g_node.tick_count;
        g_node.neighbor_count = 2;
        #endif
        
        hologram_tick(&g_node, 0xAAAAAAAAAAAAAAAAULL);
    }
    
    uint32_t final_crystallized = __builtin_popcountll(g_node.crystallized);
    
    printf("  Results:\n");
    printf("    Initial crystallized bits: %lu\n", (unsigned long)initial_crystallized);
    printf("    Final crystallized bits:   %lu\n", (unsigned long)final_crystallized);
    printf("    New crystallizations:      %lu\n", (unsigned long)(final_crystallized - initial_crystallized));
    printf("    Total crystallization events: %lu\n\n", (unsigned long)g_node.crystallization_count);
}

// ============================================================
// Main Task
// ============================================================

static void hologram_task(void* arg) {
    uint32_t tick = 0;
    uint32_t last_broadcast = 0;
    uint32_t last_status = 0;
    
    while (g_running) {
        // Run hologram tick
        demo_single_tick();
        tick++;
        
        #ifdef HOLOGRAM_MESH_ENABLED
        // Broadcast at lower rate
        if ((tick - last_broadcast) >= (TICK_RATE_HZ / BROADCAST_RATE_HZ)) {
            mesh_broadcast_node(&g_node);
            last_broadcast = tick;
        }
        #endif
        
        // Print status every 5 seconds
        if ((tick - last_status) >= (TICK_RATE_HZ * 5)) {
            print_status();
            last_status = tick;
        }
        
        // Maintain tick rate
        vTaskDelay(pdMS_TO_TICKS(1000 / TICK_RATE_HZ));
    }
    
    vTaskDelete(NULL);
}

// ============================================================
// Entry Point
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("          HOLOGRAPHIC INTELLIGENCE DEMO                         \n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("  Node ID: %d\n", NODE_ID);
    #ifdef HOLOGRAM_MESH_ENABLED
    printf("  Mode:    MESH ENABLED (IEEE 802.15.4)\n");
    #else
    printf("  Mode:    SINGLE NODE (simulated neighbors)\n");
    #endif
    printf("\n");
    printf("  Each node IS the brain, not a part of it.\n");
    printf("  The interference pattern is the intelligence.\n");
    printf("\n");
    fflush(stdout);
    
    // Initialize GPIO
    gpio_set_output(PIN_LED);
    gpio_write(PIN_LED, 1);  // LED off
    
    // Configure button as input with pull-up
    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << PIN_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);
    
    // Initialize hologram node
    printf("  Initializing hologram node...\n");
    hologram_init(&g_node, NODE_ID, esp_random());
    printf("    CfC size: %lu bytes\n", (unsigned long)cfc_turbo_memory_size());
    printf("    Node state: %lu bytes\n", (unsigned long)sizeof(hologram_node_t));
    printf("\n");
    fflush(stdout);
    
    #ifdef HOLOGRAM_MESH_ENABLED
    // Initialize mesh
    printf("  Initializing IEEE 802.15.4 mesh...\n");
    esp_err_t err = mesh_init(NODE_ID, on_mesh_receive);
    if (err != ESP_OK) {
        printf("    FAILED: %s\n", esp_err_to_name(err));
    } else {
        mesh_start();
        printf("    Channel: %d\n", MESH_CHANNEL);
        printf("    PAN ID:  0x%04X\n", MESH_PAN_ID);
        printf("    OK\n");
    }
    printf("\n");
    fflush(stdout);
    #endif
    
    // Run benchmark
    printf("════════════════════════════════════════════════════════════════\n");
    printf("                      BENCHMARK                                 \n");
    printf("════════════════════════════════════════════════════════════════\n");
    demo_benchmark();
    fflush(stdout);
    
    // Test crystallization
    printf("════════════════════════════════════════════════════════════════\n");
    printf("                   CRYSTALLIZATION TEST                         \n");
    printf("════════════════════════════════════════════════════════════════\n");
    demo_crystallization();
    fflush(stdout);
    
    // Re-initialize for live demo
    hologram_init(&g_node, NODE_ID, esp_random());
    
    // Start main loop
    printf("════════════════════════════════════════════════════════════════\n");
    printf("                      LIVE DEMO                                 \n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("  Press button to generate input.\n");
    printf("  Watch crystallization emerge from repeated patterns.\n");
    printf("  LED blink rate = confidence level.\n");
    printf("\n");
    fflush(stdout);
    
    // Create task
    xTaskCreate(hologram_task, "hologram", 4096, NULL, 5, NULL);
    
    // Main loop just monitors
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
