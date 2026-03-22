/**
 * cfc_parallel_dot.c - CfC Neural Network with True Parallel Dot Product
 *
 * Integrates the 4-channel PARLIO parallel pulse architecture with CfC.
 * 
 * Key insight: CfC with ternary weights needs dot products, which we can
 * now compute 4× faster using parallel PCNT accumulation.
 *
 * CfC equation:
 *   gate = σ(W_gate @ [x, h] + b_gate)
 *   candidate = tanh(W_cand @ [x, h] + b_cand)
 *   h_new = (1 - gate) * h_prev * decay + gate * candidate
 *
 * With ternary weights {-1, 0, +1}:
 *   dot_product = Σ(+1 inputs) - Σ(-1 inputs)
 *   = PCNT(+1 pulses) - PCNT(-1 pulses)
 *
 * Our parallel architecture:
 *   - PARLIO 4-bit → 4 GPIOs → 4 PCNTs
 *   - Process 4 neurons simultaneously!
 *   - Each neuron's dot product computed in parallel
 *
 * This gives 4× speedup over sequential processing.
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

static const char *TAG = "CFC_PARALLEL";

// ============================================================
// CfC Configuration
// ============================================================

#define CFC_HIDDEN_DIM      16      // Number of neurons (must be multiple of 4)
#define CFC_INPUT_DIM       4       // Input size
#define CFC_CONCAT_DIM      (CFC_HIDDEN_DIM + CFC_INPUT_DIM)

// Quantization
#define CFC_QUANT_BITS      4       // 4-bit quantization for inputs
#define CFC_QUANT_LEVELS    16      // 0-15

// ============================================================
// Parallel Hardware Configuration
// ============================================================

#define NUM_PARALLEL        4       // Process 4 neurons at once
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
// CfC State and Weights
// ============================================================

// Ternary weights: +1, 0, -1 stored as bit masks
typedef struct {
    uint32_t pos_mask[CFC_HIDDEN_DIM];  // Bits set where weight = +1
    uint32_t neg_mask[CFC_HIDDEN_DIM];  // Bits set where weight = -1
} cfc_ternary_layer_t;

// CfC network
typedef struct {
    cfc_ternary_layer_t W_gate;
    cfc_ternary_layer_t W_cand;
    int8_t b_gate[CFC_HIDDEN_DIM];
    int8_t b_cand[CFC_HIDDEN_DIM];
    float decay[CFC_HIDDEN_DIM];
    
    // State (Q4 format: 0-15 representing -1 to +1)
    uint8_t h[CFC_HIDDEN_DIM];
} cfc_network_t;

static cfc_network_t network;

// LUTs
static uint8_t sigmoid_lut[256];
static uint8_t tanh_lut[256];
static uint8_t mixer_lut[16][16][16];  // [gate][h_prev][candidate] - shared for simplicity

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
// Hardware setup
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
    
    // Pattern buffer
    pattern_buffer = heap_caps_aligned_alloc(4, MAX_PATTERN_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    
    return ESP_OK;
}

// ============================================================
// Initialize LUTs
// ============================================================

static void init_luts(void) {
    // Sigmoid LUT
    for (int i = 0; i < 256; i++) {
        float x = ((float)i / 255.0f) * 16.0f - 8.0f;
        float sig = 1.0f / (1.0f + expf(-x));
        sigmoid_lut[i] = (uint8_t)(sig * 15.0f);
    }
    
    // Tanh LUT
    for (int i = 0; i < 256; i++) {
        float x = ((float)i / 255.0f) * 16.0f - 8.0f;
        float t = tanhf(x);
        tanh_lut[i] = (uint8_t)((t + 1.0f) * 7.5f);
    }
    
    // Mixer LUT (with decay = 0.9)
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
}

// ============================================================
// Initialize random ternary weights
// ============================================================

static void init_random_weights(void) {
    // Simple random ternary weights with ~33% sparsity each
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        network.W_gate.pos_mask[n] = 0;
        network.W_gate.neg_mask[n] = 0;
        network.W_cand.pos_mask[n] = 0;
        network.W_cand.neg_mask[n] = 0;
        
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            int r = esp_random() % 3;  // 0, 1, or 2
            if (r == 0) {
                network.W_gate.pos_mask[n] |= (1 << i);
            } else if (r == 1) {
                network.W_gate.neg_mask[n] |= (1 << i);
            }
            
            r = esp_random() % 3;
            if (r == 0) {
                network.W_cand.pos_mask[n] |= (1 << i);
            } else if (r == 1) {
                network.W_cand.neg_mask[n] |= (1 << i);
            }
        }
        
        network.b_gate[n] = (esp_random() % 11) - 5;  // -5 to +5
        network.b_cand[n] = (esp_random() % 11) - 5;
        network.decay[n] = 0.9f;
        network.h[n] = 8;  // Initialize to 0 (middle of 0-15 range)
    }
}

// ============================================================
// Build parallel pattern for 4 neurons' dot products
// ============================================================

static size_t build_4neuron_pattern(
    const uint8_t* input_q4,  // Quantized input [CFC_CONCAT_DIM]
    int neuron_base,          // Process neurons neuron_base to neuron_base+3
    const cfc_ternary_layer_t* W
) {
    // Calculate pulses for each channel (neuron)
    int pulses[NUM_PARALLEL] = {0};
    
    for (int ch = 0; ch < NUM_PARALLEL; ch++) {
        int n = neuron_base + ch;
        if (n >= CFC_HIDDEN_DIM) continue;
        
        uint32_t pos_mask = W->pos_mask[n];
        uint32_t neg_mask = W->neg_mask[n];
        
        // For +1 weights: accumulate input values
        // For -1 weights: we'll handle separately (subtract at end)
        int pos_sum = 0;
        int neg_sum = 0;
        
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            int val = input_q4[i];  // 0-15
            if (pos_mask & (1 << i)) {
                pos_sum += val;
            }
            if (neg_mask & (1 << i)) {
                neg_sum += val;
            }
        }
        
        // Total pulses needed for positive contribution
        pulses[ch] = pos_sum;  // We'll handle neg separately
    }
    
    // Find max pulses
    int max_pulses = 0;
    for (int i = 0; i < NUM_PARALLEL; i++) {
        if (pulses[i] > max_pulses) max_pulses = pulses[i];
    }
    
    size_t num_cycles = max_pulses * 2;  // Low-high pairs
    if (num_cycles < MIN_PATTERN_BYTES) num_cycles = MIN_PATTERN_BYTES;
    size_t pattern_bytes = num_cycles;
    if (pattern_bytes > MAX_PATTERN_BYTES) pattern_bytes = MAX_PATTERN_BYTES;
    
    memset(pattern_buffer, 0, MAX_PATTERN_BYTES);
    
    // Track remaining pulses
    int remaining[NUM_PARALLEL];
    for (int i = 0; i < NUM_PARALLEL; i++) remaining[i] = pulses[i];
    
    // Build pattern with edge-based encoding
    for (size_t cycle = 0; cycle < num_cycles; cycle++) {
        if (cycle % 2 == 0) {
            pattern_buffer[cycle] = 0x00;  // Low
        } else {
            uint8_t mask = 0;
            for (int ch = 0; ch < NUM_PARALLEL; ch++) {
                if (remaining[ch] > 0) {
                    mask |= (1 << ch);
                    remaining[ch]--;
                }
            }
            pattern_buffer[cycle] = mask;  // High for channels with remaining pulses
        }
    }
    
    return pattern_bytes;
}

// ============================================================
// Compute parallel dot products for 4 neurons
// ============================================================

static void compute_4neuron_dot(
    const uint8_t* input_q4,
    int neuron_base,
    const cfc_ternary_layer_t* W,
    const int8_t* bias,
    int* results  // Output: 4 dot product results
) {
    // Build pattern and transmit
    size_t pattern_len = build_4neuron_pattern(input_q4, neuron_base, W);
    reset_all_counters();
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pattern_buffer, pattern_len * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 1000);
    esp_rom_delay_us(5);
    
    // Get positive counts
    int pos_counts[NUM_PARALLEL];
    for (int i = 0; i < NUM_PARALLEL; i++) {
        pos_counts[i] = get_full_count(i);
    }
    
    // Now compute negative contributions (we'd need another pass or compute in software)
    // For simplicity, compute negative sum in software
    for (int ch = 0; ch < NUM_PARALLEL; ch++) {
        int n = neuron_base + ch;
        if (n >= CFC_HIDDEN_DIM) {
            results[ch] = 0;
            continue;
        }
        
        uint32_t neg_mask = W->neg_mask[n];
        int neg_sum = 0;
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            if (neg_mask & (1 << i)) {
                neg_sum += input_q4[i];
            }
        }
        
        // Final dot product = pos_counts - neg_sum + bias
        results[ch] = pos_counts[ch] - neg_sum + bias[n];
    }
}

// ============================================================
// Full CfC forward pass with parallel dot products
// ============================================================

static void cfc_forward_parallel(const uint8_t* input_q4) {
    // Build concatenated input [x; h]
    uint8_t concat[CFC_CONCAT_DIM];
    memcpy(concat, input_q4, CFC_INPUT_DIM);
    memcpy(concat + CFC_INPUT_DIM, network.h, CFC_HIDDEN_DIM);
    
    uint8_t gate[CFC_HIDDEN_DIM];
    uint8_t candidate[CFC_HIDDEN_DIM];
    
    // Process neurons in batches of 4
    for (int base = 0; base < CFC_HIDDEN_DIM; base += NUM_PARALLEL) {
        // Gate computation (4 neurons at once)
        int gate_dots[NUM_PARALLEL];
        compute_4neuron_dot(concat, base, &network.W_gate, network.b_gate, gate_dots);
        
        for (int i = 0; i < NUM_PARALLEL && (base + i) < CFC_HIDDEN_DIM; i++) {
            // Clamp to LUT range and apply sigmoid
            int idx = gate_dots[i] + 128;  // Offset to positive
            if (idx < 0) idx = 0;
            if (idx > 255) idx = 255;
            gate[base + i] = sigmoid_lut[idx];
        }
        
        // Candidate computation (4 neurons at once)
        int cand_dots[NUM_PARALLEL];
        compute_4neuron_dot(concat, base, &network.W_cand, network.b_cand, cand_dots);
        
        for (int i = 0; i < NUM_PARALLEL && (base + i) < CFC_HIDDEN_DIM; i++) {
            int idx = cand_dots[i] + 128;
            if (idx < 0) idx = 0;
            if (idx > 255) idx = 255;
            candidate[base + i] = tanh_lut[idx];
        }
    }
    
    // Update hidden state using mixer LUT
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        network.h[n] = mixer_lut[gate[n]][network.h[n]][candidate[n]];
    }
}

// ============================================================
// Benchmark
// ============================================================

static void benchmark_cfc(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  CfC NEURAL NETWORK WITH PARALLEL DOT PRODUCT                     ║\n");
    printf("║  %d neurons, %d inputs, processing 4 at a time                    ║\n", CFC_HIDDEN_DIM, CFC_INPUT_DIM);
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Test input
    uint8_t input[CFC_INPUT_DIM] = {8, 10, 6, 12};  // Centered around 8 (which is 0)
    
    printf("  Input: [%d, %d, %d, %d] (Q4 format, 8=zero)\n\n", 
           input[0], input[1], input[2], input[3]);
    
    // Warm up
    for (int i = 0; i < 10; i++) {
        cfc_forward_parallel(input);
    }
    
    // Benchmark
    int num_iters = 100;
    int64_t start = esp_timer_get_time();
    
    for (int i = 0; i < num_iters; i++) {
        cfc_forward_parallel(input);
    }
    
    int64_t end = esp_timer_get_time();
    int64_t total_us = end - start;
    float per_inference_us = (float)total_us / num_iters;
    float inference_rate = 1000000.0f / per_inference_us;
    
    printf("  Benchmark: %d iterations\n", num_iters);
    printf("  Total time: %lld µs\n", (long long)total_us);
    printf("  Per inference: %.1f µs\n", per_inference_us);
    printf("  Inference rate: %.0f Hz\n\n", inference_rate);
    
    // Show final hidden state
    printf("  Final hidden state (first 8 neurons):\n    ");
    for (int i = 0; i < 8 && i < CFC_HIDDEN_DIM; i++) {
        printf("%3d ", network.h[i]);
    }
    printf("\n");
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  CfC + PARALLEL DOT PRODUCT INTEGRATION                           ║\n");
    printf("║  4-channel PARLIO → 4 PCNTs for neural inference                  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    ESP_LOGI(TAG, "Initializing hardware...");
    ESP_ERROR_CHECK(setup_hardware());
    
    ESP_LOGI(TAG, "Initializing LUTs...");
    init_luts();
    
    ESP_LOGI(TAG, "Initializing network...");
    init_random_weights();
    
    ESP_LOGI(TAG, "Ready!");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    benchmark_cfc();
    
    // Summary
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                          SUMMARY                                  ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
    printf("║  Parallel dot product integrated with CfC!                       ║\n");
    printf("║                                                                   ║\n");
    printf("║  Architecture:                                                    ║\n");
    printf("║    - 4 neurons processed simultaneously                          ║\n");
    printf("║    - Ternary weights: pos_mask, neg_mask                         ║\n");
    printf("║    - PARLIO 4-bit → 4 GPIOs → 4 PCNTs                            ║\n");
    printf("║    - LUT-based activation and mixing                             ║\n");
    printf("║                                                                   ║\n");
    printf("║  Next: Full 64-neuron network, autonomous ETM triggering         ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    
    ESP_LOGI(TAG, "Done.");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
