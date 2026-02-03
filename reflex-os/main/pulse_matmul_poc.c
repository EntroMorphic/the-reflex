/**
 * pulse_matmul_poc.c - Pulse-Mode Matrix Multiplication PoC
 *
 * Phase 1: Component Verification
 *   - PARLIO 4-bit parallel output (4 GPIOs simultaneously)
 *   - 4 PCNT units counting pulse trains
 *   - PCNT increment/decrement modes for ternary weights
 *
 * Architecture:
 *   PARLIO (4-bit) ─┬─► GPIO4 ──► PCNT0 (accumulates y0)
 *                   ├─► GPIO5 ──► PCNT1 (accumulates y1)
 *                   ├─► GPIO6 ──► PCNT2 (accumulates y2)
 *                   └─► GPIO7 ──► PCNT3 (accumulates y3)
 *
 * For full matmul, we need external fan-out wiring:
 *   GPIO4 (input 0 pulses) ─┬─► PCNT0.ch0, PCNT1.ch0, PCNT2.ch0, PCNT3.ch0
 *   GPIO5 (input 1 pulses) ─┼─► PCNT0.ch1, PCNT1.ch1, PCNT2.ch1, PCNT3.ch1
 *   etc.
 *
 * This PoC first verifies the basic 1:1 mapping works.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/pulse_cnt.h"
#include "driver/parlio_tx.h"
#include "driver/gpio.h"

static const char *TAG = "MATMUL";

// ============================================================
// Configuration
// ============================================================

// PARLIO output GPIOs (directly wired to corresponding PCNT inputs)
#define GPIO_OUT_0      4   // Input vector element 0
#define GPIO_OUT_1      5   // Input vector element 1
#define GPIO_OUT_2      6   // Input vector element 2
#define GPIO_OUT_3      7   // Input vector element 3

// PARLIO clock - higher = faster matmul
// PCNT can handle up to 40 MHz, but we'll use 20 MHz for margin
#define PARLIO_CLK_HZ   20000000  // 20 MHz = 50ns per bit

// ============================================================
// Global Handles
// ============================================================

static parlio_tx_unit_handle_t parlio = NULL;
static pcnt_unit_handle_t pcnt[4] = {NULL};
static pcnt_channel_handle_t pcnt_chan[4] = {NULL};

// DMA buffer for pulse patterns (must be 4-byte aligned)
// With 4-bit data width, each byte contains 2 samples (LSB nibble first)
// We need 256 samples, so 128 bytes
// But let's use 512 samples (256 bytes) to have headroom
static uint8_t __attribute__((aligned(4))) pulse_buffer[256];

// ============================================================
// Pulse Pattern Generation
// ============================================================

/**
 * Generate pulse pattern buffer for input vector.
 * 
 * To generate V pulses (rising edges), we need to alternate low/high V times.
 * Pattern for each input channel:
 *   pulse 0: LOW, HIGH   (1 rising edge)
 *   pulse 1: LOW, HIGH   (1 rising edge)
 *   ...
 *   pulse V-1: LOW, HIGH (1 rising edge)
 *   remaining: LOW, LOW  (no more edges)
 *
 * With 4-bit data width and LSB pack order:
 * - Each byte contains 2 samples (nibbles)
 * - Lower nibble (bits 0-3) = first sample
 * - Upper nibble (bits 4-7) = second sample
 *
 * For 256 pulses max, we need 512 samples (256 LOW-HIGH pairs).
 * 512 samples = 256 bytes.
 */
static void generate_pulses(const uint8_t inputs[4], uint8_t* buffer) {
    // Generate 512 samples (256 LOW-HIGH pairs) packed into 256 bytes
    for (int pulse = 0; pulse < 256; pulse++) {
        uint8_t sample_low = 0;   // First half of pulse (LOW)
        uint8_t sample_high = 0;  // Second half of pulse (HIGH)
        
        for (int i = 0; i < 4; i++) {
            if (pulse < inputs[i]) {
                // This pulse is active for this input
                // sample_low stays 0 (low)
                sample_high |= (1 << i);  // Goes high = rising edge
            }
            // If pulse >= inputs[i], both stay 0 (no edge)
        }
        
        // Pack LOW-HIGH pair into one byte
        buffer[pulse] = sample_low | (sample_high << 4);
    }
}

