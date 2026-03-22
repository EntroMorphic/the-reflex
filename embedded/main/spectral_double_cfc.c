/**
 * spectral_double_cfc.c - Spectral Double-CfC with Multi-Timescale Dynamics
 *
 * CONCEPT: Combine two CfC networks at different timescales with spectral encoding
 *
 * Double-CfC Architecture:
 *   - FAST network: decay = 0.5 (rapid response, tracks transients)
 *   - SLOW network: decay = 0.95 (long memory, tracks context)
 *   - Output = mixer(fast_h, slow_h) - combines both timescales
 *
 * Spectral Rotation:
 *   - Hidden states treated as complex numbers (real, imag)
 *   - Gate and candidate produce rotation angle
 *   - State evolves by rotation rather than linear interpolation
 *   - h_new = h_prev * exp(i * theta) where theta = gate * candidate
 *
 * Hardware Mapping:
 *   4 PCNT channels split as:
 *     - PCNT0, PCNT1: Fast network neurons (pairs processed in parallel)
 *     - PCNT2, PCNT3: Slow network neurons (pairs processed in parallel)
 *
 * The fast/slow combination naturally captures:
 *   - Fast: Edge detection, transients, immediate response
 *   - Slow: Context, memory, accumulated history
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

static const char *TAG = "SPECTRAL_CFC";

// ============================================================
// Configuration
// ============================================================

#define NUM_NEURONS_FAST    8       // Fast timescale neurons
#define NUM_NEURONS_SLOW    8       // Slow timescale neurons
#define CFC_INPUT_DIM       4
#define CFC_CONCAT_DIM      (NUM_NEURONS_FAST + NUM_NEURONS_SLOW + CFC_INPUT_DIM)

// Timescale parameters
#define DECAY_FAST          0.5f    // Fast decay (rapid change)
#define DECAY_SLOW          0.95f   // Slow decay (long memory)

// Hardware
#define NUM_PARALLEL        4
#define GPIO_CH0            4
#define GPIO_CH1            5
#define GPIO_CH2            6
#define GPIO_CH3            7

#define PARLIO_FREQ_HZ      10000000
#define PCNT_OVERFLOW_LIMIT 30000
#define MAX_PATTERN_BYTES   4096
#define MIN_PATTERN_BYTES   4

// Spectral rotation resolution (16 angles)
#define SPECTRAL_ANGLES     16
#define SPECTRAL_SCALE      7.5f    // Scale for tanh-like range

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
// Spectral State (complex representation)
// ============================================================

typedef struct {
    int8_t real;    // Real component (Q7: -128 to 127 maps to -1 to 1)
    int8_t imag;    // Imaginary component
} complex_q7_t;

// ============================================================
// Network State
// ============================================================

typedef struct {
    uint32_t pos_mask[NUM_NEURONS_FAST + NUM_NEURONS_SLOW];
    uint32_t neg_mask[NUM_NEURONS_FAST + NUM_NEURONS_SLOW];
} ternary_weights_t;

typedef struct {
    // Fast network weights
    ternary_weights_t W_gate_fast;
    ternary_weights_t W_cand_fast;
    int8_t b_gate_fast[NUM_NEURONS_FAST];
    int8_t b_cand_fast[NUM_NEURONS_FAST];
    
    // Slow network weights
    ternary_weights_t W_gate_slow;
    ternary_weights_t W_cand_slow;
    int8_t b_gate_slow[NUM_NEURONS_SLOW];
    int8_t b_cand_slow[NUM_NEURONS_SLOW];
    
    // Spectral hidden states (complex)
    complex_q7_t h_fast[NUM_NEURONS_FAST];
    complex_q7_t h_slow[NUM_NEURONS_SLOW];
    
    // Cross-network mixing weights (for combining fast/slow)
    int8_t mix_fast_to_slow[NUM_NEURONS_SLOW];  // How fast influences slow
    int8_t mix_slow_to_fast[NUM_NEURONS_FAST];  // How slow influences fast
    
} spectral_cfc_t;

static spectral_cfc_t network;

// ============================================================
// LUTs for spectral operations
// ============================================================

// Sigmoid LUT (8-bit input -> 4-bit output)
static uint8_t sigmoid_lut[256];

// Tanh LUT (8-bit input -> 4-bit output centered at 8)
static uint8_t tanh_lut[256];

// Rotation LUTs: cos and sin for 16 angles (0 to 2pi)
static int8_t cos_lut[SPECTRAL_ANGLES];
static int8_t sin_lut[SPECTRAL_ANGLES];

// Magnitude from real,imag (approximation)
static uint8_t magnitude_lut[16][16];  // [real_q4][imag_q4] -> magnitude

// ============================================================
// Overflow callbacks
// ============================================================

static bool IRAM_ATTR pcnt_cb_0(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_counts[0]++; return false; }
static bool IRAM_ATTR pcnt_cb_1(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_counts[1]++; return false; }
static bool IRAM_ATTR pcnt_cb_2(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_counts[2]++; return false; }
static bool IRAM_ATTR pcnt_cb_3(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_counts[3]++; return false; }

static pcnt_watch_cb_t overflow_callbacks[NUM_PARALLEL] = { pcnt_cb_0, pcnt_cb_1, pcnt_cb_2, pcnt_cb_3 };

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

// ============================================================
// Hardware setup
// ============================================================

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
    
    // Rotation LUTs (Q7 fixed point: -128 to 127 for -1 to ~1)
    for (int i = 0; i < SPECTRAL_ANGLES; i++) {
        float angle = (2.0f * M_PI * i) / SPECTRAL_ANGLES;
        cos_lut[i] = (int8_t)(cosf(angle) * 127.0f);
        sin_lut[i] = (int8_t)(sinf(angle) * 127.0f);
    }
    
    // Magnitude approximation LUT (uses max(|r|,|i|) + 0.4*min(|r|,|i|))
    for (int r = 0; r < 16; r++) {
        for (int i = 0; i < 16; i++) {
            float rf = ((float)r / 7.5f) - 1.0f;
            float imf = ((float)i / 7.5f) - 1.0f;
            float mag = sqrtf(rf*rf + imf*imf);
            if (mag > 1.0f) mag = 1.0f;
            magnitude_lut[r][i] = (uint8_t)(mag * 15.0f);
        }
    }
}

// ============================================================
// Initialize random weights
// ============================================================

static void init_random_weights(void) {
    // Fast network weights
    for (int n = 0; n < NUM_NEURONS_FAST; n++) {
        network.W_gate_fast.pos_mask[n] = 0;
        network.W_gate_fast.neg_mask[n] = 0;
        network.W_cand_fast.pos_mask[n] = 0;
        network.W_cand_fast.neg_mask[n] = 0;
        
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            int r = esp_random() % 3;
            if (r == 0) network.W_gate_fast.pos_mask[n] |= (1 << i);
            else if (r == 1) network.W_gate_fast.neg_mask[n] |= (1 << i);
            
            r = esp_random() % 3;
            if (r == 0) network.W_cand_fast.pos_mask[n] |= (1 << i);
            else if (r == 1) network.W_cand_fast.neg_mask[n] |= (1 << i);
        }
        
        network.b_gate_fast[n] = (esp_random() % 11) - 5;
        network.b_cand_fast[n] = (esp_random() % 11) - 5;
        network.h_fast[n].real = 0;
        network.h_fast[n].imag = 0;
    }
    
    // Slow network weights
    for (int n = 0; n < NUM_NEURONS_SLOW; n++) {
        network.W_gate_slow.pos_mask[n] = 0;
        network.W_gate_slow.neg_mask[n] = 0;
        network.W_cand_slow.pos_mask[n] = 0;
        network.W_cand_slow.neg_mask[n] = 0;
        
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            int r = esp_random() % 3;
            if (r == 0) network.W_gate_slow.pos_mask[n] |= (1 << i);
            else if (r == 1) network.W_gate_slow.neg_mask[n] |= (1 << i);
            
            r = esp_random() % 3;
            if (r == 0) network.W_cand_slow.pos_mask[n] |= (1 << i);
            else if (r == 1) network.W_cand_slow.neg_mask[n] |= (1 << i);
        }
        
        network.b_gate_slow[n] = (esp_random() % 11) - 5;
        network.b_cand_slow[n] = (esp_random() % 11) - 5;
        network.h_slow[n].real = 0;
        network.h_slow[n].imag = 0;
    }
    
    // Cross-network mixing (small random values)
    for (int n = 0; n < NUM_NEURONS_SLOW; n++) {
        network.mix_fast_to_slow[n] = (esp_random() % 21) - 10;  // -10 to +10
    }
    for (int n = 0; n < NUM_NEURONS_FAST; n++) {
        network.mix_slow_to_fast[n] = (esp_random() % 21) - 10;
    }
}

// ============================================================
// Build parallel pattern for 4 neurons
// ============================================================

static size_t build_4neuron_pattern(
    const uint8_t* input_q4,
    int neuron_base,
    const ternary_weights_t* W,
    int total_neurons
) {
    int pulses[NUM_PARALLEL] = {0};
    
    for (int ch = 0; ch < NUM_PARALLEL; ch++) {
        int n = neuron_base + ch;
        if (n >= total_neurons) continue;
        
        uint32_t pos_mask = W->pos_mask[n];
        uint32_t neg_mask = W->neg_mask[n];
        
        int pos_sum = 0;
        for (int i = 0; i < CFC_CONCAT_DIM && i < 32; i++) {
            int val = (i < CFC_INPUT_DIM) ? input_q4[i] : 8;  // Default to 0 (center)
            if (pos_mask & (1 << i)) pos_sum += val;
        }
        
        pulses[ch] = pos_sum;
    }
    
    int max_pulses = 0;
    for (int i = 0; i < NUM_PARALLEL; i++) {
        if (pulses[i] > max_pulses) max_pulses = pulses[i];
    }
    
    size_t num_cycles = max_pulses * 2;
    if (num_cycles < MIN_PATTERN_BYTES) num_cycles = MIN_PATTERN_BYTES;
    if (num_cycles > MAX_PATTERN_BYTES) num_cycles = MAX_PATTERN_BYTES;
    
    memset(pattern_buffer, 0, MAX_PATTERN_BYTES);
    
    int remaining[NUM_PARALLEL];
    for (int i = 0; i < NUM_PARALLEL; i++) remaining[i] = pulses[i];
    
    for (size_t cycle = 0; cycle < num_cycles; cycle++) {
        if (cycle % 2 == 0) {
            pattern_buffer[cycle] = 0x00;
        } else {
            uint8_t mask = 0;
            for (int ch = 0; ch < NUM_PARALLEL; ch++) {
                if (remaining[ch] > 0) {
                    mask |= (1 << ch);
                    remaining[ch]--;
                }
            }
            pattern_buffer[cycle] = mask;
        }
    }
    
    return num_cycles;
}

// ============================================================
// Compute dot products and get results
// ============================================================

static void compute_4neuron_dot(
    const uint8_t* input_q4,
    int neuron_base,
    const ternary_weights_t* W,
    const int8_t* bias,
    int total_neurons,
    int* results
) {
    size_t pattern_len = build_4neuron_pattern(input_q4, neuron_base, W, total_neurons);
    reset_all_counters();
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pattern_buffer, pattern_len * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 1000);
    esp_rom_delay_us(5);
    
    for (int ch = 0; ch < NUM_PARALLEL; ch++) {
        int n = neuron_base + ch;
        if (n >= total_neurons) {
            results[ch] = 0;
            continue;
        }
        
        int hw_pos = get_full_count(ch);
        
        // Software negative sum
        uint32_t neg_mask = W->neg_mask[n];
        int neg_sum = 0;
        for (int i = 0; i < CFC_CONCAT_DIM && i < 32; i++) {
            int val = (i < CFC_INPUT_DIM) ? input_q4[i] : 8;
            if (neg_mask & (1 << i)) neg_sum += val;
        }
        
        results[ch] = hw_pos - neg_sum + bias[n];
    }
}

// ============================================================
// Spectral rotation of complex state
// ============================================================

static void spectral_rotate(complex_q7_t* state, int angle_idx, float decay) {
    // Get rotation factors
    int c = cos_lut[angle_idx & (SPECTRAL_ANGLES - 1)];
    int s = sin_lut[angle_idx & (SPECTRAL_ANGLES - 1)];
    
    // Current state
    int r = state->real;
    int i = state->imag;
    
    // Rotate: new = old * exp(i*theta) = old * (cos(theta) + i*sin(theta))
    // real_new = real*cos - imag*sin
    // imag_new = real*sin + imag*cos
    int new_r = (r * c - i * s) >> 7;  // Q7 * Q7 -> Q14, shift back to Q7
    int new_i = (r * s + i * c) >> 7;
    
    // Apply decay
    int decay_q7 = (int)(decay * 127.0f);
    new_r = (new_r * decay_q7) >> 7;
    new_i = (new_i * decay_q7) >> 7;
    
    // Clamp
    if (new_r > 127) new_r = 127;
    if (new_r < -128) new_r = -128;
    if (new_i > 127) new_i = 127;
    if (new_i < -128) new_i = -128;
    
    state->real = (int8_t)new_r;
    state->imag = (int8_t)new_i;
}

// ============================================================
// Full Spectral Double-CfC forward pass
// ============================================================

static void spectral_cfc_forward(const uint8_t* input_q4) {
    // Build concatenated input [x; h_fast_mag; h_slow_mag]
    // We use magnitudes of complex states for input representation
    uint8_t concat[CFC_CONCAT_DIM];
    memcpy(concat, input_q4, CFC_INPUT_DIM);
    
    // Convert complex h_fast to magnitudes for concat
    for (int n = 0; n < NUM_NEURONS_FAST; n++) {
        int r = (network.h_fast[n].real + 128) >> 4;  // Q7 to Q4
        int i = (network.h_fast[n].imag + 128) >> 4;
        if (r > 15) { r = 15; } if (r < 0) { r = 0; }
        if (i > 15) { i = 15; } if (i < 0) { i = 0; }
        concat[CFC_INPUT_DIM + n] = magnitude_lut[r][i];
    }
    
    for (int n = 0; n < NUM_NEURONS_SLOW; n++) {
        int r = (network.h_slow[n].real + 128) >> 4;
        int i = (network.h_slow[n].imag + 128) >> 4;
        if (r > 15) { r = 15; } if (r < 0) { r = 0; }
        if (i > 15) { i = 15; } if (i < 0) { i = 0; }
        concat[CFC_INPUT_DIM + NUM_NEURONS_FAST + n] = magnitude_lut[r][i];
    }
    
    // ========== FAST NETWORK ==========
    // Process fast neurons in batches of 4
    for (int base = 0; base < NUM_NEURONS_FAST; base += NUM_PARALLEL) {
        int gate_dots[NUM_PARALLEL];
        compute_4neuron_dot(concat, base, &network.W_gate_fast, network.b_gate_fast, NUM_NEURONS_FAST, gate_dots);
        
        int cand_dots[NUM_PARALLEL];
        compute_4neuron_dot(concat, base, &network.W_cand_fast, network.b_cand_fast, NUM_NEURONS_FAST, cand_dots);
        
        for (int ch = 0; ch < NUM_PARALLEL && (base + ch) < NUM_NEURONS_FAST; ch++) {
            int n = base + ch;
            
            // Gate determines rotation magnitude
            int gate_idx = gate_dots[ch] + 128;
            if (gate_idx < 0) gate_idx = 0;
            if (gate_idx > 255) gate_idx = 255;
            int gate_q4 = sigmoid_lut[gate_idx];  // 0-15
            
            // Candidate determines rotation direction
            int cand_idx = cand_dots[ch] + 128;
            if (cand_idx < 0) cand_idx = 0;
            if (cand_idx > 255) cand_idx = 255;
            int cand_q4 = tanh_lut[cand_idx];  // 0-15
            
            // Rotation angle = gate * candidate (normalized)
            int angle_idx = (gate_q4 * (cand_q4 - 8)) >> 4;  // Center cand around 0
            angle_idx = (angle_idx + SPECTRAL_ANGLES) % SPECTRAL_ANGLES;
            
            // Apply spectral rotation with fast decay
            spectral_rotate(&network.h_fast[n], angle_idx, DECAY_FAST);
        }
    }
    
    // ========== SLOW NETWORK ==========
    // Process slow neurons in batches of 4
    for (int base = 0; base < NUM_NEURONS_SLOW; base += NUM_PARALLEL) {
        int gate_dots[NUM_PARALLEL];
        compute_4neuron_dot(concat, base, &network.W_gate_slow, network.b_gate_slow, NUM_NEURONS_SLOW, gate_dots);
        
        int cand_dots[NUM_PARALLEL];
        compute_4neuron_dot(concat, base, &network.W_cand_slow, network.b_cand_slow, NUM_NEURONS_SLOW, cand_dots);
        
        for (int ch = 0; ch < NUM_PARALLEL && (base + ch) < NUM_NEURONS_SLOW; ch++) {
            int n = base + ch;
            
            int gate_idx = gate_dots[ch] + 128;
            if (gate_idx < 0) gate_idx = 0;
            if (gate_idx > 255) gate_idx = 255;
            int gate_q4 = sigmoid_lut[gate_idx];
            
            int cand_idx = cand_dots[ch] + 128;
            if (cand_idx < 0) cand_idx = 0;
            if (cand_idx > 255) cand_idx = 255;
            int cand_q4 = tanh_lut[cand_idx];
            
            int angle_idx = (gate_q4 * (cand_q4 - 8)) >> 4;
            angle_idx = (angle_idx + SPECTRAL_ANGLES) % SPECTRAL_ANGLES;
            
            // Apply spectral rotation with slow decay
            spectral_rotate(&network.h_slow[n], angle_idx, DECAY_SLOW);
        }
    }
    
    // ========== CROSS-NETWORK INFLUENCE ==========
    // Fast influences slow (context from transients)
    for (int n = 0; n < NUM_NEURONS_SLOW; n++) {
        int influence = network.mix_fast_to_slow[n];
        if (n < NUM_NEURONS_FAST) {
            // Add a fraction of fast's rotation to slow
            int fast_mag = magnitude_lut[(network.h_fast[n].real + 128) >> 4]
                                        [(network.h_fast[n].imag + 128) >> 4];
            int nudge = (fast_mag * influence) >> 7;
            network.h_slow[n].real += nudge;
            network.h_slow[n].imag += nudge >> 1;
        }
    }
    
    // Slow influences fast (context guides response)
    for (int n = 0; n < NUM_NEURONS_FAST; n++) {
        int influence = network.mix_slow_to_fast[n];
        if (n < NUM_NEURONS_SLOW) {
            int slow_mag = magnitude_lut[(network.h_slow[n].real + 128) >> 4]
                                        [(network.h_slow[n].imag + 128) >> 4];
            int nudge = (slow_mag * influence) >> 7;
            network.h_fast[n].real += nudge;
            network.h_fast[n].imag += nudge >> 1;
        }
    }
}

// ============================================================
// Benchmark
// ============================================================

static void benchmark_spectral_cfc(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  SPECTRAL DOUBLE-CfC BENCHMARK                                    ║\n");
    printf("║  Fast: %d neurons (decay=%.2f), Slow: %d neurons (decay=%.2f)      ║\n",
           NUM_NEURONS_FAST, DECAY_FAST, NUM_NEURONS_SLOW, DECAY_SLOW);
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    uint8_t input[CFC_INPUT_DIM] = {8, 10, 6, 12};
    
    printf("  Input: [%d, %d, %d, %d] (Q4 format, 8=zero)\n\n", 
           input[0], input[1], input[2], input[3]);
    
    // Warm up
    for (int i = 0; i < 10; i++) {
        spectral_cfc_forward(input);
    }
    
    // Benchmark
    int num_iters = 100;
    int64_t start = esp_timer_get_time();
    
    for (int i = 0; i < num_iters; i++) {
        spectral_cfc_forward(input);
    }
    
    int64_t end = esp_timer_get_time();
    int64_t total_us = end - start;
    float per_inference_us = (float)total_us / num_iters;
    float inference_rate = 1000000.0f / per_inference_us;
    
    printf("  Benchmark: %d iterations\n", num_iters);
    printf("  Total time: %lld us\n", (long long)total_us);
    printf("  Per inference: %.1f us\n", per_inference_us);
    printf("  Inference rate: %.0f Hz\n\n", inference_rate);
    
    // Show spectral states
    printf("  Fast network states (magnitude, phase-like):\n    ");
    for (int n = 0; n < NUM_NEURONS_FAST; n++) {
        int r = (network.h_fast[n].real + 128) >> 4;
        int i = (network.h_fast[n].imag + 128) >> 4;
        if (r > 15) { r = 15; } if (r < 0) { r = 0; }
        if (i > 15) { i = 15; } if (i < 0) { i = 0; }
        printf("%2d ", magnitude_lut[r][i]);
    }
    printf("\n");
    
    printf("  Slow network states (magnitude, phase-like):\n    ");
    for (int n = 0; n < NUM_NEURONS_SLOW; n++) {
        int r = (network.h_slow[n].real + 128) >> 4;
        int i = (network.h_slow[n].imag + 128) >> 4;
        if (r > 15) { r = 15; } if (r < 0) { r = 0; }
        if (i > 15) { i = 15; } if (i < 0) { i = 0; }
        printf("%2d ", magnitude_lut[r][i]);
    }
    printf("\n");
}

// ============================================================
// Demonstration: Time series with changing input
// ============================================================

static void demo_timescale_separation(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  DEMO: TIMESCALE SEPARATION                                       ║\n");
    printf("║  Step input: fast responds quickly, slow accumulates              ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Reset states
    for (int n = 0; n < NUM_NEURONS_FAST; n++) {
        network.h_fast[n].real = 0;
        network.h_fast[n].imag = 0;
    }
    for (int n = 0; n < NUM_NEURONS_SLOW; n++) {
        network.h_slow[n].real = 0;
        network.h_slow[n].imag = 0;
    }
    
    uint8_t input_low[CFC_INPUT_DIM] = {4, 4, 4, 4};   // Below center
    uint8_t input_high[CFC_INPUT_DIM] = {12, 12, 12, 12}; // Above center
    
    printf("  Step: 5 low -> 5 high -> 5 low\n");
    printf("  Time | Fast(0) | Slow(0) | Input\n");
    printf("  -----+---------+---------+-------\n");
    
    for (int t = 0; t < 15; t++) {
        uint8_t* input = (t >= 5 && t < 10) ? input_high : input_low;
        
        spectral_cfc_forward(input);
        
        // Get magnitudes for display
        int fast_r = (network.h_fast[0].real + 128) >> 4;
        int fast_i = (network.h_fast[0].imag + 128) >> 4;
        if (fast_r > 15) { fast_r = 15; }
        if (fast_r < 0) { fast_r = 0; }
        if (fast_i > 15) { fast_i = 15; }
        if (fast_i < 0) { fast_i = 0; }
        int fast_mag = magnitude_lut[fast_r][fast_i];
        
        int slow_r = (network.h_slow[0].real + 128) >> 4;
        int slow_i = (network.h_slow[0].imag + 128) >> 4;
        if (slow_r > 15) { slow_r = 15; }
        if (slow_r < 0) { slow_r = 0; }
        if (slow_i > 15) { slow_i = 15; }
        if (slow_i < 0) { slow_i = 0; }
        int slow_mag = magnitude_lut[slow_r][slow_i];
        
        printf("  %4d |   %2d    |   %2d    | %s\n", 
               t, fast_mag, slow_mag, 
               (t >= 5 && t < 10) ? "HIGH" : "low");
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  SPECTRAL DOUBLE-CfC                                              ║\n");
    printf("║  Multi-timescale liquid dynamics with spectral rotation           ║\n");
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
    
    // Run benchmark
    benchmark_spectral_cfc();
    
    // Run demo
    demo_timescale_separation();
    
    // Summary
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                          SUMMARY                                  ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
    printf("║  Spectral Double-CfC Architecture:                                ║\n");
    printf("║    - FAST network: decay=0.5 (rapid response)                     ║\n");
    printf("║    - SLOW network: decay=0.95 (long memory)                       ║\n");
    printf("║    - Spectral rotation: complex state evolves by angle            ║\n");
    printf("║    - Cross-network mixing: fast<->slow influence                  ║\n");
    printf("║                                                                   ║\n");
    printf("║  Benefits:                                                        ║\n");
    printf("║    - Natural separation of transients and context                 ║\n");
    printf("║    - Phase information encoded in complex state                   ║\n");
    printf("║    - Hierarchical temporal abstraction                            ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    
    ESP_LOGI(TAG, "Done.");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
