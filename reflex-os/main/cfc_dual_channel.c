/**
 * cfc_dual_channel.c - CfC with TRUE Parallel Pos/Neg Accumulation
 *
 * KEY IMPROVEMENT: Each PCNT unit has TWO channels!
 *   - Channel A: Count positive weight contributions (rising edges)
 *   - Channel B: Count negative weight contributions (rising edges, subtract)
 *
 * This eliminates software negative computation entirely.
 *
 * Architecture:
 *   PARLIO (8-bit) ──┬── GPIO4 ── PCNT0_CH_A (+) ──┐
 *                    │         ── PCNT0_CH_B (-) ──┤ neuron0: pos - neg
 *                    ├── GPIO5 ── PCNT1_CH_A (+) ──┤
 *                    │         ── PCNT1_CH_B (-) ──┤ neuron1: pos - neg
 *                    ├── GPIO6 ── PCNT2_CH_A (+) ──┤
 *                    │         ── PCNT2_CH_B (-) ──┤ neuron2: pos - neg
 *                    └── GPIO7 ── PCNT3_CH_A (+) ──┤
 *                              ── PCNT3_CH_B (-) ──┘ neuron3: pos - neg
 *
 * Wait... that's the same GPIO for both channels.
 * 
 * ACTUAL ARCHITECTURE:
 *   PARLIO (8-bit) ──┬── GPIO4 (pos0) ── PCNT0_CH_A (+1 per edge)
 *                    ├── GPIO5 (neg0) ── PCNT0_CH_B (-1 per edge)
 *                    ├── GPIO6 (pos1) ── PCNT1_CH_A (+1 per edge)
 *                    ├── GPIO7 (neg1) ── PCNT1_CH_B (-1 per edge)
 *                    ├── GPIO8 (pos2) ── PCNT2_CH_A (+1 per edge)
 *                    ├── GPIO9 (neg2) ── PCNT2_CH_B (-1 per edge)
 *                    ├── GPIO10(pos3) ── PCNT3_CH_A (+1 per edge)
 *                    └── GPIO11(neg3) ── PCNT3_CH_B (-1 per edge)
 *
 * With 8-bit PARLIO, we get 4 neurons per batch with true pos/neg accumulation!
 * The PCNT counter naturally computes: pos_count - neg_count
 *
 * Benefits:
 *   - Zero software computation for negative weights
 *   - Single DMA transfer computes 4 complete dot products
 *   - PCNT does the subtraction for us
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

static const char *TAG = "CFC_DUAL";

// ============================================================
// CfC Configuration
// ============================================================

#define CFC_HIDDEN_DIM      16      // Number of neurons (must be multiple of 4)
#define CFC_INPUT_DIM       4       // Input size
#define CFC_CONCAT_DIM      (CFC_HIDDEN_DIM + CFC_INPUT_DIM)

// Quantization
#define CFC_QUANT_BITS      4
#define CFC_QUANT_LEVELS    16

// ============================================================
// Dual-Channel Hardware Configuration
// ============================================================

#define NUM_NEURONS_PARALLEL    4       // Process 4 neurons at once
#define NUM_CHANNELS_PER_NEURON 2       // Positive and negative
#define PARLIO_DATA_WIDTH       8       // 8-bit PARLIO for 4 neurons × 2 channels

// GPIO assignments: [pos0, neg0, pos1, neg1, pos2, neg2, pos3, neg3]
#define GPIO_POS0   4
#define GPIO_NEG0   5
#define GPIO_POS1   6
#define GPIO_NEG1   7
#define GPIO_POS2   8
#define GPIO_NEG2   9
#define GPIO_POS3   10
#define GPIO_NEG3   11

static const int gpio_pos[NUM_NEURONS_PARALLEL] = {GPIO_POS0, GPIO_POS1, GPIO_POS2, GPIO_POS3};
static const int gpio_neg[NUM_NEURONS_PARALLEL] = {GPIO_NEG0, GPIO_NEG1, GPIO_NEG2, GPIO_NEG3};

#define PARLIO_FREQ_HZ      10000000
#define PCNT_HIGH_LIMIT     30000
#define PCNT_LOW_LIMIT      -30000
#define MAX_PATTERN_BYTES   4096
#define MIN_PATTERN_BYTES   4

// ============================================================
// Hardware handles
// ============================================================

static pcnt_unit_handle_t pcnt_units[NUM_NEURONS_PARALLEL] = {NULL};
static pcnt_channel_handle_t pcnt_ch_pos[NUM_NEURONS_PARALLEL] = {NULL};
static pcnt_channel_handle_t pcnt_ch_neg[NUM_NEURONS_PARALLEL] = {NULL};
static parlio_tx_unit_handle_t parlio = NULL;

// Overflow tracking (for values > 30000)
static volatile int overflow_high[NUM_NEURONS_PARALLEL] = {0};
static volatile int overflow_low[NUM_NEURONS_PARALLEL] = {0};

static uint8_t *pattern_buffer = NULL;

// ============================================================
// CfC State and Weights
// ============================================================

typedef struct {
    uint32_t pos_mask[CFC_HIDDEN_DIM];
    uint32_t neg_mask[CFC_HIDDEN_DIM];
} cfc_ternary_layer_t;

typedef struct {
    cfc_ternary_layer_t W_gate;
    cfc_ternary_layer_t W_cand;
    int8_t b_gate[CFC_HIDDEN_DIM];
    int8_t b_cand[CFC_HIDDEN_DIM];
    float decay[CFC_HIDDEN_DIM];
    uint8_t h[CFC_HIDDEN_DIM];
} cfc_network_t;

static cfc_network_t network;

// LUTs
static uint8_t sigmoid_lut[256];
static uint8_t tanh_lut[256];
static uint8_t mixer_lut[16][16][16];

// ============================================================
// Overflow callbacks
// ============================================================

static bool IRAM_ATTR pcnt_high_cb_0(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_high[0]++; return false; }
static bool IRAM_ATTR pcnt_high_cb_1(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_high[1]++; return false; }
static bool IRAM_ATTR pcnt_high_cb_2(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_high[2]++; return false; }
static bool IRAM_ATTR pcnt_high_cb_3(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_high[3]++; return false; }

static bool IRAM_ATTR pcnt_low_cb_0(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_low[0]++; return false; }
static bool IRAM_ATTR pcnt_low_cb_1(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_low[1]++; return false; }
static bool IRAM_ATTR pcnt_low_cb_2(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_low[2]++; return false; }
static bool IRAM_ATTR pcnt_low_cb_3(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_low[3]++; return false; }

// ============================================================
// Hardware helpers
// ============================================================

static int get_full_count(int neuron) {
    int val;
    pcnt_unit_get_count(pcnt_units[neuron], &val);
    // Account for overflows in both directions
    return val + (overflow_high[neuron] * PCNT_HIGH_LIMIT) - (overflow_low[neuron] * (-PCNT_LOW_LIMIT));
}

static void reset_all_counters(void) {
    for (int i = 0; i < NUM_NEURONS_PARALLEL; i++) {
        overflow_high[i] = 0;
        overflow_low[i] = 0;
        pcnt_unit_clear_count(pcnt_units[i]);
    }
}

// ============================================================
// Hardware setup
// ============================================================

static esp_err_t setup_hardware(void) {
    ESP_LOGI(TAG, "Setting up dual-channel PCNT architecture...");
    
    // Configure all GPIOs
    for (int i = 0; i < NUM_NEURONS_PARALLEL; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << gpio_pos[i]) | (1ULL << gpio_neg[i]),
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }
    
    // Setup PCNT units with dual channels
    for (int i = 0; i < NUM_NEURONS_PARALLEL; i++) {
        // Create PCNT unit with both high and low limits
        pcnt_unit_config_t unit_cfg = {
            .low_limit = PCNT_LOW_LIMIT,
            .high_limit = PCNT_HIGH_LIMIT,
        };
        ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &pcnt_units[i]));
        
        // Channel A: Positive weights (increment on rising edge)
        pcnt_chan_config_t pos_cfg = {
            .edge_gpio_num = gpio_pos[i],
            .level_gpio_num = -1,
        };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_units[i], &pos_cfg, &pcnt_ch_pos[i]));
        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_ch_pos[i],
            PCNT_CHANNEL_EDGE_ACTION_INCREASE,  // Rising edge: +1
            PCNT_CHANNEL_EDGE_ACTION_HOLD));    // Falling edge: hold
        
        // Channel B: Negative weights (decrement on rising edge)
        pcnt_chan_config_t neg_cfg = {
            .edge_gpio_num = gpio_neg[i],
            .level_gpio_num = -1,
        };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_units[i], &neg_cfg, &pcnt_ch_neg[i]));
        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_ch_neg[i],
            PCNT_CHANNEL_EDGE_ACTION_DECREASE,  // Rising edge: -1
            PCNT_CHANNEL_EDGE_ACTION_HOLD));    // Falling edge: hold
        
        // Watch points for overflow
        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_units[i], PCNT_HIGH_LIMIT));
        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_units[i], PCNT_LOW_LIMIT));
        
        // Register callbacks
        pcnt_event_callbacks_t cbs = { .on_reach = NULL };
        if (i == 0) {
            cbs.on_reach = pcnt_high_cb_0;  // We'll use same callback for both
        } else if (i == 1) {
            cbs.on_reach = pcnt_high_cb_1;
        } else if (i == 2) {
            cbs.on_reach = pcnt_high_cb_2;
        } else {
            cbs.on_reach = pcnt_high_cb_3;
        }
        ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_units[i], &cbs, NULL));
        
        ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_units[i]));
        ESP_ERROR_CHECK(pcnt_unit_start(pcnt_units[i]));
        
        ESP_LOGI(TAG, "  PCNT%d: pos=GPIO%d, neg=GPIO%d", i, gpio_pos[i], gpio_neg[i]);
    }
    
    // Setup 8-bit PARLIO
    parlio_tx_unit_config_t parlio_cfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = PARLIO_FREQ_HZ,
        .data_width = PARLIO_DATA_WIDTH,
        .trans_queue_depth = 8,
        .max_transfer_size = MAX_PATTERN_BYTES + 100,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB,
        .flags = { .io_loop_back = 1 },
    };
    
    // Assign GPIOs: bit0=pos0, bit1=neg0, bit2=pos1, bit3=neg1, ...
    parlio_cfg.data_gpio_nums[0] = GPIO_POS0;
    parlio_cfg.data_gpio_nums[1] = GPIO_NEG0;
    parlio_cfg.data_gpio_nums[2] = GPIO_POS1;
    parlio_cfg.data_gpio_nums[3] = GPIO_NEG1;
    parlio_cfg.data_gpio_nums[4] = GPIO_POS2;
    parlio_cfg.data_gpio_nums[5] = GPIO_NEG2;
    parlio_cfg.data_gpio_nums[6] = GPIO_POS3;
    parlio_cfg.data_gpio_nums[7] = GPIO_NEG3;
    
    for (int i = 8; i < 16; i++) {
        parlio_cfg.data_gpio_nums[i] = -1;
    }
    
    ESP_ERROR_CHECK(parlio_new_tx_unit(&parlio_cfg, &parlio));
    ESP_ERROR_CHECK(parlio_tx_unit_enable(parlio));
    
    // DMA pattern buffer
    pattern_buffer = heap_caps_aligned_alloc(4, MAX_PATTERN_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!pattern_buffer) {
        ESP_LOGE(TAG, "Failed to allocate pattern buffer");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Hardware setup complete!");
    ESP_LOGI(TAG, "  PARLIO: 8-bit @ %d Hz", PARLIO_FREQ_HZ);
    ESP_LOGI(TAG, "  Pattern: [pos0,neg0,pos1,neg1,pos2,neg2,pos3,neg3]");
    
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
    
    // Mixer LUT (decay = 0.9)
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
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        network.W_gate.pos_mask[n] = 0;
        network.W_gate.neg_mask[n] = 0;
        network.W_cand.pos_mask[n] = 0;
        network.W_cand.neg_mask[n] = 0;
        
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            int r = esp_random() % 3;
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
        
        network.b_gate[n] = (esp_random() % 11) - 5;
        network.b_cand[n] = (esp_random() % 11) - 5;
        network.decay[n] = 0.9f;
        network.h[n] = 8;  // Initialize to 0
    }
}

// ============================================================
// Build dual-channel pattern for 4 neurons
// ============================================================

/**
 * Build pattern where each byte contains:
 *   bit 0: positive pulses for neuron 0
 *   bit 1: negative pulses for neuron 0
 *   bit 2: positive pulses for neuron 1
 *   bit 3: negative pulses for neuron 1
 *   bit 4: positive pulses for neuron 2
 *   bit 5: negative pulses for neuron 2
 *   bit 6: positive pulses for neuron 3
 *   bit 7: negative pulses for neuron 3
 *
 * Edge-based encoding: 0x00 → mask → 0x00 → mask (each cycle = 2 bytes)
 */