// ============================================================
// Hardware Setup
// ============================================================

static esp_err_t setup_parlio(void) {
    ESP_LOGI(TAG, "Setting up PARLIO with 4-bit parallel output...");
    
    parlio_tx_unit_config_t cfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = PARLIO_CLK_HZ,
        .data_width = 4,  // 4 parallel data lines!
        .clk_out_gpio_num = -1,  // No clock output needed
        .valid_gpio_num = -1,
        .trans_queue_depth = 4,
        .max_transfer_size = 1024,  // Need 256 bytes * 2 for double buffering
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB,
        .flags = { .io_loop_back = 1 },  // Enable loopback for internal routing
    };
    
    // Assign GPIOs: data[0..3] = GPIO4..7, rest = -1
    for (int i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; i++) {
        if (i < 4) {
            cfg.data_gpio_nums[i] = GPIO_OUT_0 + i;
        } else {
            cfg.data_gpio_nums[i] = -1;
        }
    }
    
    esp_err_t ret = parlio_new_tx_unit(&cfg, &parlio);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PARLIO TX unit: %s", esp_err_to_name(ret));
        return ret;
    }
    
    parlio_tx_unit_enable(parlio);
    
    ESP_LOGI(TAG, "PARLIO configured: 4-bit output on GPIO %d-%d @ %d Hz",
             GPIO_OUT_0, GPIO_OUT_3, PARLIO_CLK_HZ);
    return ESP_OK;
}

static esp_err_t setup_pcnt_unit(int unit_idx, int gpio) {
    esp_err_t ret;
    
    // Create PCNT unit
    pcnt_unit_config_t cfg = {
        .low_limit = -32768,
        .high_limit = 32767,
    };
    ret = pcnt_new_unit(&cfg, &pcnt[unit_idx]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PCNT unit %d: %s", unit_idx, esp_err_to_name(ret));
        return ret;
    }
    
    // Create channel on this unit
    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num = gpio,
        .level_gpio_num = -1,
    };
    ret = pcnt_new_channel(pcnt[unit_idx], &chan_cfg, &pcnt_chan[unit_idx]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PCNT channel %d: %s", unit_idx, esp_err_to_name(ret));
        return ret;
    }
    
    // Default: count rising edges (weight = +1)
    pcnt_channel_set_edge_action(pcnt_chan[unit_idx],
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,  // Rising edge = +1
        PCNT_CHANNEL_EDGE_ACTION_HOLD);     // Falling edge = ignore
    
    pcnt_unit_enable(pcnt[unit_idx]);
    pcnt_unit_start(pcnt[unit_idx]);
    
    return ESP_OK;
}

static esp_err_t setup_pcnt_all(void) {
    ESP_LOGI(TAG, "Setting up 4 PCNT units...");
    
    int gpios[4] = {GPIO_OUT_0, GPIO_OUT_1, GPIO_OUT_2, GPIO_OUT_3};
    
    for (int i = 0; i < 4; i++) {
        esp_err_t ret = setup_pcnt_unit(i, gpios[i]);
        if (ret != ESP_OK) return ret;
        ESP_LOGI(TAG, "  PCNT%d: GPIO%d (weight=+1)", i, gpios[i]);
    }
    
    return ESP_OK;
}

/**
 * Set weight for a PCNT unit.
 * weight: +1 = increment on rising edge
 *         -1 = decrement on rising edge
 *          0 = hold (don't count)
 */
