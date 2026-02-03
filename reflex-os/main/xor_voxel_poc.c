/**
 * xor_voxel_poc.c - XOR Voxel Cube Proof of Concept
 *
 * Demonstrates that dual-channel PCNT can detect phase relationships
 * between pulse trains, implementing the k=3 (interference) layer
 * of the 4³ voxel cube.
 *
 * Architecture:
 *   PARLIO (4-bit) ─┬─► GPIO4 ──┬─► PCNT0.ch0 (primary)
 *                   │           └─► PCNT3.ch1 (interference)
 *                   │
 *                   ├─► GPIO5 ──┬─► PCNT1.ch0 (primary)
 *                   │           └─► PCNT0.ch1 (interference)
 *                   │
 *                   ├─► GPIO6 ──┬─► PCNT2.ch0 (primary)
 *                   │           └─► PCNT1.ch1 (interference)
 *                   │
 *                   └─► GPIO7 ──┬─► PCNT3.ch0 (primary)
 *                               └─► PCNT2.ch1 (interference)
 *
 * This creates a RING TOPOLOGY where each PCNT computes:
 *   count = (primary pulses) - (neighbor pulses)
 *
 * XOR-like behavior:
 *   - If primary and neighbor are equal: count ≈ 0 (agreement)
 *   - If only primary active: count = +N (primary dominates)
 *   - If only neighbor active: count = -N (neighbor dominates)
 *   - Mixed: partial interference
 *
 * The 4³ = 64 voxels:
 *   k=0: Weight +1 (increment on primary)
 *   k=1: Weight -1 (decrement on primary) 
 *   k=2: Weight 0  (hold)
 *   k=3: XOR (difference between primary and secondary)
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/pulse_cnt.h"
#include "driver/parlio_tx.h"
#include "driver/gpio.h"

static const char *TAG = "VOXEL";

// ============================================================
// Configuration
// ============================================================

// PARLIO output GPIOs
#define GPIO_OUT_0      4   // Input vector element 0
#define GPIO_OUT_1      5   // Input vector element 1
#define GPIO_OUT_2      6   // Input vector element 2
#define GPIO_OUT_3      7   // Input vector element 3

#define PARLIO_CLK_HZ   10000000  // 10 MHz

// Ring topology: PCNT[n].ch0 = GPIO[n], PCNT[n].ch1 = GPIO[(n+1)%4]
static const int PRIMARY_GPIO[4]   = {4, 5, 6, 7};
static const int SECONDARY_GPIO[4] = {5, 6, 7, 4};  // Ring: 4←5, 5←6, 6←7, 7←4

// ============================================================
// Global Handles
// ============================================================

static parlio_tx_unit_handle_t parlio = NULL;
static pcnt_unit_handle_t pcnt[4] = {NULL};
static pcnt_channel_handle_t pcnt_ch0[4] = {NULL};  // Primary channel
static pcnt_channel_handle_t pcnt_ch1[4] = {NULL};  // Interference channel

// DMA buffer for pulse patterns
static uint8_t __attribute__((aligned(4))) pulse_buffer[256];

// ============================================================
// Pulse Generation
// ============================================================

/**
 * Generate pulse pattern for input vector.
 * Same as pulse_matmul_poc.c
 */
static void generate_pulses(const uint8_t inputs[4], uint8_t* buffer) {
    for (int pulse = 0; pulse < 256; pulse++) {
        uint8_t sample_low = 0;
        uint8_t sample_high = 0;
        
        for (int i = 0; i < 4; i++) {
            if (pulse < inputs[i]) {
                sample_high |= (1 << i);
            }
        }
        
        buffer[pulse] = sample_low | (sample_high << 4);
    }
}

// ============================================================
// Hardware Setup
// ============================================================

static esp_err_t setup_parlio(void) {
    ESP_LOGI(TAG, "Setting up PARLIO 4-bit output...");
    
    parlio_tx_unit_config_t cfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = PARLIO_CLK_HZ,
        .data_width = 4,
        .clk_out_gpio_num = -1,
        .valid_gpio_num = -1,
        .trans_queue_depth = 4,
        .max_transfer_size = 1024,
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB,
        .flags = { .io_loop_back = 1 },
    };
    
    for (int i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; i++) {
        cfg.data_gpio_nums[i] = (i < 4) ? (GPIO_OUT_0 + i) : -1;
    }
    
    esp_err_t ret = parlio_new_tx_unit(&cfg, &parlio);
    if (ret != ESP_OK) return ret;
    
    parlio_tx_unit_enable(parlio);
    
    ESP_LOGI(TAG, "PARLIO ready on GPIO %d-%d", GPIO_OUT_0, GPIO_OUT_3);
    return ESP_OK;
}

