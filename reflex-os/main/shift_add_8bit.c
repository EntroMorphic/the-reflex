/**
 * shift_add_8bit.c - 8-bit Hardware Multiplication via Shift-Add
 *
 * Explores maximum bit-width for shift-add multiplication in ETM fabric.
 *
 * Constraints:
 *   - PCNT: 16-bit signed (-32768 to +32767)
 *   - Safe max: 8-bit × 7-bit = 255 × 127 = 32385
 *   - 8-bit × 8-bit = 255 × 255 = 65025 OVERFLOWS PCNT!
 *
 * This test explores:
 *   1. 8-bit × 4-bit (safe, fast)
 *   2. 8-bit × 6-bit (safe, medium)
 *   3. 8-bit × 7-bit (near limit)
 *   4. 8-bit × 8-bit with dual-PCNT for overflow handling
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

static const char *TAG = "8BIT_MUL";

// ============================================================
// Configuration
// ============================================================

#define OUTPUT_GPIO         4
#define PARLIO_FREQ_HZ      10000000  // 10 MHz for faster execution

// For 8-bit values, max shift is A × 128 = A << 7
// Max A = 255, so max pattern = 255 × 128 = 32640 bytes
#define MAX_SHIFT_BYTES     33000

// ============================================================
// Pulse pattern byte
// ============================================================

#define PULSE_BYTE          0x80    // 10000000 = 1 rising edge per byte
#define MIN_PATTERN_BYTES   4       // Minimum for PARLIO stability

// ============================================================
// Dynamic pattern buffers (allocated from heap)
// ============================================================

static uint8_t *pattern_buffers[8] = {NULL};  // shift0 through shift7
static size_t pattern_lengths[8] = {0};
static int pattern_pulses[8] = {0};

// ============================================================
// Hardware handles
// ============================================================

static pcnt_unit_handle_t pcnt = NULL;
static pcnt_channel_handle_t pcnt_chan = NULL;
static parlio_tx_unit_handle_t parlio = NULL;

// ============================================================
// Allocate pattern buffers
// ============================================================

static bool allocate_patterns(int max_A) {
    // Calculate maximum buffer needed for each shift
    for (int i = 0; i < 8; i++) {
        size_t needed = max_A * (1 << i);
        if (needed < MIN_PATTERN_BYTES) needed = MIN_PATTERN_BYTES;
        if (needed > MAX_SHIFT_BYTES) needed = MAX_SHIFT_BYTES;
        
        // Allocate DMA-capable memory (4-byte aligned)
        pattern_buffers[i] = heap_caps_aligned_alloc(4, needed, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!pattern_buffers[i]) {
            ESP_LOGE(TAG, "Failed to allocate %zu bytes for shift%d", needed, i);
            return false;
        }
        pattern_lengths[i] = needed;
        memset(pattern_buffers[i], 0, needed);
        ESP_LOGI(TAG, "Allocated shift%d: %zu bytes", i, needed);
    }
    
    size_t total = 0;
    for (int i = 0; i < 8; i++) total += pattern_lengths[i];
    ESP_LOGI(TAG, "Total pattern memory: %zu bytes", total);
    
    return true;
}

static void free_patterns(void) {
    for (int i = 0; i < 8; i++) {
        if (pattern_buffers[i]) {
            heap_caps_free(pattern_buffers[i]);
            pattern_buffers[i] = NULL;
        }
    }
}

// ============================================================
// Initialize patterns for value A
// ============================================================

static void init_patterns_for_A(int A) {
    for (int shift = 0; shift < 8; shift++) {
        int pulses = A * (1 << shift);
        pattern_pulses[shift] = pulses;
        
        // Clear buffer
        memset(pattern_buffers[shift], 0, pattern_lengths[shift]);
        
        // Fill with pulse bytes
        size_t fill = (pulses < pattern_lengths[shift]) ? pulses : pattern_lengths[shift];
        for (size_t i = 0; i < fill; i++) {
            pattern_buffers[shift][i] = PULSE_BYTE;
        }
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

static esp_err_t setup_pcnt(void) {
    pcnt_unit_config_t cfg = {
        .low_limit = -32768,
        .high_limit = 32767,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&cfg, &pcnt));
    
    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num = OUTPUT_GPIO,
        .level_gpio_num = -1,
    };
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt, &chan_cfg, &pcnt_chan));
    
    // Count rising edges only
    pcnt_channel_set_edge_action(pcnt_chan,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_HOLD);
    
    pcnt_unit_enable(pcnt);
    pcnt_unit_start(pcnt);
    
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
        .max_transfer_size = MAX_SHIFT_BYTES + 100,
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
// Execute multiplication
// ============================================================

static int execute_multiply(int A, int B, int B_bits) {
    init_patterns_for_A(A);
    pcnt_unit_clear_count(pcnt);
    
    int64_t start_time = esp_timer_get_time();
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    
    // Output patterns for each set bit in B
    for (int shift = 0; shift < B_bits; shift++) {
        if (B & (1 << shift)) {
            size_t tx_len = pattern_pulses[shift];
            if (tx_len < MIN_PATTERN_BYTES) tx_len = MIN_PATTERN_BYTES;
            if (tx_len > pattern_lengths[shift]) tx_len = pattern_lengths[shift];
            
            esp_err_t ret = parlio_tx_unit_transmit(parlio,
                                                     pattern_buffers[shift],
                                                     tx_len * 8,
                                                     &tx_cfg);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "TX failed for shift%d", shift);
                continue;
            }
            parlio_tx_unit_wait_all_done(parlio, 5000);
            esp_rom_delay_us(5);
        }
    }
    
    int64_t end_time = esp_timer_get_time();
    
    int result;
    pcnt_unit_get_count(pcnt, &result);
    
    int expected = A * B;
    bool overflow = (expected > 32767);
    bool match = overflow ? false : (result == expected);
    
    printf("  %3d x %3d = %5d (PCNT=%5d) [%6lld us] %s%s\n",
           A, B, expected, result,
           (long long)(end_time - start_time),
           match ? "OK" : "FAIL",
           overflow ? " (OVERFLOW)" : "");
    
    return result;
}

// ============================================================
// Test suites
// ============================================================

static void test_4bit_multiplier(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  8-bit × 4-bit MULTIPLICATION (max 255 × 15 = 3825)          ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    int tests[][2] = {
        {200, 10},
        {255, 15},
        {128, 8},
        {100, 12},
        {255, 1},
        {1, 15},
    };
    
    int passed = 0;
    int total = sizeof(tests) / sizeof(tests[0]);
    
    for (int i = 0; i < total; i++) {
        int A = tests[i][0];
        int B = tests[i][1];
        int result = execute_multiply(A, B, 4);
        if (result == A * B) passed++;
    }
    
    printf("\n  Result: %d/%d passed\n", passed, total);
}

static void test_6bit_multiplier(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  8-bit × 6-bit MULTIPLICATION (max 255 × 63 = 16065)         ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    int tests[][2] = {
        {200, 50},
        {255, 63},
        {128, 32},
        {100, 45},
        {255, 1},
        {1, 63},
    };
    
    int passed = 0;
    int total = sizeof(tests) / sizeof(tests[0]);
    
    for (int i = 0; i < total; i++) {
        int A = tests[i][0];
        int B = tests[i][1];
        int result = execute_multiply(A, B, 6);
        if (result == A * B) passed++;
    }
    
    printf("\n  Result: %d/%d passed\n", passed, total);
}

static void test_7bit_multiplier(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  8-bit × 7-bit MULTIPLICATION (max 255 × 127 = 32385)        ║\n");
    printf("║  (Near PCNT limit of 32767)                                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    int tests[][2] = {
        {200, 100},
        {255, 127},   // Max safe: 32385
        {128, 64},
        {100, 100},
        {255, 1},
        {1, 127},
        {256, 127},   // Would be 32512, still fits but A > 8-bit conceptually
    };
    
    int passed = 0;
    int total = sizeof(tests) / sizeof(tests[0]);
    
    for (int i = 0; i < total; i++) {
        int A = tests[i][0];
        int B = tests[i][1];
        int expected = A * B;
        if (expected > 32767) {
            printf("  %3d x %3d = %5d SKIPPED (overflow)\n", A, B, expected);
            continue;
        }
        int result = execute_multiply(A, B, 7);
        if (result == expected) passed++;
    }
    
    printf("\n  Result: %d/%d passed\n", passed, total);
}

static void test_8bit_multiplier(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  8-bit × 8-bit MULTIPLICATION (max 255 × 255 = 65025)        ║\n");
    printf("║  WARNING: Values > 32767 WILL OVERFLOW PCNT!                  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    int tests[][2] = {
        {100, 100},   // 10000 - safe
        {128, 128},   // 16384 - safe
        {180, 180},   // 32400 - near limit
        {181, 181},   // 32761 - very close!
        {182, 182},   // 33124 - OVERFLOW!
        {200, 200},   // 40000 - OVERFLOW!
        {255, 255},   // 65025 - MAX OVERFLOW!
    };
    
    int total = sizeof(tests) / sizeof(tests[0]);
    
    for (int i = 0; i < total; i++) {
        int A = tests[i][0];
        int B = tests[i][1];
        execute_multiply(A, B, 8);
    }
    
    printf("\n  Note: PCNT wraps at 32767, so overflowed values show wrong result\n");
}

static void test_timing_analysis(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  TIMING ANALYSIS                                              ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Single bit tests to measure base overhead
    printf("  Single-shift timing (overhead measurement):\n");
    execute_multiply(1, 1, 8);    // Just shift0
    execute_multiply(1, 128, 8);  // Just shift7
    execute_multiply(255, 1, 8);  // 255 pulses in shift0
    execute_multiply(255, 128, 8); // 32640 pulses in shift7
    
    printf("\n  Multi-shift timing:\n");
    execute_multiply(100, 255, 8);  // All 8 shifts, A=100
    execute_multiply(181, 181, 8);  // Near-max safe
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║       8-BIT SHIFT-ADD MULTIPLIER - BIT WIDTH EXPLORATION      ║\n");
    printf("║       ESP32-C6 ETM Fabric                                     ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    ESP_LOGI(TAG, "PARLIO clock: %d Hz", PARLIO_FREQ_HZ);
    ESP_LOGI(TAG, "Max pattern size: %d bytes", MAX_SHIFT_BYTES);
    
    // Allocate pattern buffers
    ESP_LOGI(TAG, "Allocating pattern buffers for max A=255...");
    if (!allocate_patterns(255)) {
        ESP_LOGE(TAG, "Failed to allocate patterns!");
        return;
    }
    
    // Setup hardware
    ESP_LOGI(TAG, "Setting up hardware...");
    ESP_ERROR_CHECK(setup_gpio());
    ESP_ERROR_CHECK(setup_pcnt());
    ESP_ERROR_CHECK(setup_parlio());
    ESP_LOGI(TAG, "Hardware ready!");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Run test suites
    test_4bit_multiplier();
    vTaskDelay(pdMS_TO_TICKS(200));
    
    test_6bit_multiplier();
    vTaskDelay(pdMS_TO_TICKS(200));
    
    test_7bit_multiplier();
    vTaskDelay(pdMS_TO_TICKS(200));
    
    test_8bit_multiplier();
    vTaskDelay(pdMS_TO_TICKS(200));
    
    test_timing_analysis();
    
    // Summary
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                       SUMMARY                                 ║\n");
    printf("╠═══════════════════════════════════════════════════════════════╣\n");
    printf("║  PCNT limit: 32767 (16-bit signed)                           ║\n");
    printf("║  Safe max: 8-bit × 7-bit = 255 × 127 = 32385                 ║\n");
    printf("║  Or: 181 × 181 = 32761 (largest square < 32767)              ║\n");
    printf("║                                                               ║\n");
    printf("║  For full 8×8: Need dual-PCNT or 32-bit accumulator          ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    
    // Cleanup
    free_patterns();
    
    ESP_LOGI(TAG, "Test complete.");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