static void set_pcnt_weight(int unit_idx, int8_t weight) {
    pcnt_channel_edge_action_t action;
    
    switch (weight) {
        case 1:
            action = PCNT_CHANNEL_EDGE_ACTION_INCREASE;
            break;
        case -1:
            action = PCNT_CHANNEL_EDGE_ACTION_DECREASE;
            break;
        default:  // 0
            action = PCNT_CHANNEL_EDGE_ACTION_HOLD;
            break;
    }
    
    pcnt_channel_set_edge_action(pcnt_chan[unit_idx], action, PCNT_CHANNEL_EDGE_ACTION_HOLD);
}

static void clear_all_pcnt(void) {
    for (int i = 0; i < 4; i++) {
        pcnt_unit_clear_count(pcnt[i]);
    }
}

static void read_all_pcnt(int16_t outputs[4]) {
    for (int i = 0; i < 4; i++) {
        int count;
        pcnt_unit_get_count(pcnt[i], &count);
        outputs[i] = (int16_t)count;
    }
}

// ============================================================
// Matmul Compute
// ============================================================

/**
 * Perform pulse-mode matrix-vector multiplication.
 * 
 * For this PoC (1:1 mapping without external wiring):
 *   outputs[i] = inputs[i] * weight[i]
 *
 * With external fan-out wiring:
 *   outputs[i] = sum_j(inputs[j] * weights[i][j])
 */
static esp_err_t pulse_matmul_compute(const uint8_t inputs[4], 
                                       const int8_t weights[4],
                                       int16_t outputs[4]) {
    // Set weights
    for (int i = 0; i < 4; i++) {
        set_pcnt_weight(i, weights[i]);
    }
    
    // Clear counters
    clear_all_pcnt();
    
    // Generate pulse pattern
    generate_pulses(inputs, pulse_buffer);
    
    // Transmit pulses
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    // 256 bytes * 8 bits = 2048 bits = 512 samples (256 pulse pairs)
    esp_err_t ret = parlio_tx_unit_transmit(parlio, pulse_buffer, 256 * 8, &tx_cfg);
    if (ret != ESP_OK) return ret;
    
    // Wait for completion
    parlio_tx_unit_wait_all_done(parlio, 100);
    
    // Read results
    read_all_pcnt(outputs);
    
    return ESP_OK;
}

// ============================================================
// CPU Reference Implementation
// ============================================================

static void cpu_matmul_reference(const uint8_t inputs[4],
                                  const int8_t weights[4],
                                  int16_t expected[4]) {
    for (int i = 0; i < 4; i++) {
        expected[i] = (int16_t)inputs[i] * weights[i];
    }
}

// ============================================================
// Tests
// ============================================================

static void test_parallel_output(void) {
    printf("\n=== TEST 1: PARLIO 4-bit Parallel Output ===\n");
    
    // Set all weights to +1
    for (int i = 0; i < 4; i++) {
        set_pcnt_weight(i, 1);
    }
    clear_all_pcnt();
    
    // Test input: [100, 50, 200, 25]
    uint8_t inputs[4] = {100, 50, 200, 25};
    generate_pulses(inputs, pulse_buffer);
    
    printf("Inputs: [%d, %d, %d, %d]\n", inputs[0], inputs[1], inputs[2], inputs[3]);
    printf("Expected PCNT counts: [%d, %d, %d, %d]\n", inputs[0], inputs[1], inputs[2], inputs[3]);
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    // 256 bytes * 8 bits = 2048 bits = 512 samples (256 pulse pairs)
    parlio_tx_unit_transmit(parlio, pulse_buffer, 256 * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 100);
    
    int16_t outputs[4];
    read_all_pcnt(outputs);
    
    printf("Actual PCNT counts:   [%d, %d, %d, %d]\n", outputs[0], outputs[1], outputs[2], outputs[3]);
    
    int pass = 1;
    for (int i = 0; i < 4; i++) {
        if (outputs[i] != inputs[i]) pass = 0;
    }
    printf("Result: %s\n", pass ? "PASS" : "FAIL");
}

