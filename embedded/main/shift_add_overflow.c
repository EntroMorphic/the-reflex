/**
 * shift_add_overflow.c - Extended Bit-Width via PCNT Overflow Tracking
 *
 * Uses PCNT high_limit callback to track overflows, extending effective
 * bit-width beyond the 16-bit PCNT limit.
 *
 * Strategy:
 *   - Set PCNT high_limit to 30000 (leaves headroom)
 *   - When limit reached, ISR increments overflow_count
 *   - PCNT clears automatically and continues
 *   - Final result = overflow_count × 30000 + pcnt_value
 *
 * This enables full 8×8 (65025 max) and potentially larger multiplications!
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

static const char *TAG = "OVERFLOW_MUL";

// ============================================================
// Configuration
// ============================================================

#define OUTPUT_GPIO         4
#define PARLIO_FREQ_HZ      10000000  // 10 MHz

// Overflow tracking configuration
// Use 30000 as limit (leaves room for count between ISR checks)
#define PCNT_OVERFLOW_LIMIT 30000

// Max pattern size for 8×8 multiply (255 × 128 = 32640)
#define MAX_SHIFT_BYTES     33000

#define PULSE_BYTE          0x80
#define MIN_PATTERN_BYTES   4

// ============================================================
// Pattern buffers (8 shifts for 8-bit B)
// ============================================================

static uint8_t *pattern_buffers[8] = {NULL};
static size_t pattern_lengths[8] = {0};
static int pattern_pulses[8] = {0};

// ============================================================
// Hardware handles
// ============================================================

static pcnt_unit_handle_t pcnt = NULL;
static pcnt_channel_handle_t pcnt_chan = NULL;
static parlio_tx_unit_handle_t parlio = NULL;

// ============================================================
// Overflow tracking (ISR-safe)
// ============================================================

static volatile int overflow_count = 0;

static bool IRAM_ATTR pcnt_overflow_cb(pcnt_unit_handle_t unit,
                                        const pcnt_watch_event_data_t *edata,
                                        void *user_ctx) {
    // Increment overflow counter
    overflow_count++;
    
    // PCNT automatically clears when hitting limit (with auto_clear enabled)
    // So we just track how many times this happened
    
    return false;  // No task switch needed
}

// ============================================================
// Calculate full result from PCNT + overflows
// ============================================================

static int get_full_count(void) {
    int pcnt_value;
    pcnt_unit_get_count(pcnt, &pcnt_value);
    
    return (overflow_count * PCNT_OVERFLOW_LIMIT) + pcnt_value;
}

// ============================================================
// Allocate pattern buffers
// ============================================================

static bool allocate_patterns(int max_A) {
    for (int i = 0; i < 8; i++) {
        size_t needed = max_A * (1 << i);
        if (needed < MIN_PATTERN_BYTES) needed = MIN_PATTERN_BYTES;
        if (needed > MAX_SHIFT_BYTES) needed = MAX_SHIFT_BYTES;
        
        pattern_buffers[i] = heap_caps_aligned_alloc(4, needed, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!pattern_buffers[i]) {
            ESP_LOGE(TAG, "Failed to allocate %zu bytes for shift%d", needed, i);
            return false;
        }
        pattern_lengths[i] = needed;
        memset(pattern_buffers[i], 0, needed);
    }
    
    size_t total = 0;
    for (int i = 0; i < 8; i++) total += pattern_lengths[i];
    ESP_LOGI(TAG, "Allocated %zu bytes for patterns", total);
    
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
        
        memset(pattern_buffers[shift], 0, pattern_lengths[shift]);
        
        size_t fill = (pulses < (int)pattern_lengths[shift]) ? pulses : pattern_lengths[shift];
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
        .high_limit = PCNT_OVERFLOW_LIMIT,  // Trigger overflow callback at this limit
        .flags.accum_count = 0,  // Don't accumulate, we track overflow manually
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
    
    // Add watch point at the limit to trigger callback
    pcnt_unit_add_watch_point(pcnt, PCNT_OVERFLOW_LIMIT);
    
    // Register overflow callback
    pcnt_event_callbacks_t cbs = {
        .on_reach = pcnt_overflow_cb,
    };
    pcnt_unit_register_event_callbacks(pcnt, &cbs, NULL);
    
    pcnt_unit_enable(pcnt);
    pcnt_unit_start(pcnt);
    
    ESP_LOGI(TAG, "PCNT configured with overflow limit %d", PCNT_OVERFLOW_LIMIT);
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
// Execute multiplication with overflow tracking
// ============================================================

static int execute_multiply(int A, int B) {
    init_patterns_for_A(A);
    
    // Reset counters
    overflow_count = 0;
    pcnt_unit_clear_count(pcnt);
    
    int64_t start_time = esp_timer_get_time();
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    
    // Output patterns for each set bit in B (all 8 bits)
    for (int shift = 0; shift < 8; shift++) {
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
    
    // Calculate full result using overflow tracking
    int pcnt_value;
    pcnt_unit_get_count(pcnt, &pcnt_value);
    int result = (overflow_count * PCNT_OVERFLOW_LIMIT) + pcnt_value;
    
    int expected = A * B;
    bool match = (result == expected);
    
    printf("  %3d x %3d = %5d (overflows=%d, pcnt=%5d, total=%5d) [%6lld us] %s\n",
           A, B, expected, overflow_count, pcnt_value, result,
           (long long)(end_time - start_time),
           match ? "OK" : "FAIL");
    
    return result;
}

// ============================================================
// Test suite
// ============================================================

static void test_8x8_full(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  FULL 8×8 MULTIPLICATION WITH OVERFLOW TRACKING                   ║\n");
    printf("║  PCNT limit: %d, tracking overflows in ISR                     ║\n", PCNT_OVERFLOW_LIMIT);
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    int tests[][2] = {
        // Small values (no overflow)
        {10, 10},       // 100
        {50, 50},       // 2500
        {100, 100},     // 10000
        
        // Near limit (single overflow)
        {180, 180},     // 32400 - just over one overflow
        {181, 181},     // 32761
        
        // Multiple overflows
        {182, 182},     // 33124 - previously failed!
        {200, 200},     // 40000
        {220, 220},     // 48400
        {250, 250},     // 62500
        
        // Maximum
        {255, 255},     // 65025 - max 8×8
        
        // Edge cases
        {255, 1},       // 255
        {1, 255},       // 255
        {128, 255},     // 32640
        {255, 128},     // 32640
    };
    
    int passed = 0;
    int total = sizeof(tests) / sizeof(tests[0]);
    
    for (int i = 0; i < total; i++) {
        int A = tests[i][0];
        int B = tests[i][1];
        int result = execute_multiply(A, B);
        if (result == A * B) passed++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    printf("\n");
    printf("  Result: %d/%d passed\n", passed, total);
    
    if (passed == total) {
        printf("\n");
        printf("  ╔═══════════════════════════════════════════════════════════════╗\n");
        printf("  ║  SUCCESS: Full 8×8 multiplication with overflow tracking!    ║\n");
        printf("  ║  Max tested: 255 × 255 = 65025                                ║\n");
        printf("  ╚═══════════════════════════════════════════════════════════════╝\n");
    }
}

static void test_larger_values(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  BEYOND 8×8: Testing larger multiplications                       ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Test larger A values (limited by pattern buffer size)
    // With MAX_SHIFT_BYTES = 33000, max A×128 = 33000, so max A ≈ 257
    // But we can test A > 255 if B has few high bits
    
    int tests[][2] = {
        {300, 100},     // 30000 - fits in single PCNT
        {300, 200},     // 60000 - needs overflow
        {400, 100},     // 40000
        {500, 50},      // 25000
        {500, 100},     // 50000
        {1000, 30},     // 30000
        {1000, 60},     // 60000
    };
    
    int passed = 0;
    int total = sizeof(tests) / sizeof(tests[0]);
    
    for (int i = 0; i < total; i++) {
        int A = tests[i][0];
        int B = tests[i][1];
        
        // Check if pattern would fit
        int max_shift_needed = 0;
        for (int s = 7; s >= 0; s--) {
            if (B & (1 << s)) {
                max_shift_needed = s;
                break;
            }
        }
        int max_pulses = A * (1 << max_shift_needed);
        
        if (max_pulses > MAX_SHIFT_BYTES) {
            printf("  %4d x %3d = %6d SKIPPED (pattern too large: %d > %d)\n",
                   A, B, A * B, max_pulses, MAX_SHIFT_BYTES);
            continue;
        }
        
        int result = execute_multiply(A, B);
        if (result == A * B) passed++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    printf("\n  Result: %d/%d passed\n", passed, total);
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  OVERFLOW-TRACKING SHIFT-ADD MULTIPLIER                           ║\n");
    printf("║  Extends bit-width via PCNT overflow ISR                          ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    ESP_LOGI(TAG, "PCNT overflow limit: %d", PCNT_OVERFLOW_LIMIT);
    ESP_LOGI(TAG, "Max pattern size: %d bytes", MAX_SHIFT_BYTES);
    
    // Allocate pattern buffers
    if (!allocate_patterns(1000)) {  // Support A up to 1000
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
    
    // Run tests
    test_8x8_full();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    test_larger_values();
    
    // Summary
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                          SUMMARY                                  ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
    printf("║  Overflow tracking extends PCNT beyond 16-bit limit!             ║\n");
    printf("║  Each overflow = %d additional counts                         ║\n", PCNT_OVERFLOW_LIMIT);
    printf("║  Theoretical max: ~2 billion (int32 limit)                       ║\n");
    printf("║  Practical limit: pattern buffer size (%d bytes)             ║\n", MAX_SHIFT_BYTES);
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    
    free_patterns();
    
    ESP_LOGI(TAG, "Test complete.");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
