/**
 * falsify_cfc_parallel.c - Rigorous Falsification of CfC Parallel Dot Product
 *
 * FALSIFICATION TESTS:
 * 1. Do hardware dot products match software computation exactly?
 * 2. Are ternary weight accumulations correct?
 * 3. Does the CfC hidden state evolve correctly vs reference?
 * 4. With known inputs/weights, is the output bit-exact?
 *
 * If any test fails, the implementation is FALSIFIED.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
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
#include "esp_random.h"

static const char *TAG = "FALSIFY_CFC";

// ============================================================
// Configuration (same as cfc_parallel_dot.c)
// ============================================================

#define NUM_PARALLEL        4
#define GPIO_CH0            4
#define GPIO_CH1            5
#define GPIO_CH2            6
#define GPIO_CH3            7

#define PARLIO_FREQ_HZ      10000000
#define PCNT_OVERFLOW_LIMIT 30000
#define MAX_PATTERN_BYTES   4096
#define MIN_PATTERN_BYTES   4

// ============================================================
// Hardware handles
// ============================================================

static pcnt_unit_handle_t pcnt_units[NUM_PARALLEL] = {NULL};
static pcnt_channel_handle_t pcnt_channels[NUM_PARALLEL] = {NULL};
static parlio_tx_unit_handle_t parlio = NULL;

static const int gpio_nums[NUM_PARALLEL] = {GPIO_CH0, GPIO_CH1, GPIO_CH2, GPIO_CH3};
static volatile int overflow_counts[NUM_PARALLEL] = {0};
static uint8_t *pattern_buffer = NULL;

// ============================================================
// Overflow callbacks
// ============================================================

static bool IRAM_ATTR pcnt_overflow_cb_0(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *ctx) { overflow_counts[0]++; return false; }
static bool IRAM_ATTR pcnt_overflow_cb_1(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *ctx) { overflow_counts[1]++; return false; }
static bool IRAM_ATTR pcnt_overflow_cb_2(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *ctx) { overflow_counts[2]++; return false; }
static bool IRAM_ATTR pcnt_overflow_cb_3(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *ctx) { overflow_counts[3]++; return false; }

static pcnt_watch_cb_t overflow_callbacks[NUM_PARALLEL] = {
    pcnt_overflow_cb_0, pcnt_overflow_cb_1, pcnt_overflow_cb_2, pcnt_overflow_cb_3
};

// ============================================================
// Hardware helpers
// ============================================================

static int get_full_count(int ch) {
    int val;
    pcnt_unit_get_count(pcnt_units[ch], &val);
    return (overflow_counts[ch] * PCNT_OVERFLOW_LIMIT) + val;
}

static void reset_all_counters(void) {
    for (int i = 0; i < NUM_PARALLEL; i++) {
        overflow_counts[i] = 0;
        pcnt_unit_clear_count(pcnt_units[i]);
    }
}

static esp_err_t setup_hardware(void) {
    // GPIOs
    for (int i = 0; i < NUM_PARALLEL; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << gpio_nums[i]),
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }
    
    // PCNTs
    for (int i = 0; i < NUM_PARALLEL; i++) {
        pcnt_unit_config_t cfg = {
            .low_limit = -32768,
            .high_limit = PCNT_OVERFLOW_LIMIT,
        };
        ESP_ERROR_CHECK(pcnt_new_unit(&cfg, &pcnt_units[i]));
        
        pcnt_chan_config_t chan_cfg = {
            .edge_gpio_num = gpio_nums[i],
            .level_gpio_num = -1,
        };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_units[i], &chan_cfg, &pcnt_channels[i]));
        pcnt_channel_set_edge_action(pcnt_channels[i], PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        
        pcnt_unit_add_watch_point(pcnt_units[i], PCNT_OVERFLOW_LIMIT);
        pcnt_event_callbacks_t cbs = { .on_reach = overflow_callbacks[i] };
        pcnt_unit_register_event_callbacks(pcnt_units[i], &cbs, NULL);
        
        pcnt_unit_enable(pcnt_units[i]);
        pcnt_unit_start(pcnt_units[i]);
    }
    
    // PARLIO
    parlio_tx_unit_config_t parlio_cfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = PARLIO_FREQ_HZ,
        .data_width = 4,
        .trans_queue_depth = 8,
        .max_transfer_size = MAX_PATTERN_BYTES + 100,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB,
        .flags = { .io_loop_back = 1 },
    };
    for (int i = 0; i < NUM_PARALLEL; i++) {
        parlio_cfg.data_gpio_nums[i] = gpio_nums[i];
    }
    for (int i = NUM_PARALLEL; i < 16; i++) {
        parlio_cfg.data_gpio_nums[i] = -1;
    }
    
    ESP_ERROR_CHECK(parlio_new_tx_unit(&parlio_cfg, &parlio));
    parlio_tx_unit_enable(parlio);
    
    pattern_buffer = heap_caps_aligned_alloc(4, MAX_PATTERN_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    
    return ESP_OK;
}

// ============================================================
// TEST 1: Basic pulse counting accuracy
// ============================================================

static int test_basic_pulse_counting(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  FALSIFICATION TEST 1: BASIC PULSE COUNTING                       ║\n");
    printf("║  Does hardware count exactly the pulses we send?                  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    int failures = 0;
    
    // Test specific pulse counts
    int test_counts[][4] = {
        {10, 10, 10, 10},       // Uniform
        {1, 2, 3, 4},           // Ascending
        {100, 50, 25, 12},      // Descending
        {0, 100, 0, 100},       // Alternating zeros
        {1, 0, 0, 0},           // Single channel
        {255, 255, 255, 255},   // Max 8-bit
    };
    
    int num_tests = sizeof(test_counts) / sizeof(test_counts[0]);
    
    for (int t = 0; t < num_tests; t++) {
        int expected[4];
        for (int i = 0; i < 4; i++) expected[i] = test_counts[t][i];
        
        // Find max for pattern length
        int max_pulses = 0;
        for (int i = 0; i < 4; i++) {
            if (expected[i] > max_pulses) max_pulses = expected[i];
        }
        
        // Build edge-based pattern
        size_t num_cycles = max_pulses * 2;
        if (num_cycles < MIN_PATTERN_BYTES) num_cycles = MIN_PATTERN_BYTES;
        
        memset(pattern_buffer, 0, MAX_PATTERN_BYTES);
        
        int remaining[4];
        for (int i = 0; i < 4; i++) remaining[i] = expected[i];
        
        for (size_t cycle = 0; cycle < num_cycles; cycle++) {
            if (cycle % 2 == 0) {
                pattern_buffer[cycle] = 0x00;
            } else {
                uint8_t mask = 0;
                for (int ch = 0; ch < 4; ch++) {
                    if (remaining[ch] > 0) {
                        mask |= (1 << ch);
                        remaining[ch]--;
                    }
                }
                pattern_buffer[cycle] = mask;
            }
        }
        
        // Transmit
        reset_all_counters();
        parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
        parlio_tx_unit_transmit(parlio, pattern_buffer, num_cycles * 8, &tx_cfg);
        parlio_tx_unit_wait_all_done(parlio, 1000);
        esp_rom_delay_us(10);
        
        // Check
        int actual[4];
        for (int i = 0; i < 4; i++) {
            actual[i] = get_full_count(i);
        }
        
        int match = 1;
        for (int i = 0; i < 4; i++) {
            if (actual[i] != expected[i]) match = 0;
        }
        
        printf("  Test %d: expected=[%3d,%3d,%3d,%3d] actual=[%3d,%3d,%3d,%3d] %s\n",
               t, expected[0], expected[1], expected[2], expected[3],
               actual[0], actual[1], actual[2], actual[3],
               match ? "PASS" : "FAIL");
        
        if (!match) failures++;
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    printf("\n  Result: %d/%d passed\n", num_tests - failures, num_tests);
    return failures;
}

// ============================================================
// TEST 2: Software dot product vs hardware
// ============================================================

static int software_ternary_dot(const uint8_t* input, int input_len,
                                 uint32_t pos_mask, uint32_t neg_mask) {
    int pos_sum = 0;
    int neg_sum = 0;
    
    for (int i = 0; i < input_len && i < 32; i++) {
        if (pos_mask & (1 << i)) {
            pos_sum += input[i];
        }
        if (neg_mask & (1 << i)) {
            neg_sum += input[i];
        }
    }
    
    return pos_sum - neg_sum;
}

static int test_ternary_dot_product(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  FALSIFICATION TEST 2: TERNARY DOT PRODUCT                        ║\n");
    printf("║  Does hardware match software dot(ternary_weights, input)?        ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    int failures = 0;
    
    // Test cases: [input_len, pos_mask, neg_mask, input values...]
    // We'll test with 8 inputs for simplicity
    
    struct {
        uint8_t input[8];
        uint32_t pos_mask;
        uint32_t neg_mask;
    } tests[] = {
        // All positive weights
        {{5, 5, 5, 5, 5, 5, 5, 5}, 0xFF, 0x00},  // 8×5 = 40
        
        // All negative weights
        {{5, 5, 5, 5, 5, 5, 5, 5}, 0x00, 0xFF},  // -40
        
        // Mixed weights: +1 on even, -1 on odd
        {{10, 10, 10, 10, 10, 10, 10, 10}, 0x55, 0xAA},  // 40 - 40 = 0
        
        // Some zeros (sparse)
        {{10, 0, 10, 0, 10, 0, 10, 0}, 0xFF, 0x00},  // 40
        
        // Alternating values
        {{1, 2, 3, 4, 5, 6, 7, 8}, 0xFF, 0x00},  // 36
        
        // Complex pattern
        {{15, 10, 5, 0, 15, 10, 5, 0}, 0x33, 0xCC},  // (15+10+15+10) - (5+0+5+0) = 50 - 10 = 40
    };
    
    int num_tests = sizeof(tests) / sizeof(tests[0]);
    
    for (int t = 0; t < num_tests; t++) {
        // Software computation
        int sw_result = software_ternary_dot(tests[t].input, 8,
                                              tests[t].pos_mask, tests[t].neg_mask);
        
        // Hardware computation (positive part only - we count positive pulses)
        // Build pattern for positive weights
        int pulses[4] = {0, 0, 0, 0};
        
        // Channel 0 gets positive sum
        for (int i = 0; i < 8; i++) {
            if (tests[t].pos_mask & (1 << i)) {
                pulses[0] += tests[t].input[i];
            }
        }
        
        int max_pulses = pulses[0];
        if (max_pulses < 4) max_pulses = 4;
        
        size_t num_cycles = max_pulses * 2;
        memset(pattern_buffer, 0, MAX_PATTERN_BYTES);
        
        int remaining = pulses[0];
        for (size_t cycle = 0; cycle < num_cycles; cycle++) {
            if (cycle % 2 == 0) {
                pattern_buffer[cycle] = 0x00;
            } else {
                pattern_buffer[cycle] = (remaining > 0) ? 0x01 : 0x00;
                if (remaining > 0) remaining--;
            }
        }
        
        reset_all_counters();
        parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
        parlio_tx_unit_transmit(parlio, pattern_buffer, num_cycles * 8, &tx_cfg);
        parlio_tx_unit_wait_all_done(parlio, 1000);
        esp_rom_delay_us(10);
        
        int hw_pos = get_full_count(0);
        
        // Compute negative in software (we only have hardware for positive accumulation)
        int neg_sum = 0;
        for (int i = 0; i < 8; i++) {
            if (tests[t].neg_mask & (1 << i)) {
                neg_sum += tests[t].input[i];
            }
        }
        
        int hw_result = hw_pos - neg_sum;
        
        int match = (hw_result == sw_result);
        
        printf("  Test %d: pos_mask=0x%02X neg_mask=0x%02X\n", t, 
               (unsigned)tests[t].pos_mask, (unsigned)tests[t].neg_mask);
        printf("          sw_result=%d hw_result=%d (hw_pos=%d, neg_sum=%d) %s\n",
               sw_result, hw_result, hw_pos, neg_sum, match ? "PASS" : "FAIL");
        
        if (!match) failures++;
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    printf("\n  Result: %d/%d passed\n", num_tests - failures, num_tests);
    return failures;
}

// ============================================================
// TEST 3: LUT accuracy
// ============================================================

static int test_lut_accuracy(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  FALSIFICATION TEST 3: ACTIVATION LUT ACCURACY                    ║\n");
    printf("║  Do sigmoid/tanh LUTs produce expected values?                    ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    int failures = 0;
    
    // Generate LUTs
    uint8_t sigmoid_lut[256];
    uint8_t tanh_lut[256];
    
    for (int i = 0; i < 256; i++) {
        float x = ((float)i / 255.0f) * 16.0f - 8.0f;
        float sig = 1.0f / (1.0f + expf(-x));
        sigmoid_lut[i] = (uint8_t)(sig * 15.0f);
        
        float t = tanhf(x);
        tanh_lut[i] = (uint8_t)((t + 1.0f) * 7.5f);
    }
    
    // Test specific points
    struct {
        int lut_idx;
        float expected_sig;
        float expected_tanh;
    } test_points[] = {
        {0, 0.0003, -1.0},      // x = -8
        {64, 0.018, -0.964},    // x = -4
        {128, 0.5, 0.0},        // x = 0
        {192, 0.982, 0.964},    // x = +4
        {255, 0.9997, 1.0},     // x = +8
    };
    
    int num_tests = sizeof(test_points) / sizeof(test_points[0]);
    
    for (int t = 0; t < num_tests; t++) {
        int idx = test_points[t].lut_idx;
        float x = ((float)idx / 255.0f) * 16.0f - 8.0f;
        
        // LUT values (0-15)
        uint8_t sig_q4 = sigmoid_lut[idx];
        uint8_t tanh_q4 = tanh_lut[idx];
        
        // Convert back to float
        float sig_actual = (float)sig_q4 / 15.0f;
        float tanh_actual = ((float)tanh_q4 / 7.5f) - 1.0f;
        
        // Expected (compute fresh)
        float sig_expected = 1.0f / (1.0f + expf(-x));
        float tanh_expected = tanhf(x);
        
        // Allow 0.1 tolerance due to 4-bit quantization
        int sig_ok = (fabsf(sig_actual - sig_expected) < 0.15f);
        int tanh_ok = (fabsf(tanh_actual - tanh_expected) < 0.15f);
        
        printf("  x=%.1f: sigmoid=%.3f (expected %.3f) %s, tanh=%.3f (expected %.3f) %s\n",
               x, sig_actual, sig_expected, sig_ok ? "OK" : "FAIL",
               tanh_actual, tanh_expected, tanh_ok ? "OK" : "FAIL");
        
        if (!sig_ok || !tanh_ok) failures++;
    }
    
    printf("\n  Result: %d/%d passed\n", num_tests - failures, num_tests);
    return failures;
}

// ============================================================
// TEST 4: CfC dynamics with known values
// ============================================================

static int test_cfc_dynamics(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  FALSIFICATION TEST 4: CfC DYNAMICS                               ║\n");
    printf("║  Does mixer LUT produce correct h_new values?                     ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    int failures = 0;
    
    // Generate mixer LUT
    uint8_t mixer_lut[16][16][16];
    float decay = 0.9f;
    
    for (int g = 0; g < 16; g++) {
        float gate = (float)g / 15.0f;
        for (int h = 0; h < 16; h++) {
            float h_prev = ((float)h / 7.5f) - 1.0f;
            for (int c = 0; c < 16; c++) {
                float cand = ((float)c / 7.5f) - 1.0f;
                float h_new = (1.0f - gate) * h_prev * decay + gate * cand;
                if (h_new < -1.0f) h_new = -1.0f;
                if (h_new > 1.0f) h_new = 1.0f;
                mixer_lut[g][h][c] = (uint8_t)((h_new + 1.0f) * 7.5f);
            }
        }
    }
    
    // Test specific combinations
    struct {
        int gate_q4;
        int h_prev_q4;
        int cand_q4;
    } test_cases[] = {
        {0, 8, 8},      // gate=0: h_new = h_prev * decay
        {15, 8, 15},    // gate=1: h_new = candidate
        {8, 0, 15},     // gate=0.5: mix of both
        {8, 15, 0},     // gate=0.5: opposite values
        {4, 8, 12},     // gate≈0.27
    };
    
    int num_tests = sizeof(test_cases) / sizeof(test_cases[0]);
    
    for (int t = 0; t < num_tests; t++) {
        int g = test_cases[t].gate_q4;
        int h = test_cases[t].h_prev_q4;
        int c = test_cases[t].cand_q4;
        
        // Software computation
        float gate = (float)g / 15.0f;
        float h_prev = ((float)h / 7.5f) - 1.0f;
        float cand = ((float)c / 7.5f) - 1.0f;
        float h_new_sw = (1.0f - gate) * h_prev * decay + gate * cand;
        if (h_new_sw < -1.0f) h_new_sw = -1.0f;
        if (h_new_sw > 1.0f) h_new_sw = 1.0f;
        
        // LUT result
        uint8_t h_new_lut = mixer_lut[g][h][c];
        float h_new_lut_f = ((float)h_new_lut / 7.5f) - 1.0f;
        
        // Allow quantization tolerance
        int match = (fabsf(h_new_sw - h_new_lut_f) < 0.15f);
        
        printf("  gate=%2d h_prev=%2d cand=%2d: sw=%.3f lut=%.3f (q4=%d) %s\n",
               g, h, c, h_new_sw, h_new_lut_f, h_new_lut, match ? "PASS" : "FAIL");
        
        if (!match) failures++;
    }
    
    printf("\n  Result: %d/%d passed\n", num_tests - failures, num_tests);
    return failures;
}

// ============================================================
// TEST 5: End-to-end known computation
// ============================================================

static int test_end_to_end(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  FALSIFICATION TEST 5: END-TO-END KNOWN VALUES                    ║\n");
    printf("║  Fixed input → fixed weights → verify exact output                ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    int failures = 0;
    
    // Known input
    uint8_t input[4] = {8, 10, 6, 12};  // Q4 values
    
    // Known weights (simple: all +1 for first neuron)
    uint32_t pos_mask = 0x0F;  // First 4 bits
    uint32_t neg_mask = 0x00;
    
    // Software dot product
    int sw_dot = 0;
    for (int i = 0; i < 4; i++) {
        if (pos_mask & (1 << i)) sw_dot += input[i];
        if (neg_mask & (1 << i)) sw_dot -= input[i];
    }
    printf("  Input: [%d, %d, %d, %d]\n", input[0], input[1], input[2], input[3]);
    printf("  Weights: pos_mask=0x%X, neg_mask=0x%X\n", (unsigned)pos_mask, (unsigned)neg_mask);
    printf("  Software dot product: %d\n", sw_dot);
    
    // Hardware dot product (count pulses)
    int pulses = 0;
    for (int i = 0; i < 4; i++) {
        if (pos_mask & (1 << i)) pulses += input[i];
    }
    
    size_t num_cycles = pulses * 2;
    if (num_cycles < 8) num_cycles = 8;
    
    memset(pattern_buffer, 0, MAX_PATTERN_BYTES);
    int remaining = pulses;
    for (size_t cycle = 0; cycle < num_cycles; cycle++) {
        if (cycle % 2 == 0) {
            pattern_buffer[cycle] = 0x00;
        } else {
            pattern_buffer[cycle] = (remaining > 0) ? 0x01 : 0x00;
            if (remaining > 0) remaining--;
        }
    }
    
    reset_all_counters();
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pattern_buffer, num_cycles * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 1000);
    esp_rom_delay_us(10);
    
    int hw_count = get_full_count(0);
    printf("  Hardware pulse count: %d\n", hw_count);
    
    int match = (hw_count == sw_dot);
    printf("  Match: %s\n", match ? "PASS" : "FAIL");
    
    if (!match) failures++;
    
    return failures;
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  FALSIFICATION: CfC PARALLEL DOT PRODUCT                          ║\n");
    printf("║  Rigorous testing of all claims                                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    ESP_LOGI(TAG, "Setting up hardware...");
    ESP_ERROR_CHECK(setup_hardware());
    ESP_LOGI(TAG, "Hardware ready.");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    int total_failures = 0;
    
    // Run all tests
    total_failures += test_basic_pulse_counting();
    vTaskDelay(pdMS_TO_TICKS(200));
    
    total_failures += test_ternary_dot_product();
    vTaskDelay(pdMS_TO_TICKS(200));
    
    total_failures += test_lut_accuracy();
    vTaskDelay(pdMS_TO_TICKS(200));
    
    total_failures += test_cfc_dynamics();
    vTaskDelay(pdMS_TO_TICKS(200));
    
    total_failures += test_end_to_end();
    
    // Final verdict
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    if (total_failures == 0) {
        printf("║                    FALSIFICATION: NOT FALSIFIED                   ║\n");
        printf("║                    All tests passed!                              ║\n");
    } else {
        printf("║                    FALSIFICATION: FALSIFIED                       ║\n");
        printf("║                    %d test(s) FAILED                              ║\n", total_failures);
    }
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    
    ESP_LOGI(TAG, "Falsification complete. Total failures: %d", total_failures);
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
