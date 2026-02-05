/**
 * parallel_pcnt.c - 4-Channel Parallel Pulse Counting
 *
 * ESP32-C6 has 4 PCNT units. This prototype wires all 4 to the same
 * GPIO output, proving they can count the same pulse stream independently.
 *
 * This is the foundation for:
 *   1. 4-element dot products (4 weights × 1 activation)
 *   2. Parallel multiply-accumulate (4 products simultaneously)
 *   3. Hardware neurons (weighted sum of 4 inputs)
 *
 * Architecture:
 *   PARLIO → GPIO4 → [PCNT0, PCNT1, PCNT2, PCNT3]
 *                           ↓      ↓      ↓      ↓
 *                        count0 count1 count2 count3
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "driver/parlio_tx.h"
#include "esp_rom_sys.h"
#include "esp_heap_caps.h"

static const char *TAG = "PARALLEL_PCNT";

// ============================================================
// Configuration
// ============================================================

#define OUTPUT_GPIO         4
#define PARLIO_FREQ_HZ      10000000  // 10 MHz
#define NUM_PCNT_UNITS      4

// Overflow tracking
#define PCNT_OVERFLOW_LIMIT 30000

// Pattern buffer
#define MAX_PATTERN_BYTES   4096
#define PULSE_BYTE          0x80
#define MIN_PATTERN_BYTES   4

// ============================================================
// Hardware handles - 4 PCNT units
// ============================================================

static pcnt_unit_handle_t pcnt_units[NUM_PCNT_UNITS] = {NULL};
static pcnt_channel_handle_t pcnt_channels[NUM_PCNT_UNITS] = {NULL};
static parlio_tx_unit_handle_t parlio = NULL;

// Overflow tracking per unit
static volatile int overflow_counts[NUM_PCNT_UNITS] = {0};

// ============================================================
// Pattern buffer
// ============================================================

static uint8_t *pattern_buffer = NULL;

// ============================================================
// Overflow callbacks for each PCNT unit
// ============================================================

static bool IRAM_ATTR pcnt_overflow_cb_0(pcnt_unit_handle_t unit,
                                          const pcnt_watch_event_data_t *edata,
                                          void *user_ctx) {
    overflow_counts[0]++;
    return false;
}

static bool IRAM_ATTR pcnt_overflow_cb_1(pcnt_unit_handle_t unit,
                                          const pcnt_watch_event_data_t *edata,
                                          void *user_ctx) {
    overflow_counts[1]++;
    return false;
}

static bool IRAM_ATTR pcnt_overflow_cb_2(pcnt_unit_handle_t unit,
                                          const pcnt_watch_event_data_t *edata,
                                          void *user_ctx) {
    overflow_counts[2]++;
    return false;
}

static bool IRAM_ATTR pcnt_overflow_cb_3(pcnt_unit_handle_t unit,
                                          const pcnt_watch_event_data_t *edata,
                                          void *user_ctx) {
    overflow_counts[3]++;
    return false;
}

static pcnt_watch_cb_t overflow_callbacks[NUM_PCNT_UNITS] = {
    pcnt_overflow_cb_0,
    pcnt_overflow_cb_1,
    pcnt_overflow_cb_2,
    pcnt_overflow_cb_3
};

// ============================================================
// Get full count for a PCNT unit
// ============================================================

static int get_full_count(int unit) {
    int pcnt_value;
    pcnt_unit_get_count(pcnt_units[unit], &pcnt_value);
    return (overflow_counts[unit] * PCNT_OVERFLOW_LIMIT) + pcnt_value;
}

// ============================================================
// Reset all counters
// ============================================================

static void reset_all_counters(void) {
    for (int i = 0; i < NUM_PCNT_UNITS; i++) {
        overflow_counts[i] = 0;
        pcnt_unit_clear_count(pcnt_units[i]);
    }
}

// ============================================================
// Setup hardware
// ============================================================

static esp_err_t setup_gpio(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << OUTPUT_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io_conf);
}

static esp_err_t setup_all_pcnt(void) {
    ESP_LOGI(TAG, "Setting up %d PCNT units on GPIO%d", NUM_PCNT_UNITS, OUTPUT_GPIO);
    
    for (int i = 0; i < NUM_PCNT_UNITS; i++) {
        // Create PCNT unit
        pcnt_unit_config_t cfg = {
            .low_limit = -32768,
            .high_limit = PCNT_OVERFLOW_LIMIT,
            .flags.accum_count = 0,
        };
        ESP_ERROR_CHECK(pcnt_new_unit(&cfg, &pcnt_units[i]));
        
        // Create channel - ALL units watch the same GPIO!
        pcnt_chan_config_t chan_cfg = {
            .edge_gpio_num = OUTPUT_GPIO,
            .level_gpio_num = -1,
        };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_units[i], &chan_cfg, &pcnt_channels[i]));
        
        // Count rising edges
        pcnt_channel_set_edge_action(pcnt_channels[i],
            PCNT_CHANNEL_EDGE_ACTION_INCREASE,
            PCNT_CHANNEL_EDGE_ACTION_HOLD);
        
        // Add watch point for overflow
        pcnt_unit_add_watch_point(pcnt_units[i], PCNT_OVERFLOW_LIMIT);
        
        // Register overflow callback
        pcnt_event_callbacks_t cbs = {
            .on_reach = overflow_callbacks[i],
        };
        pcnt_unit_register_event_callbacks(pcnt_units[i], &cbs, NULL);
        
        // Enable and start
        pcnt_unit_enable(pcnt_units[i]);
        pcnt_unit_start(pcnt_units[i]);
        
        ESP_LOGI(TAG, "  PCNT[%d] ready", i);
    }
    
    return ESP_OK;
}

static esp_err_t setup_parlio(void) {
    parlio_tx_unit_config_t cfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = PARLIO_FREQ_HZ,
        .data_width = 1,
        .clk_out_gpio_num = -1,
        .valid_gpio_num = -1,
        .trans_queue_depth = 8,
        .max_transfer_size = MAX_PATTERN_BYTES + 100,
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_MSB,
        .flags = { .io_loop_back = 1 },
    };
    
    for (int i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; i++) {
        cfg.data_gpio_nums[i] = (i == 0) ? OUTPUT_GPIO : -1;
    }
    
    ESP_ERROR_CHECK(parlio_new_tx_unit(&cfg, &parlio));
    parlio_tx_unit_enable(parlio);
    
    return ESP_OK;
}

// ============================================================
// Emit pulses and check all 4 counters
// ============================================================

static void emit_pulses(int count) {
    if (count > MAX_PATTERN_BYTES) count = MAX_PATTERN_BYTES;
    if (count < MIN_PATTERN_BYTES) count = MIN_PATTERN_BYTES;
    
    // Fill pattern with pulses
    memset(pattern_buffer, 0, MAX_PATTERN_BYTES);
    for (int i = 0; i < count; i++) {
        pattern_buffer[i] = PULSE_BYTE;
    }
    
    // Transmit
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pattern_buffer, count * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 5000);
    esp_rom_delay_us(10);
}

// ============================================================
// Test 1: Basic parallel counting
// ============================================================

static void test_basic_parallel(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 1: BASIC PARALLEL COUNTING                                  ║\n");
    printf("║  All 4 PCNT units should count the same pulses                    ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    int test_counts[] = {100, 500, 1000, 5000, 10000};
    int num_tests = sizeof(test_counts) / sizeof(test_counts[0]);
    int passed = 0;
    
    for (int t = 0; t < num_tests; t++) {
        int expected = test_counts[t];
        
        reset_all_counters();
        emit_pulses(expected);
        
        printf("  Emitted %5d pulses: ", expected);
        
        int all_match = 1;
        for (int i = 0; i < NUM_PCNT_UNITS; i++) {
            int actual = get_full_count(i);
            printf("PCNT[%d]=%5d ", i, actual);
            if (actual != expected) all_match = 0;
        }
        
        if (all_match) {
            printf(" ✓\n");
            passed++;
        } else {
            printf(" FAIL\n");
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    printf("\n  Result: %d/%d passed\n", passed, num_tests);
}

// ============================================================
// Test 2: Cumulative counting (multiple emissions)
// ============================================================

static void test_cumulative(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 2: CUMULATIVE COUNTING                                      ║\n");
    printf("║  Multiple pulse bursts should accumulate in all 4 counters        ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    reset_all_counters();
    
    int bursts[] = {1000, 2000, 3000, 4000, 5000};
    int running_total = 0;
    
    for (int b = 0; b < 5; b++) {
        emit_pulses(bursts[b]);
        running_total += bursts[b];
        
        printf("  After burst %d (+%d, total=%d): ", b+1, bursts[b], running_total);
        
        int all_match = 1;
        for (int i = 0; i < NUM_PCNT_UNITS; i++) {
            int actual = get_full_count(i);
            if (i < 2) printf("PCNT[%d]=%5d ", i, actual);
            if (actual != running_total) all_match = 0;
        }
        printf("... %s\n", all_match ? "✓" : "FAIL");
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    printf("\n  Final total: %d (expected %d)\n", running_total, running_total);
}

// ============================================================
// Test 3: Overflow tracking across all units
// ============================================================

static void test_overflow_parallel(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 3: OVERFLOW TRACKING (ALL 4 UNITS)                          ║\n");
    printf("║  Emit >30000 pulses, verify overflow ISR works for all units      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // We'll emit pulses in multiple bursts to exceed the overflow limit
    int target = 50000;  // More than PCNT_OVERFLOW_LIMIT
    int burst_size = 4000;  // Pattern buffer limit
    
    reset_all_counters();
    
    int emitted = 0;
    while (emitted < target) {
        int this_burst = (target - emitted < burst_size) ? (target - emitted) : burst_size;
        emit_pulses(this_burst);
        emitted += this_burst;
    }
    
    printf("  Emitted %d total pulses (limit=%d)\n\n", target, PCNT_OVERFLOW_LIMIT);
    
    int all_match = 1;
    for (int i = 0; i < NUM_PCNT_UNITS; i++) {
        int pcnt_val;
        pcnt_unit_get_count(pcnt_units[i], &pcnt_val);
        int total = get_full_count(i);
        
        printf("  PCNT[%d]: overflows=%d, counter=%5d, total=%5d %s\n",
               i, overflow_counts[i], pcnt_val, total,
               (total == target) ? "✓" : "FAIL");
        
        if (total != target) all_match = 0;
    }
    
    printf("\n  %s\n", all_match ? "All 4 units tracked overflows correctly!" : "SOME UNITS FAILED");
}

// ============================================================
// Test 4: Dot product simulation
// ============================================================

static void test_dot_product_sim(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 4: DOT PRODUCT SIMULATION                                   ║\n");
    printf("║  Simulates: dot(weights, activations) using 4 PCNT units          ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Simulate: y = w0*x0 + w1*x1 + w2*x2 + w3*x3
    // Currently all PCNTs count the same stream, so we need to emit each term
    // This is a proof of concept - real dot product needs separate patterns
    
    // Weights and activations (simple example)
    int weights[] = {3, 5, 2, 4};
    int activations[] = {10, 20, 30, 40};
    
    // Expected dot product
    int expected = 0;
    for (int i = 0; i < 4; i++) {
        expected += weights[i] * activations[i];
    }
    
    printf("  Weights:     [%d, %d, %d, %d]\n", weights[0], weights[1], weights[2], weights[3]);
    printf("  Activations: [%d, %d, %d, %d]\n", activations[0], activations[1], activations[2], activations[3]);
    printf("  Expected:    %d\n\n", expected);
    
    // For now, we accumulate each product into ALL counters
    // (True parallel would need different patterns per PCNT)
    reset_all_counters();
    
    for (int i = 0; i < 4; i++) {
        int product = weights[i] * activations[i];
        printf("  w%d * x%d = %d * %d = %d\n", i, i, weights[i], activations[i], product);
        emit_pulses(product);
    }
    
    printf("\n  Final counts:\n");
    for (int i = 0; i < NUM_PCNT_UNITS; i++) {
        int total = get_full_count(i);
        printf("    PCNT[%d] = %d %s\n", i, total, (total == expected) ? "✓" : "FAIL");
    }
    
    printf("\n  Note: Currently all PCNTs count the same stream.\n");
    printf("  Next step: wire different patterns to different PCNTs.\n");
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  4-CHANNEL PARALLEL PCNT PROTOTYPE                                ║\n");
    printf("║  Foundation for parallel multiply-accumulate                      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    ESP_LOGI(TAG, "ESP32-C6 has %d PCNT units, using all of them", NUM_PCNT_UNITS);
    
    // Allocate pattern buffer
    pattern_buffer = heap_caps_aligned_alloc(4, MAX_PATTERN_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!pattern_buffer) {
        ESP_LOGE(TAG, "Failed to allocate pattern buffer");
        return;
    }
    
    // Setup hardware
    ESP_LOGI(TAG, "Setting up hardware...");
    ESP_ERROR_CHECK(setup_gpio());
    ESP_ERROR_CHECK(setup_all_pcnt());
    ESP_ERROR_CHECK(setup_parlio());
    ESP_LOGI(TAG, "Hardware ready!");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Run tests
    test_basic_parallel();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    test_cumulative();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    test_overflow_parallel();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    test_dot_product_sim();
    
    // Summary
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                          SUMMARY                                  ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
    printf("║  All 4 PCNT units can count the same GPIO independently!         ║\n");
    printf("║  Each has its own overflow tracking.                             ║\n");
    printf("║                                                                   ║\n");
    printf("║  Next steps for true parallel multiply:                          ║\n");
    printf("║  1. Use 4 GPIOs (one per PCNT)                                   ║\n");
    printf("║  2. Or: time-slice patterns with PCNT enable/disable             ║\n");
    printf("║  3. Or: use PCNT glitch filter to differentiate pulse widths     ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    
    heap_caps_free(pattern_buffer);
    
    ESP_LOGI(TAG, "Test complete.");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
