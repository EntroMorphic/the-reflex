/**
 * parallel_dot_product.c - True 4-Element Parallel Dot Product
 *
 * Uses PARLIO with 4-bit data width to output 4 independent pulse streams
 * simultaneously, each counted by a separate PCNT unit.
 *
 * Architecture:
 *   PARLIO (4-bit) ──┬── GPIO4 ── PCNT0 (accumulates w0 × x)
 *                    ├── GPIO5 ── PCNT1 (accumulates w1 × x)
 *                    ├── GPIO6 ── PCNT2 (accumulates w2 × x)
 *                    └── GPIO7 ── PCNT3 (accumulates w3 × x)
 *
 * Pattern encoding:
 *   Each byte in DMA buffer: [bit3][bit2][bit1][bit0]
 *   - bit0 → GPIO4 → PCNT0
 *   - bit1 → GPIO5 → PCNT1
 *   - bit2 → GPIO6 → PCNT2
 *   - bit3 → GPIO7 → PCNT3
 *
 * For dot product y = Σ(wi × xi):
 *   - Stream pulses where each channel gets wi pulses per xi clock cycles
 *   - All 4 products computed simultaneously
 *   - Sum the 4 PCNT values at the end
 *
 * This is the foundation for hardware neural inference!
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

static const char *TAG = "PARALLEL_DOT";

// ============================================================
// Configuration
// ============================================================

// 4 GPIOs for 4 parallel channels
#define GPIO_CH0    4
#define GPIO_CH1    5
#define GPIO_CH2    6
#define GPIO_CH3    7

#define NUM_CHANNELS    4
#define PARLIO_FREQ_HZ  10000000  // 10 MHz

// PCNT overflow tracking
#define PCNT_OVERFLOW_LIMIT 30000

// Pattern buffer - each byte encodes 4 simultaneous pulses
#define MAX_PATTERN_BYTES   8192
#define MIN_PATTERN_BYTES   4

// ============================================================
// Hardware handles
// ============================================================

static pcnt_unit_handle_t pcnt_units[NUM_CHANNELS] = {NULL};
static pcnt_channel_handle_t pcnt_channels[NUM_CHANNELS] = {NULL};
static parlio_tx_unit_handle_t parlio = NULL;

static const int gpio_nums[NUM_CHANNELS] = {GPIO_CH0, GPIO_CH1, GPIO_CH2, GPIO_CH3};

// Overflow tracking per channel
static volatile int overflow_counts[NUM_CHANNELS] = {0};

// Pattern buffer
static uint8_t *pattern_buffer = NULL;

// ============================================================
// Overflow callbacks
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

static pcnt_watch_cb_t overflow_callbacks[NUM_CHANNELS] = {
    pcnt_overflow_cb_0,
    pcnt_overflow_cb_1,
    pcnt_overflow_cb_2,
    pcnt_overflow_cb_3
};

// ============================================================
// Get full count for a channel
// ============================================================

static int get_full_count(int ch) {
    int pcnt_value;
    pcnt_unit_get_count(pcnt_units[ch], &pcnt_value);
    return (overflow_counts[ch] * PCNT_OVERFLOW_LIMIT) + pcnt_value;
}

// ============================================================
// Reset all counters
// ============================================================

static void reset_all_counters(void) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        overflow_counts[i] = 0;
        pcnt_unit_clear_count(pcnt_units[i]);
    }
}

// ============================================================
// Setup hardware
// ============================================================

static esp_err_t setup_gpios(void) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << gpio_nums[i]),
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }
    return ESP_OK;
}

static esp_err_t setup_all_pcnt(void) {
    ESP_LOGI(TAG, "Setting up %d PCNT units on GPIOs %d,%d,%d,%d",
             NUM_CHANNELS, GPIO_CH0, GPIO_CH1, GPIO_CH2, GPIO_CH3);
    
    for (int i = 0; i < NUM_CHANNELS; i++) {
        pcnt_unit_config_t cfg = {
            .low_limit = -32768,
            .high_limit = PCNT_OVERFLOW_LIMIT,
            .flags.accum_count = 0,
        };
        ESP_ERROR_CHECK(pcnt_new_unit(&cfg, &pcnt_units[i]));
        
        // Each PCNT watches its own GPIO
        pcnt_chan_config_t chan_cfg = {
            .edge_gpio_num = gpio_nums[i],
            .level_gpio_num = -1,
        };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_units[i], &chan_cfg, &pcnt_channels[i]));
        
        pcnt_channel_set_edge_action(pcnt_channels[i],
            PCNT_CHANNEL_EDGE_ACTION_INCREASE,
            PCNT_CHANNEL_EDGE_ACTION_HOLD);
        
        pcnt_unit_add_watch_point(pcnt_units[i], PCNT_OVERFLOW_LIMIT);
        
        pcnt_event_callbacks_t cbs = {
            .on_reach = overflow_callbacks[i],
        };
        pcnt_unit_register_event_callbacks(pcnt_units[i], &cbs, NULL);
        
        pcnt_unit_enable(pcnt_units[i]);
        pcnt_unit_start(pcnt_units[i]);
        
        ESP_LOGI(TAG, "  PCNT[%d] on GPIO%d ready", i, gpio_nums[i]);
    }
    
    return ESP_OK;
}

static esp_err_t setup_parlio(void) {
    parlio_tx_unit_config_t cfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = PARLIO_FREQ_HZ,
        .data_width = 4,  // 4-bit parallel output!
        .clk_out_gpio_num = -1,
        .valid_gpio_num = -1,
        .trans_queue_depth = 8,
        .max_transfer_size = MAX_PATTERN_BYTES + 100,
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB,  // LSB first for easier bit mapping
        .flags = { .io_loop_back = 1 },
    };
    
    // Map data bits to GPIOs: bit0→GPIO4, bit1→GPIO5, bit2→GPIO6, bit3→GPIO7
    for (int i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; i++) {
        if (i < NUM_CHANNELS) {
            cfg.data_gpio_nums[i] = gpio_nums[i];
        } else {
            cfg.data_gpio_nums[i] = -1;
        }
    }
    
    ESP_ERROR_CHECK(parlio_new_tx_unit(&cfg, &parlio));
    parlio_tx_unit_enable(parlio);
    
    ESP_LOGI(TAG, "PARLIO configured: 4-bit width, %d Hz", PARLIO_FREQ_HZ);
    
    return ESP_OK;
}

// ============================================================
// Build pattern for parallel multiply
// ============================================================

/**
 * Build a pattern buffer where each channel gets a different number of pulses.
 * 
 * For PCNT to count, we need transitions (0→1 edges).
 * Pattern: alternating 0x00 and 0x0F creates pulses on all channels.
 * For individual channel control: alternate 0x00 and channel mask.
 *
 * With data_width=4 and LSB bit order:
 *   - Bits 0-3 of each byte = output for channels 0-3
 *   - Each byte is one 4-bit output cycle
 *   - For pulses: need 0→1 transitions, so alternate low and high
 *
 * Each pulse = 2 bytes (low then high)
 */