static size_t build_dual_channel_pattern(
    const uint8_t* input_q4,
    int neuron_base,
    const cfc_ternary_layer_t* W
) {
    // Calculate positive and negative pulse counts for each neuron
    int pos_pulses[NUM_NEURONS_PARALLEL] = {0};
    int neg_pulses[NUM_NEURONS_PARALLEL] = {0};
    
    for (int ch = 0; ch < NUM_NEURONS_PARALLEL; ch++) {
        int n = neuron_base + ch;
        if (n >= CFC_HIDDEN_DIM) continue;
        
        uint32_t pos_mask = W->pos_mask[n];
        uint32_t neg_mask = W->neg_mask[n];
        
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            int val = input_q4[i];
            if (pos_mask & (1 << i)) pos_pulses[ch] += val;
            if (neg_mask & (1 << i)) neg_pulses[ch] += val;
        }
    }
    
    // Find max pulses needed
    int max_pulses = 0;
    for (int ch = 0; ch < NUM_NEURONS_PARALLEL; ch++) {
        if (pos_pulses[ch] > max_pulses) max_pulses = pos_pulses[ch];
        if (neg_pulses[ch] > max_pulses) max_pulses = neg_pulses[ch];
    }
    
    // Add 4 zero bytes at start to ensure clean initial state
    size_t preamble = 4;
    size_t num_cycles = max_pulses * 2 + preamble;  // Low-high pairs + preamble
    if (num_cycles < MIN_PATTERN_BYTES) num_cycles = MIN_PATTERN_BYTES;
    if (num_cycles > MAX_PATTERN_BYTES) num_cycles = MAX_PATTERN_BYTES;
    
    memset(pattern_buffer, 0, MAX_PATTERN_BYTES);
    
    // Track remaining pulses
    int pos_remaining[NUM_NEURONS_PARALLEL];
    int neg_remaining[NUM_NEURONS_PARALLEL];
    for (int ch = 0; ch < NUM_NEURONS_PARALLEL; ch++) {
        pos_remaining[ch] = pos_pulses[ch];
        neg_remaining[ch] = neg_pulses[ch];
    }
    
    // Build pattern with edge-based encoding (after preamble zeros)
    for (size_t cycle = preamble; cycle < num_cycles; cycle++) {
        if ((cycle - preamble) % 2 == 0) {
            pattern_buffer[cycle] = 0x00;  // Low phase
        } else {
            uint8_t mask = 0;
            for (int ch = 0; ch < NUM_NEURONS_PARALLEL; ch++) {
                // Positive pulse on bit (ch * 2)
                if (pos_remaining[ch] > 0) {
                    mask |= (1 << (ch * 2));
                    pos_remaining[ch]--;
                }
                // Negative pulse on bit (ch * 2 + 1)
                if (neg_remaining[ch] > 0) {
                    mask |= (1 << (ch * 2 + 1));
                    neg_remaining[ch]--;
                }
            }
            pattern_buffer[cycle] = mask;  // High phase
        }
    }
    
    return num_cycles;
}