static void test_weight_modes(void) {
    printf("\n=== TEST 2: PCNT Weight Modes (inc/dec/hold) ===\n");
    
    // Fixed input: all 100
    uint8_t inputs[4] = {100, 100, 100, 100};
    
    // Weights: +1, -1, 0, +1
    int8_t weights[4] = {1, -1, 0, 1};
    
    int16_t outputs[4];
    int16_t expected[4];
    
    pulse_matmul_compute(inputs, weights, outputs);
    cpu_matmul_reference(inputs, weights, expected);
    
    printf("Inputs:   [%d, %d, %d, %d]\n", inputs[0], inputs[1], inputs[2], inputs[3]);
    printf("Weights:  [%+d, %+d, %+d, %+d]\n", weights[0], weights[1], weights[2], weights[3]);
    printf("Expected: [%d, %d, %d, %d]\n", expected[0], expected[1], expected[2], expected[3]);
    printf("Actual:   [%d, %d, %d, %d]\n", outputs[0], outputs[1], outputs[2], outputs[3]);
    
    int pass = 1;
    for (int i = 0; i < 4; i++) {
        if (outputs[i] != expected[i]) pass = 0;
    }
    printf("Result: %s\n", pass ? "PASS" : "FAIL");
}

static void test_throughput(void) {
    printf("\n=== TEST 3: Throughput Measurement ===\n");
    
    int8_t weights[4] = {1, 1, 1, 1};
    int16_t outputs[4];
    int num_iters = 1000;
    
    // Test with typical NN activation values (max 64)
    uint8_t inputs_small[4] = {32, 48, 16, 64};
    
    int64_t start = esp_timer_get_time();
    for (int i = 0; i < num_iters; i++) {
        pulse_matmul_compute(inputs_small, weights, outputs);
    }
    int64_t end = esp_timer_get_time();
    int64_t elapsed_us = end - start;
    
    double matmuls_per_sec = (double)num_iters * 1000000.0 / (double)elapsed_us;
    double us_per_matmul = (double)elapsed_us / (double)num_iters;
    
    printf("Small inputs [32,48,16,64]:\n");
    printf("  Time/matmul: %.2f us, Throughput: %.0f/sec\n", us_per_matmul, matmuls_per_sec);
    
    // Test with larger inputs
    uint8_t inputs_large[4] = {128, 192, 64, 255};
    
    start = esp_timer_get_time();
    for (int i = 0; i < num_iters; i++) {
        pulse_matmul_compute(inputs_large, weights, outputs);
    }
    end = esp_timer_get_time();
    elapsed_us = end - start;
    
    matmuls_per_sec = (double)num_iters * 1000000.0 / (double)elapsed_us;
    us_per_matmul = (double)elapsed_us / (double)num_iters;
    
    printf("Large inputs [128,192,64,255]:\n");
    printf("  Time/matmul: %.2f us, Throughput: %.0f/sec\n", us_per_matmul, matmuls_per_sec);
    printf("Target: >100K matmuls/sec\n");
    printf("Result: %s\n", matmuls_per_sec > 100000 ? "PASS" : "NEEDS OPTIMIZATION");
}