/**
 * Setup PCNT with DUAL channels for XOR detection.
 * 
 * Channel 0: Primary input (INCREMENT)
 * Channel 1: Secondary input (DECREMENT)
 * 
 * Net count = primary_pulses - secondary_pulses
 */
static esp_err_t setup_dual_pcnt(int unit_idx) {
    esp_err_t ret;
    
    int gpio_primary = PRIMARY_GPIO[unit_idx];
    int gpio_secondary = SECONDARY_GPIO[unit_idx];
    
    ESP_LOGI(TAG, "PCNT%d: ch0=GPIO%d (inc), ch1=GPIO%d (dec)", 
             unit_idx, gpio_primary, gpio_secondary);
    
    // Create unit
    pcnt_unit_config_t cfg = {
        .low_limit = -32768,
        .high_limit = 32767,
    };
    ret = pcnt_new_unit(&cfg, &pcnt[unit_idx]);
    if (ret != ESP_OK) return ret;
    
    // Channel 0: Primary input, INCREMENT on rising edge
    pcnt_chan_config_t ch0_cfg = {
        .edge_gpio_num = gpio_primary,
        .level_gpio_num = -1,
    };
    ret = pcnt_new_channel(pcnt[unit_idx], &ch0_cfg, &pcnt_ch0[unit_idx]);
    if (ret != ESP_OK) return ret;
    
    pcnt_channel_set_edge_action(pcnt_ch0[unit_idx],
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,  // Rising edge: +1
        PCNT_CHANNEL_EDGE_ACTION_HOLD);     // Falling edge: hold
    
    // Channel 1: Secondary input, DECREMENT on rising edge
    pcnt_chan_config_t ch1_cfg = {
        .edge_gpio_num = gpio_secondary,
        .level_gpio_num = -1,
    };
    ret = pcnt_new_channel(pcnt[unit_idx], &ch1_cfg, &pcnt_ch1[unit_idx]);
    if (ret != ESP_OK) return ret;
    
    pcnt_channel_set_edge_action(pcnt_ch1[unit_idx],
        PCNT_CHANNEL_EDGE_ACTION_DECREASE,  // Rising edge: -1
        PCNT_CHANNEL_EDGE_ACTION_HOLD);     // Falling edge: hold
    
    pcnt_unit_enable(pcnt[unit_idx]);
    pcnt_unit_start(pcnt[unit_idx]);
    
    return ESP_OK;
}

static esp_err_t setup_all_pcnt(void) {
    ESP_LOGI(TAG, "Setting up 4 dual-channel PCNTs (ring topology)...");
    
    for (int i = 0; i < 4; i++) {
        esp_err_t ret = setup_dual_pcnt(i);
        if (ret != ESP_OK) return ret;
    }
    
    ESP_LOGI(TAG, "Ring: PCNT0(4-5), PCNT1(5-6), PCNT2(6-7), PCNT3(7-4)");
    return ESP_OK;
}

static void clear_all_pcnt(void) {
    for (int i = 0; i < 4; i++) {
        pcnt_unit_clear_count(pcnt[i]);
    }
}

static void read_all_pcnt(int16_t counts[4]) {
    for (int i = 0; i < 4; i++) {
        int count;
        pcnt_unit_get_count(pcnt[i], &count);
        counts[i] = (int16_t)count;
    }
}

// ============================================================
// XOR Interpretation
// ============================================================

/**
 * Interpret count as XOR proxy.
 * 
 * count = primary - secondary
 * XOR ≈ |count| / max when inputs differ
 * XOR ≈ 0 when inputs equal
 */
static float xor_proxy(int16_t count, int max_pulses) {
    if (max_pulses == 0) return 0.0f;
    return fabsf((float)count / (float)max_pulses);
}

/**
 * Compute expected count for verification.
 * count = input[primary] - input[secondary]
 */
static int16_t expected_count(const uint8_t inputs[4], int pcnt_idx) {
    int primary_idx = pcnt_idx;
    int secondary_idx = (pcnt_idx + 1) % 4;  // Ring topology
    return (int16_t)inputs[primary_idx] - (int16_t)inputs[secondary_idx];
}