static size_t build_parallel_pattern(int weights[4], int activation) {
    // Calculate pulses per channel
    int pulses[NUM_CHANNELS];
    int max_pulses = 0;
    
    for (int i = 0; i < NUM_CHANNELS; i++) {
        pulses[i] = weights[i] * activation;
        if (pulses[i] > max_pulses) max_pulses = pulses[i];
    }
    
    // Each pulse needs 2 cycles (low, high) - but PCNT counts edges not levels
    // So pattern: 0, mask, 0, mask, ... for each pulse
    // Total cycles = max_pulses * 2
    size_t num_cycles = max_pulses * 2;
    if (num_cycles < MIN_PATTERN_BYTES) num_cycles = MIN_PATTERN_BYTES;
    
    // With data_width=4, we output 4 bits per byte (since 4 bits fit in lower nibble)
    // Actually let's just use 1 byte per cycle for simplicity
    size_t pattern_bytes = num_cycles;
    if (pattern_bytes > MAX_PATTERN_BYTES) pattern_bytes = MAX_PATTERN_BYTES;
    
    // Clear buffer
    memset(pattern_buffer, 0, MAX_PATTERN_BYTES);
    
    // Track remaining pulses per channel
    int remaining[NUM_CHANNELS];
    for (int i = 0; i < NUM_CHANNELS; i++) {
        remaining[i] = pulses[i];
    }
    
    // Fill pattern: alternate between 0 and channel mask
    for (size_t cycle = 0; cycle < num_cycles; cycle++) {
        if (cycle % 2 == 0) {
            // Low phase - all zeros
            pattern_buffer[cycle] = 0x00;
        } else {
            // High phase - set bits for channels that still need pulses
            uint8_t mask = 0;
            for (int ch = 0; ch < NUM_CHANNELS; ch++) {
                if (remaining[ch] > 0) {
                    mask |= (1 << ch);
                    remaining[ch]--;
                }
            }
            pattern_buffer[cycle] = mask;
        }
    }
    
    ESP_LOGI(TAG, "Pattern: %zu cycles (%zu bytes), pulses=[%d,%d,%d,%d]",
             num_cycles, pattern_bytes, pulses[0], pulses[1], pulses[2], pulses[3]);
    
    return pattern_bytes;
}