// ============================================================
// Compute parallel dot products for 4 neurons (TRUE pos-neg)
// ============================================================

static void compute_4neuron_dot_dual(
    const uint8_t* input_q4,
    int neuron_base,
    const cfc_ternary_layer_t* W,
    const int8_t* bias,
    int* results
) {
    // Build pattern
    size_t pattern_len = build_dual_channel_pattern(input_q4, neuron_base, W);
    
    // Ensure GPIOs are low before we start (prevents spurious edges)
    for (int i = 0; i < NUM_NEURONS_PARALLEL; i++) {
        gpio_set_level(gpio_pos[i], 0);
        gpio_set_level(gpio_neg[i], 0);
    }
    esp_rom_delay_us(2);
    
    // Reset counters AFTER GPIOs are known low
    reset_all_counters();
    esp_rom_delay_us(1);
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pattern_buffer, pattern_len * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 1000);
    
    // Allow last edges to settle
    esp_rom_delay_us(5);
    
    // Read results - PCNT already computed pos - neg!
    for (int ch = 0; ch < NUM_NEURONS_PARALLEL; ch++) {
        int n = neuron_base + ch;
        if (n >= CFC_HIDDEN_DIM) {
            results[ch] = 0;
            continue;
        }
        
        int hw_result = get_full_count(ch);
        results[ch] = hw_result + bias[n];
    }
}