static void test_accuracy_sweep(void) {
    printf("\n=== TEST 4: Accuracy Sweep (all weight combos) ===\n");
    
    int total_tests = 0;
    int passed_tests = 0;
    
    // Test all 81 weight combinations (3^4)
    int8_t weight_vals[3] = {-1, 0, 1};
    
    for (int w0 = 0; w0 < 3; w0++) {
        for (int w1 = 0; w1 < 3; w1++) {
            for (int w2 = 0; w2 < 3; w2++) {
                for (int w3 = 0; w3 < 3; w3++) {
                    int8_t weights[4] = {weight_vals[w0], weight_vals[w1], 
                                          weight_vals[w2], weight_vals[w3]};
                    
                    // Test with random-ish inputs
                    uint8_t inputs[4] = {100, 77, 200, 33};
                    
                    int16_t outputs[4];
                    int16_t expected[4];
                    
                    pulse_matmul_compute(inputs, weights, outputs);
                    cpu_matmul_reference(inputs, weights, expected);
                    
                    int pass = 1;
                    for (int i = 0; i < 4; i++) {
                        if (outputs[i] != expected[i]) pass = 0;
                    }
                    
                    total_tests++;
                    if (pass) passed_tests++;
                }
            }
        }
    }
    
    printf("Tested: %d weight combinations\n", total_tests);
    printf("Passed: %d/%d\n", passed_tests, total_tests);
    printf("Accuracy: %.1f%%\n", 100.0 * passed_tests / total_tests);
    printf("Result: %s\n", (passed_tests == total_tests) ? "PASS" : "FAIL");
}

static void test_max_edge_rate(void) {
    printf("\n=== TEST 5: Maximum Edge Detection Rate ===\n");
    
    // Generate max pulses (255 per channel)
    uint8_t inputs[4] = {255, 255, 255, 255};
    int8_t weights[4] = {1, 1, 1, 1};
    int16_t outputs[4];
    
    // Measure single operation at max load
    int64_t start = esp_timer_get_time();
    pulse_matmul_compute(inputs, weights, outputs);
    int64_t end = esp_timer_get_time();
    
    printf("Max input: [255, 255, 255, 255]\n");
    printf("Output: [%d, %d, %d, %d]\n", outputs[0], outputs[1], outputs[2], outputs[3]);
    printf("Time: %lld us\n", end - start);
    
    // Calculate effective edge rate
    // 255 pulses * 4 channels = 1020 edges total
    // Each pulse = 2 edges (rising + falling), but we only count rising
    double edges = 255.0 * 4;
    double edge_rate_mhz = edges / ((double)(end - start));
    
    printf("Edge rate: %.2f MHz\n", edge_rate_mhz);
    printf("PARLIO clock: %.2f MHz\n", PARLIO_CLK_HZ / 1000000.0);
    
    int pass = (outputs[0] == 255 && outputs[1] == 255 && 
                outputs[2] == 255 && outputs[3] == 255);
    printf("Result: %s\n", pass ? "PASS" : "FAIL - edges missed");
}

// ============================================================
// Main Entry
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                                                               ║\n");
    printf("║   PULSE-MODE MATRIX MULTIPLICATION PoC                        ║\n");
    printf("║                                                               ║\n");
    printf("║   Phase 1: Component Verification                             ║\n");
    printf("║   - PARLIO 4-bit parallel output                              ║\n");
    printf("║   - 4 PCNT units with weight encoding                         ║\n");
    printf("║   - Edge detection rate verification                          ║\n");
    printf("║                                                               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    esp_err_t ret;
    
    printf("Initializing hardware...\n");
    fflush(stdout);
    
    ret = setup_parlio();
    if (ret != ESP_OK) {
        printf("PARLIO setup failed!\n");
        return;
    }
    
    ret = setup_pcnt_all();
    if (ret != ESP_OK) {
        printf("PCNT setup failed!\n");
        return;
    }
    
    printf("Hardware ready!\n\n");
    fflush(stdout);
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Run tests
    test_parallel_output();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    test_weight_modes();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    test_accuracy_sweep();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    test_max_edge_rate();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    test_throughput();
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                                                               ║\n");
    printf("║   Phase 1 Complete!                                           ║\n");
    printf("║                                                               ║\n");
    printf("║   Current: 1:1 mapping (each PCNT reads one GPIO)             ║\n");
    printf("║   outputs[i] = inputs[i] * weight[i]                          ║\n");
    printf("║                                                               ║\n");
    printf("║   Next: External fan-out wiring for full matmul               ║\n");
    printf("║   outputs[i] = sum_j(inputs[j] * weights[i][j])               ║\n");
    printf("║                                                               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