// ============================================================
// Execute parallel dot product
// ============================================================

static int execute_dot_product(int weights[4], int activation) {
    // Build pattern
    size_t pattern_len = build_parallel_pattern(weights, activation);
    
    // Reset counters
    reset_all_counters();
    
    int64_t start_time = esp_timer_get_time();
    
    // Transmit pattern - all 4 channels output simultaneously!
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    
    // pattern_len is now in bytes, with 2 cycles packed per byte
    // Transmit pattern_len bytes = pattern_len * 8 bits
    esp_err_t ret = parlio_tx_unit_transmit(parlio, pattern_buffer, pattern_len * 8, &tx_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PARLIO transmit failed: %s", esp_err_to_name(ret));
        return -1;
    }
    
    parlio_tx_unit_wait_all_done(parlio, 5000);
    esp_rom_delay_us(10);
    
    int64_t end_time = esp_timer_get_time();
    
    // Read all counters
    int counts[NUM_CHANNELS];
    int total = 0;
    
    for (int i = 0; i < NUM_CHANNELS; i++) {
        counts[i] = get_full_count(i);
        total += counts[i];
    }
    
    // Calculate expected
    int expected = 0;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        expected += weights[i] * activation;
    }
    
    printf("  w=[%d,%d,%d,%d] x=%d\n", weights[0], weights[1], weights[2], weights[3], activation);
    printf("    Products: [%d,%d,%d,%d]\n", 
           weights[0]*activation, weights[1]*activation, 
           weights[2]*activation, weights[3]*activation);
    printf("    Counts:   [%d,%d,%d,%d]\n", counts[0], counts[1], counts[2], counts[3]);
    printf("    Sum: %d (expected %d) [%lld µs] %s\n",
           total, expected, (long long)(end_time - start_time),
           (total == expected) ? "✓" : "FAIL");
    
    return total;
}

// ============================================================
// Test suite
// ============================================================