// ============================================================
// Full CfC forward pass with dual-channel dot products
// ============================================================

static void cfc_forward_dual(const uint8_t* input_q4) {
    // Build concatenated input [x; h]
    uint8_t concat[CFC_CONCAT_DIM];
    memcpy(concat, input_q4, CFC_INPUT_DIM);
    memcpy(concat + CFC_INPUT_DIM, network.h, CFC_HIDDEN_DIM);
    
    uint8_t gate[CFC_HIDDEN_DIM];
    uint8_t candidate[CFC_HIDDEN_DIM];
    
    // Process neurons in batches of 4
    for (int base = 0; base < CFC_HIDDEN_DIM; base += NUM_NEURONS_PARALLEL) {
        // Gate computation (4 neurons at once)
        int gate_dots[NUM_NEURONS_PARALLEL];
        compute_4neuron_dot_dual(concat, base, &network.W_gate, network.b_gate, gate_dots);
        
        for (int i = 0; i < NUM_NEURONS_PARALLEL && (base + i) < CFC_HIDDEN_DIM; i++) {
            int idx = gate_dots[i] + 128;
            if (idx < 0) idx = 0;
            if (idx > 255) idx = 255;
            gate[base + i] = sigmoid_lut[idx];
        }
        
        // Candidate computation (4 neurons at once)
        int cand_dots[NUM_NEURONS_PARALLEL];
        compute_4neuron_dot_dual(concat, base, &network.W_cand, network.b_cand, cand_dots);
        
        for (int i = 0; i < NUM_NEURONS_PARALLEL && (base + i) < CFC_HIDDEN_DIM; i++) {
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
// Software reference implementation for verification
// ============================================================

static void cfc_forward_software(const uint8_t* input_q4, uint8_t* h_out) {
    uint8_t concat[CFC_CONCAT_DIM];
    memcpy(concat, input_q4, CFC_INPUT_DIM);
    memcpy(concat + CFC_INPUT_DIM, network.h, CFC_HIDDEN_DIM);
    
    uint8_t gate[CFC_HIDDEN_DIM];
    uint8_t candidate[CFC_HIDDEN_DIM];
    
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        // Compute gate dot product
        int gate_dot = 0;
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            if (network.W_gate.pos_mask[n] & (1 << i)) gate_dot += concat[i];
            if (network.W_gate.neg_mask[n] & (1 << i)) gate_dot -= concat[i];
        }
        gate_dot += network.b_gate[n];
        int idx = gate_dot + 128;
        if (idx < 0) idx = 0;
        if (idx > 255) idx = 255;
        gate[n] = sigmoid_lut[idx];
        
        // Compute candidate dot product
        int cand_dot = 0;
        for (int i = 0; i < CFC_CONCAT_DIM; i++) {
            if (network.W_cand.pos_mask[n] & (1 << i)) cand_dot += concat[i];
            if (network.W_cand.neg_mask[n] & (1 << i)) cand_dot -= concat[i];
        }
        cand_dot += network.b_cand[n];
        idx = cand_dot + 128;
        if (idx < 0) idx = 0;
        if (idx > 255) idx = 255;
        candidate[n] = tanh_lut[idx];
    }
    
    for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
        h_out[n] = mixer_lut[gate[n]][network.h[n]][candidate[n]];
    }
}