// ============================================================
// Tests
// ============================================================

static void test_equal_inputs(void) {
    printf("\n=== TEST 1: Equal Inputs (XOR = 0) ===\n");
    
    // All inputs equal → all counts should be ~0
    uint8_t inputs[4] = {100, 100, 100, 100};
    
    clear_all_pcnt();
    generate_pulses(inputs, pulse_buffer);
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pulse_buffer, 256 * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 100);
    
    int16_t counts[4];
    read_all_pcnt(counts);
    
    printf("Inputs: [%d, %d, %d, %d] (all equal)\n", 
           inputs[0], inputs[1], inputs[2], inputs[3]);
    printf("Counts: [%d, %d, %d, %d]\n", counts[0], counts[1], counts[2], counts[3]);
    printf("XOR proxy: [%.2f, %.2f, %.2f, %.2f]\n",
           xor_proxy(counts[0], 100), xor_proxy(counts[1], 100),
           xor_proxy(counts[2], 100), xor_proxy(counts[3], 100));
    
    int pass = 1;
    for (int i = 0; i < 4; i++) {
        if (abs(counts[i]) > 5) pass = 0;  // Allow small tolerance
    }
    printf("Result: %s (expect all counts ≈ 0)\n", pass ? "PASS" : "FAIL");
}

static void test_exclusive_inputs(void) {
    printf("\n=== TEST 2: Exclusive Inputs (XOR = 1) ===\n");
    
    // Alternating: 100, 0, 100, 0 → differences should be ±100
    uint8_t inputs[4] = {100, 0, 100, 0};
    
    clear_all_pcnt();
    generate_pulses(inputs, pulse_buffer);
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pulse_buffer, 256 * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 100);
    
    int16_t counts[4];
    read_all_pcnt(counts);
    
    printf("Inputs: [%d, %d, %d, %d] (alternating)\n",
           inputs[0], inputs[1], inputs[2], inputs[3]);
    printf("Expected: [%d, %d, %d, %d]\n",
           expected_count(inputs, 0), expected_count(inputs, 1),
           expected_count(inputs, 2), expected_count(inputs, 3));
    printf("Actual:   [%d, %d, %d, %d]\n", counts[0], counts[1], counts[2], counts[3]);
    printf("XOR proxy: [%.2f, %.2f, %.2f, %.2f]\n",
           xor_proxy(counts[0], 100), xor_proxy(counts[1], 100),
           xor_proxy(counts[2], 100), xor_proxy(counts[3], 100));
    
    int pass = 1;
    for (int i = 0; i < 4; i++) {
        if (counts[i] != expected_count(inputs, i)) pass = 0;
    }
    printf("Result: %s\n", pass ? "PASS" : "FAIL");
}

static void test_gradient_inputs(void) {
    printf("\n=== TEST 3: Gradient Inputs ===\n");
    
    // Linear gradient: 50, 100, 150, 200
    uint8_t inputs[4] = {50, 100, 150, 200};
    
    clear_all_pcnt();
    generate_pulses(inputs, pulse_buffer);
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pulse_buffer, 256 * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 100);
    
    int16_t counts[4];
    read_all_pcnt(counts);
    
    printf("Inputs: [%d, %d, %d, %d] (gradient)\n",
           inputs[0], inputs[1], inputs[2], inputs[3]);
    printf("Expected: [%d, %d, %d, %d]\n",
           expected_count(inputs, 0), expected_count(inputs, 1),
           expected_count(inputs, 2), expected_count(inputs, 3));
    printf("Actual:   [%d, %d, %d, %d]\n", counts[0], counts[1], counts[2], counts[3]);
    
    // Expected: 50-100=-50, 100-150=-50, 150-200=-50, 200-50=+150
    int pass = 1;
    for (int i = 0; i < 4; i++) {
        if (counts[i] != expected_count(inputs, i)) pass = 0;
    }
    printf("Result: %s\n", pass ? "PASS" : "FAIL");
}