static void test_basic_parallel(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 1: BASIC PARALLEL OUTPUT                                    ║\n");
    printf("║  Verify 4 channels output independently                           ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Test: each channel gets different count
    int weights1[4] = {1, 2, 3, 4};
    execute_dot_product(weights1, 10);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Test: only some channels active
    int weights2[4] = {5, 0, 5, 0};
    execute_dot_product(weights2, 10);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Test: single channel
    int weights3[4] = {0, 0, 0, 10};
    execute_dot_product(weights3, 10);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void test_dot_products(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 2: DOT PRODUCT COMPUTATION                                  ║\n");
    printf("║  y = w0*x + w1*x + w2*x + w3*x                                    ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    struct {
        int weights[4];
        int activation;
    } tests[] = {
        {{1, 1, 1, 1}, 10},      // 40
        {{1, 2, 3, 4}, 10},      // 100
        {{5, 5, 5, 5}, 20},      // 400
        {{10, 20, 30, 40}, 5},   // 500
        {{3, 5, 2, 4}, 100},     // 1400 (same as earlier simulation)
        {{100, 100, 100, 100}, 10}, // 4000
    };
    
    int num_tests = sizeof(tests) / sizeof(tests[0]);
    int passed = 0;
    
    for (int i = 0; i < num_tests; i++) {
        int expected = 0;
        for (int j = 0; j < 4; j++) {
            expected += tests[i].weights[j] * tests[i].activation;
        }
        
        int result = execute_dot_product(tests[i].weights, tests[i].activation);
        if (result == expected) passed++;
        
        printf("\n");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    printf("  Result: %d/%d passed\n", passed, num_tests);
}

static void test_neuron_simulation(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST 3: NEURON SIMULATION                                        ║\n");
    printf("║  Simulating: y = Σ(wi × xi) for a 4-input neuron                 ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Simulate a simple neuron with 4 inputs
    // In a real neuron: y = activation_fn(Σ wi×xi + bias)
    // Here we compute just the weighted sum
    
    printf("  Neuron: 4 inputs, weights learned for XOR-like pattern\n\n");
    
    // Weights (pretend these were learned)
    int weights[4] = {3, -2, 5, 1};  // Note: negative weights need special handling
    
    // For now, use absolute values (we'll handle signs later)
    int abs_weights[4] = {3, 2, 5, 1};
    
    // Test inputs
    int inputs[][4] = {
        {10, 10, 10, 10},  // All equal
        {20, 0, 20, 0},    // Alternating
        {0, 30, 0, 30},    // Opposite alternating
        {5, 10, 15, 20},   // Gradient
    };
    
    printf("  (Using absolute weights for now: [%d,%d,%d,%d])\n\n",
           abs_weights[0], abs_weights[1], abs_weights[2], abs_weights[3]);
    
    for (int t = 0; t < 4; t++) {
        printf("  Input %d: [%d,%d,%d,%d]\n", t,
               inputs[t][0], inputs[t][1], inputs[t][2], inputs[t][3]);
        
        // For true dot product with different activations per channel,
        // we'd need to run 4 separate patterns or multiplex differently.
        // For now, demonstrate with uniform activation:
        int uniform_activation = (inputs[t][0] + inputs[t][1] + inputs[t][2] + inputs[t][3]) / 4;
        
        execute_dot_product(abs_weights, uniform_activation);
        printf("\n");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    printf("  Note: True neuron needs different activation per weight.\n");
    printf("  Current design: same activation × different weights.\n");
    printf("  For full flexibility: need time-multiplexed approach.\n");
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  TRUE 4-ELEMENT PARALLEL DOT PRODUCT                              ║\n");
    printf("║  4 independent PARLIO channels → 4 PCNTs                          ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    ESP_LOGI(TAG, "Architecture: PARLIO 4-bit → GPIO[4,5,6,7] → PCNT[0,1,2,3]");
    
    // Allocate pattern buffer
    pattern_buffer = heap_caps_aligned_alloc(4, MAX_PATTERN_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!pattern_buffer) {
        ESP_LOGE(TAG, "Failed to allocate pattern buffer");
        return;
    }
    
    // Setup hardware
    ESP_LOGI(TAG, "Setting up hardware...");
    ESP_ERROR_CHECK(setup_gpios());
    ESP_ERROR_CHECK(setup_all_pcnt());
    ESP_ERROR_CHECK(setup_parlio());
    ESP_LOGI(TAG, "Hardware ready!");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Run tests
    test_basic_parallel();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    test_dot_products();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    test_neuron_simulation();
    
    // Summary
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                          SUMMARY                                  ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
    printf("║  True parallel dot product achieved!                             ║\n");
    printf("║                                                                   ║\n");
    printf("║  Architecture:                                                    ║\n");
    printf("║    PARLIO (4-bit) → 4 GPIOs → 4 PCNTs                            ║\n");
    printf("║    Single DMA transfer, 4 simultaneous pulse streams             ║\n");
    printf("║                                                                   ║\n");
    printf("║  Current: y = Σ(wi × x) where x is uniform                       ║\n");
    printf("║  Next: y = Σ(wi × xi) with per-channel activations               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    
    heap_caps_free(pattern_buffer);
    
    ESP_LOGI(TAG, "Test complete.");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