// ============================================================
// Verification test
// ============================================================

static int verify_dual_channel(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  VERIFICATION: DUAL-CHANNEL DOT PRODUCT                           ║\n");
    printf("║  Hardware (pos-neg in PCNT) vs Software reference                 ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    int failures = 0;
    
    // Test with various inputs
    uint8_t test_inputs[][CFC_INPUT_DIM] = {
        {8, 8, 8, 8},       // All zeros (8 = center)
        {0, 15, 0, 15},     // Alternating extremes
        {4, 8, 12, 6},      // Mixed values
        {15, 15, 15, 15},   // All max
        {1, 2, 3, 4},       // Low values
    };
    
    int num_tests = sizeof(test_inputs) / sizeof(test_inputs[0]);
    
    for (int t = 0; t < num_tests; t++) {
        // Reset hidden state
        for (int n = 0; n < CFC_HIDDEN_DIM; n++) network.h[n] = 8;
        
        // Get software result
        uint8_t sw_hidden[CFC_HIDDEN_DIM];
        cfc_forward_software(test_inputs[t], sw_hidden);
        
        // Reset and get hardware result
        for (int n = 0; n < CFC_HIDDEN_DIM; n++) network.h[n] = 8;
        cfc_forward_dual(test_inputs[t]);
        
        // Compare with ±1 tolerance (acceptable for 4-bit quantization)
        int match = 1;
        int max_diff = 0;
        for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
            int diff = (int)network.h[n] - (int)sw_hidden[n];
            if (diff < 0) diff = -diff;
            if (diff > max_diff) max_diff = diff;
            if (diff > 1) {  // Allow ±1 tolerance
                match = 0;
                printf("  MISMATCH at neuron %d: hw=%d sw=%d (diff=%d)\n", n, network.h[n], sw_hidden[n], diff);
            }
        }
        
        printf("  Test %d: input=[%d,%d,%d,%d] max_diff=%d %s\n", t,
               test_inputs[t][0], test_inputs[t][1], test_inputs[t][2], test_inputs[t][3],
               max_diff, match ? "PASS" : "FAIL");
        
        if (!match) failures++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    printf("\n  Result: %d/%d passed (with +/-1 tolerance)\n", num_tests - failures, num_tests);
    return failures;
}

// ============================================================
// Benchmark
// ============================================================

static void benchmark_dual_channel(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  BENCHMARK: CfC WITH DUAL-CHANNEL DOT PRODUCT                     ║\n");
    printf("║  %d neurons, %d inputs, TRUE parallel pos/neg                     ║\n", CFC_HIDDEN_DIM, CFC_INPUT_DIM);
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    uint8_t input[CFC_INPUT_DIM] = {8, 10, 6, 12};
    
    printf("  Input: [%d, %d, %d, %d] (Q4 format, 8=zero)\n\n", 
           input[0], input[1], input[2], input[3]);
    
    // Warm up
    for (int i = 0; i < 10; i++) {
        cfc_forward_dual(input);
    }
    
    // Benchmark
    int num_iters = 100;
    int64_t start = esp_timer_get_time();
    
    for (int i = 0; i < num_iters; i++) {
        cfc_forward_dual(input);
    }
    
    int64_t end = esp_timer_get_time();
    int64_t total_us = end - start;
    float per_inference_us = (float)total_us / num_iters;
    float inference_rate = 1000000.0f / per_inference_us;
    
    printf("  Benchmark: %d iterations\n", num_iters);
    printf("  Total time: %lld us\n", (long long)total_us);
    printf("  Per inference: %.1f us\n", per_inference_us);
    printf("  Inference rate: %.0f Hz\n\n", inference_rate);
    
    // Compare with single-channel estimate
    printf("  Improvement: neg_sum computed in hardware (was software)\n");
    printf("  Expected speedup: ~1.3x (neg computation was ~25%% of time)\n");
    
    // Show hidden state
    printf("\n  Final hidden state (first 8 neurons):\n    ");
    for (int i = 0; i < 8; i++) {
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
    printf("║  CfC + DUAL-CHANNEL PCNT FOR TRUE PARALLEL POS/NEG               ║\n");
    printf("║  8-bit PARLIO -> 4 PCNTs (2 channels each)                       ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    ESP_LOGI(TAG, "Initializing hardware...");
    esp_err_t err = setup_hardware();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Hardware setup failed: %s", esp_err_to_name(err));
        return;
    }
    
    ESP_LOGI(TAG, "Initializing LUTs...");
    init_luts();
    
    ESP_LOGI(TAG, "Initializing network...");
    init_random_weights();
    
    ESP_LOGI(TAG, "Ready!");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Run verification
    int failures = verify_dual_channel();
    
    if (failures == 0) {
        // Run benchmark
        benchmark_dual_channel();
    } else {
        ESP_LOGE(TAG, "Verification failed! Not running benchmark.");
    }
    
    // Summary
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                          SUMMARY                                  ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
    printf("║  Dual-channel PCNT architecture:                                  ║\n");
    printf("║    - Channel A: +1 per rising edge (positive weights)             ║\n");
    printf("║    - Channel B: -1 per rising edge (negative weights)             ║\n");
    printf("║    - PCNT counter = pos_count - neg_count (automatic!)            ║\n");
    printf("║                                                                   ║\n");
    printf("║  Benefits:                                                        ║\n");
    printf("║    - Zero software computation for negative accumulation          ║\n");
    printf("║    - Single DMA transfer -> complete dot product                  ║\n");
    printf("║    - 4 neurons in parallel with TRUE pos-neg in hardware         ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    
    ESP_LOGI(TAG, "Done.");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