static void test_single_active(void) {
    printf("\n=== TEST 4: Single Input Active ===\n");
    
    // Only input 0 active
    uint8_t inputs[4] = {200, 0, 0, 0};
    
    clear_all_pcnt();
    generate_pulses(inputs, pulse_buffer);
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pulse_buffer, 256 * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 100);
    
    int16_t counts[4];
    read_all_pcnt(counts);
    
    printf("Inputs: [%d, %d, %d, %d] (only input 0)\n",
           inputs[0], inputs[1], inputs[2], inputs[3]);
    printf("Expected: [%d, %d, %d, %d]\n",
           expected_count(inputs, 0), expected_count(inputs, 1),
           expected_count(inputs, 2), expected_count(inputs, 3));
    printf("Actual:   [%d, %d, %d, %d]\n", counts[0], counts[1], counts[2], counts[3]);
    
    // PCNT0: 200-0=+200, PCNT1: 0-0=0, PCNT2: 0-0=0, PCNT3: 0-200=-200
    int pass = 1;
    for (int i = 0; i < 4; i++) {
        if (counts[i] != expected_count(inputs, i)) pass = 0;
    }
    printf("Result: %s\n", pass ? "PASS" : "FAIL");
}

static void test_voxel_interpretation(void) {
    printf("\n=== TEST 5: Full Voxel Cube Interpretation ===\n");
    
    // Random-ish inputs
    uint8_t inputs[4] = {80, 120, 40, 180};
    
    clear_all_pcnt();
    generate_pulses(inputs, pulse_buffer);
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pulse_buffer, 256 * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 100);
    
    int16_t counts[4];
    read_all_pcnt(counts);
    
    printf("Inputs: [%d, %d, %d, %d]\n", inputs[0], inputs[1], inputs[2], inputs[3]);
    printf("\nVoxel Cube Readout:\n");
    
    for (int j = 0; j < 4; j++) {
        // Primary (k=0): just the input value
        int16_t primary = inputs[j];
        // Interference (k=3): the count encodes XOR-like behavior
        int16_t interference = counts[j];
        float xor_val = xor_proxy(interference, 
                                   (inputs[j] > inputs[(j+1)%4]) ? inputs[j] : inputs[(j+1)%4]);
        
        printf("  PCNT%d: primary=%+4d, interference=%+4d, XOR=%.2f\n",
               j, primary, interference, xor_val);
    }
    
    // Verify counts match expected
    int pass = 1;
    for (int i = 0; i < 4; i++) {
        if (counts[i] != expected_count(inputs, i)) pass = 0;
    }
    printf("\nResult: %s\n", pass ? "PASS" : "FAIL");
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                                                               ║\n");
    printf("║   XOR VOXEL CUBE - 4^3 = 64 Voxel Proof of Concept            ║\n");
    printf("║                                                               ║\n");
    printf("║   Dual-channel PCNT for interference detection                ║\n");
    printf("║   Ring topology: each PCNT sees two adjacent inputs           ║\n");
    printf("║                                                               ║\n");
    printf("║   k=0,1,2: Weight layers (+1, -1, 0)                          ║\n");
    printf("║   k=3: XOR/interference layer                                 ║\n");
    printf("║                                                               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    esp_err_t ret;
    
    printf("Initializing hardware...\n");
    
    ret = setup_parlio();
    if (ret != ESP_OK) {
        printf("PARLIO setup failed!\n");
        return;
    }
    
    ret = setup_all_pcnt();
    if (ret != ESP_OK) {
        printf("PCNT setup failed!\n");
        return;
    }
    
    printf("Hardware ready!\n");
    fflush(stdout);
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Run tests
    test_equal_inputs();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    test_exclusive_inputs();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    test_gradient_inputs();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    test_single_active();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    test_voxel_interpretation();
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                                                               ║\n");
    printf("║   XOR VOXEL SUMMARY                                           ║\n");
    printf("║                                                               ║\n");
    printf("║   Each PCNT computes: count = primary - secondary             ║\n");
    printf("║                                                               ║\n");
    printf("║   Interpretation:                                             ║\n");
    printf("║   - count ≈ 0: inputs in-phase (XOR = 0)                      ║\n");
    printf("║   - count > 0: primary dominates                              ║\n");
    printf("║   - count < 0: secondary dominates                            ║\n");
    printf("║   - |count|/max: XOR proxy (0 = agree, 1 = exclusive)         ║\n");
    printf("║                                                               ║\n");
    printf("║   This adds INTERFERENCE DETECTION to the ETM fabric:         ║\n");
    printf("║   Not just linear matmul, but RELATIONSHIPS between inputs    ║\n");
    printf("║                                                               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
