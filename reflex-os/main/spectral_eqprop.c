/**
 * spectral_eqprop.c - Equilibrium Propagation for Spectral-Rotational CfC FFN
 *
 * THE BACKWARD PASS IS THE SAME DYNAMICS, PERTURBED
 *
 * Traditional Backprop:
 *   Forward: compute activations
 *   Backward: compute gradients (separate algorithm)
 *   Update: weights -= lr * gradients
 *
 * Equilibrium Propagation:
 *   Free Phase: let system evolve to equilibrium
 *   Nudged Phase: clamp output toward target, let system re-evolve
 *   Update: weights += lr * (correlations_nudged - correlations_free)
 *
 * For our Spectral FFN:
 *   Free Phase: oscillators evolve, coherence stabilizes
 *   Nudged Phase: inject phase error into output band, re-evolve
 *   Update: coupling += lr * (phase_correlations_nudged - phase_correlations_free)
 *
 * The learning rule emerges from the SAME DYNAMICS that compute the forward pass.
 * The strange loop learns.
 *
 * TASK: Learn to map input patterns to output phase relationships
 *   Input: 4-dimensional Q4 vector
 *   Output: Phase of Gamma band relative to Delta band
 *   Target: Specified phase relationship for each input class
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

static const char *TAG = "SPECTRAL_EQPROP";

// ============================================================
// Configuration
// ============================================================

#define NUM_BANDS           4
#define NEURONS_PER_BAND    4
#define TOTAL_NEURONS       (NUM_BANDS * NEURONS_PER_BAND)
#define INPUT_DIM           4
#define OUTPUT_DIM          1       // Phase difference Delta-Gamma

// Band indices
#define BAND_DELTA          0
#define BAND_THETA          1
#define BAND_ALPHA          2
#define BAND_GAMMA          3

// Equilibrium propagation parameters
#define FREE_PHASE_STEPS    10      // Steps to reach equilibrium
#define NUDGE_PHASE_STEPS   10      // Steps with output clamped
#define NUDGE_STRENGTH      0.3f    // How strongly to clamp output (beta)
#define LEARNING_RATE       0.01f   // Weight update magnitude

// Band characteristics
static const float BAND_DECAY[NUM_BANDS] = { 0.98f, 0.90f, 0.70f, 0.30f };
static const float BAND_FREQ[NUM_BANDS] = { 0.1f, 0.3f, 1.0f, 3.0f };

// Hardware
#define GPIO_CH0            4
#define GPIO_CH1            5
#define GPIO_CH2            6
#define GPIO_CH3            7
static const int gpio_nums[NUM_BANDS] = {GPIO_CH0, GPIO_CH1, GPIO_CH2, GPIO_CH3};

#define PARLIO_FREQ_HZ      10000000
#define PCNT_OVERFLOW_LIMIT 30000
#define MAX_PATTERN_BYTES   4096

// ============================================================
// Fixed-Point Math (Q15)
// ============================================================

typedef struct {
    int16_t real;
    int16_t imag;
} complex_q15_t;

#define Q15_ONE     32767
#define Q15_HALF    16384

#define TRIG_TABLE_SIZE     256
static int16_t sin_table[TRIG_TABLE_SIZE];
static int16_t cos_table[TRIG_TABLE_SIZE];

static void init_trig_tables(void) {
    for (int i = 0; i < TRIG_TABLE_SIZE; i++) {
        float angle = (2.0f * M_PI * i) / TRIG_TABLE_SIZE;
        sin_table[i] = (int16_t)(sinf(angle) * Q15_ONE);
        cos_table[i] = (int16_t)(cosf(angle) * Q15_ONE);
    }
}

static inline int16_t q15_sin(uint8_t angle_idx) { return sin_table[angle_idx]; }
static inline int16_t q15_cos(uint8_t angle_idx) { return cos_table[angle_idx]; }
static inline int16_t q15_mul(int16_t a, int16_t b) { return (int16_t)(((int32_t)a * b) >> 15); }

// ============================================================
// Network State
// ============================================================

typedef struct {
    // Oscillator states
    complex_q15_t oscillator[NUM_BANDS][NEURONS_PER_BAND];
    int16_t phase_velocity[NUM_BANDS][NEURONS_PER_BAND];
    
    // LEARNABLE: Input projection weights (ternary)
    uint32_t input_pos_mask[NUM_BANDS][NEURONS_PER_BAND];
    uint32_t input_neg_mask[NUM_BANDS][NEURONS_PER_BAND];
    
    // LEARNABLE: Cross-band coupling strengths
    float coupling[NUM_BANDS][NUM_BANDS];
    
    // Runtime
    int16_t coherence;
    
} spectral_network_t;

static spectral_network_t network;

// Snapshot for contrastive learning
typedef struct {
    // Phase correlations between bands (what we'll update based on)
    float band_correlation[NUM_BANDS][NUM_BANDS];
    
    // Output phase (what we're trying to learn)
    int16_t output_phase;
    
} network_snapshot_t;

static network_snapshot_t snapshot_free;
static network_snapshot_t snapshot_nudged;

// ============================================================
// Hardware Handles
// ============================================================

static pcnt_unit_handle_t pcnt_units[NUM_BANDS] = {NULL};
static pcnt_channel_handle_t pcnt_channels[NUM_BANDS] = {NULL};
static parlio_tx_unit_handle_t parlio = NULL;
static volatile int overflow_counts[NUM_BANDS] = {0};
static uint8_t *pattern_buffer = NULL;

// ============================================================
// Callbacks and Hardware Helpers
// ============================================================

static bool IRAM_ATTR pcnt_cb_0(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_counts[0]++; return false; }
static bool IRAM_ATTR pcnt_cb_1(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_counts[1]++; return false; }
static bool IRAM_ATTR pcnt_cb_2(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_counts[2]++; return false; }
static bool IRAM_ATTR pcnt_cb_3(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_counts[3]++; return false; }
static pcnt_watch_cb_t overflow_callbacks[NUM_BANDS] = { pcnt_cb_0, pcnt_cb_1, pcnt_cb_2, pcnt_cb_3 };

static int get_count(int band) {
    int val;
    pcnt_unit_get_count(pcnt_units[band], &val);
    return (overflow_counts[band] * PCNT_OVERFLOW_LIMIT) + val;
}

static void reset_counters(void) {
    for (int i = 0; i < NUM_BANDS; i++) {
        overflow_counts[i] = 0;
        pcnt_unit_clear_count(pcnt_units[i]);
    }
}

// ============================================================
// Hardware Setup
// ============================================================

static esp_err_t setup_hardware(void) {
    for (int i = 0; i < NUM_BANDS; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << gpio_nums[i]),
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }
    
    for (int i = 0; i < NUM_BANDS; i++) {
        pcnt_unit_config_t cfg = { .low_limit = -32768, .high_limit = PCNT_OVERFLOW_LIMIT };
        ESP_ERROR_CHECK(pcnt_new_unit(&cfg, &pcnt_units[i]));
        
        pcnt_chan_config_t chan_cfg = { .edge_gpio_num = gpio_nums[i], .level_gpio_num = -1 };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_units[i], &chan_cfg, &pcnt_channels[i]));
        pcnt_channel_set_edge_action(pcnt_channels[i], PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        
        pcnt_unit_add_watch_point(pcnt_units[i], PCNT_OVERFLOW_LIMIT);
        pcnt_event_callbacks_t cbs = { .on_reach = overflow_callbacks[i] };
        pcnt_unit_register_event_callbacks(pcnt_units[i], &cbs, NULL);
        
        pcnt_unit_enable(pcnt_units[i]);
        pcnt_unit_start(pcnt_units[i]);
    }
    
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
    for (int i = 0; i < NUM_BANDS; i++) {
        parlio_cfg.data_gpio_nums[i] = gpio_nums[i];
    }
    for (int i = NUM_BANDS; i < 16; i++) {
        parlio_cfg.data_gpio_nums[i] = -1;
    }
    
    ESP_ERROR_CHECK(parlio_new_tx_unit(&parlio_cfg, &parlio));
    parlio_tx_unit_enable(parlio);
    
    pattern_buffer = heap_caps_aligned_alloc(4, MAX_PATTERN_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    
    return ESP_OK;
}

// ============================================================
// Phase and Magnitude Helpers
// ============================================================

static uint8_t get_phase_idx(complex_q15_t* z) {
    int16_t r = z->real;
    int16_t i = z->imag;
    
    int quadrant = 0;
    if (r < 0) { r = -r; quadrant |= 2; }
    if (i < 0) { i = -i; quadrant |= 1; }
    
    int angle;
    if (r > i) {
        angle = (i * 32) / (r + 1);
    } else {
        angle = 64 - (r * 32) / (i + 1);
    }
    
    switch (quadrant) {
        case 0: return angle;
        case 2: return 128 - angle;
        case 3: return 128 + angle;
        case 1: return 256 - angle;
    }
    return 0;
}

static int16_t get_magnitude(complex_q15_t* z) {
    int32_t r = z->real;
    int32_t i = z->imag;
    if (r < 0) { r = -r; }
    if (i < 0) { i = -i; }
    if (r > i) {
        return (int16_t)(r + ((i * 13) >> 5));
    } else {
        return (int16_t)(i + ((r * 13) >> 5));
    }
}

// ============================================================
// Initialize Network
// ============================================================

static void init_network(void) {
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            uint8_t phase = esp_random() & 0xFF;
            network.oscillator[b][n].real = q15_cos(phase);
            network.oscillator[b][n].imag = q15_sin(phase);
            network.phase_velocity[b][n] = (int16_t)(BAND_FREQ[b] * 1000);
            
            network.input_pos_mask[b][n] = 0;
            network.input_neg_mask[b][n] = 0;
            for (int i = 0; i < INPUT_DIM; i++) {
                int r = esp_random() % 3;
                if (r == 0) {
                    network.input_pos_mask[b][n] |= (1 << i);
                } else if (r == 1) {
                    network.input_neg_mask[b][n] |= (1 << i);
                }
            }
        }
    }
    
    // Initialize coupling matrix
    for (int i = 0; i < NUM_BANDS; i++) {
        for (int j = 0; j < NUM_BANDS; j++) {
            if (i == j) {
                network.coupling[i][j] = 0.0f;
            } else {
                network.coupling[i][j] = 0.1f + 0.1f * ((float)(esp_random() % 100) / 100.0f);
            }
        }
    }
    
    network.coherence = 0;
}

// ============================================================
// Reset oscillators to initial conditions (for fresh forward pass)
// ============================================================

static void reset_oscillators(void) {
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            uint8_t phase = esp_random() & 0xFF;
            network.oscillator[b][n].real = q15_cos(phase);
            network.oscillator[b][n].imag = q15_sin(phase);
            network.phase_velocity[b][n] = (int16_t)(BAND_FREQ[b] * 1000);
        }
    }
}

// ============================================================
// Single Evolution Step
// ============================================================

static void evolve_step(const uint8_t* input_q4, int16_t* nudge_phase, float nudge_strength) {
    // 1. Inject input energy
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            int energy = 0;
            for (int i = 0; i < INPUT_DIM; i++) {
                if (network.input_pos_mask[b][n] & (1 << i)) { energy += input_q4[i]; }
                if (network.input_neg_mask[b][n] & (1 << i)) { energy -= input_q4[i]; }
            }
            
            int16_t mag = get_magnitude(&network.oscillator[b][n]);
            if (mag < Q15_HALF) {
                network.oscillator[b][n].real += energy * 50;
                network.oscillator[b][n].imag += energy * 25;
            }
        }
    }
    
    // 2. Rotate oscillators
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            uint8_t angle_idx = (uint8_t)((network.phase_velocity[b][n] >> 8) & 0xFF);
            int16_t c = q15_cos(angle_idx);
            int16_t s = q15_sin(angle_idx);
            
            int16_t new_real = q15_mul(network.oscillator[b][n].real, c) - q15_mul(network.oscillator[b][n].imag, s);
            int16_t new_imag = q15_mul(network.oscillator[b][n].real, s) + q15_mul(network.oscillator[b][n].imag, c);
            
            int16_t decay_q15 = (int16_t)(BAND_DECAY[b] * Q15_ONE);
            network.oscillator[b][n].real = q15_mul(new_real, decay_q15);
            network.oscillator[b][n].imag = q15_mul(new_imag, decay_q15);
        }
    }
    
    // 3. Cross-band coupling (Kuramoto)
    int32_t velocity_delta[NUM_BANDS][NEURONS_PER_BAND] = {0};
    for (int src = 0; src < NUM_BANDS; src++) {
        for (int dst = 0; dst < NUM_BANDS; dst++) {
            if (src == dst) { continue; }
            float strength = network.coupling[src][dst];
            if (strength < 0.01f) { continue; }
            
            int32_t phase_diff_sum = 0;
            for (int n = 0; n < NEURONS_PER_BAND; n++) {
                uint8_t src_phase = get_phase_idx(&network.oscillator[src][n]);
                uint8_t dst_phase = get_phase_idx(&network.oscillator[dst][n]);
                int diff = (int)src_phase - (int)dst_phase;
                while (diff > 127) { diff -= 256; }
                while (diff < -128) { diff += 256; }
                phase_diff_sum += diff;
            }
            int avg_diff = phase_diff_sum / NEURONS_PER_BAND;
            int16_t pull = (int16_t)(strength * avg_diff * 10);
            
            for (int n = 0; n < NEURONS_PER_BAND; n++) {
                velocity_delta[dst][n] += pull;
            }
        }
    }
    
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            network.phase_velocity[b][n] += velocity_delta[b][n] / 10;
            if (network.phase_velocity[b][n] > 10000) { network.phase_velocity[b][n] = 10000; }
            if (network.phase_velocity[b][n] < -10000) { network.phase_velocity[b][n] = -10000; }
        }
    }
    
    // 4. NUDGE: If we have a target phase, push the output band toward it
    if (nudge_phase != NULL && nudge_strength > 0.0f) {
        // Output = Gamma band phase relative to Delta band
        uint8_t gamma_phase = get_phase_idx(&network.oscillator[BAND_GAMMA][0]);
        uint8_t delta_phase = get_phase_idx(&network.oscillator[BAND_DELTA][0]);
        int16_t current_output = (int16_t)gamma_phase - (int16_t)delta_phase;
        
        // Error = target - current
        int16_t error = *nudge_phase - current_output;
        while (error > 127) { error -= 256; }
        while (error < -128) { error += 256; }
        
        // Nudge Gamma band toward target phase
        int16_t nudge_amount = (int16_t)(error * nudge_strength);
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            network.phase_velocity[BAND_GAMMA][n] += nudge_amount;
        }
    }
    
    // 5. Compute coherence
    int32_t sum_real = 0, sum_imag = 0;
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            sum_real += network.oscillator[b][n].real;
            sum_imag += network.oscillator[b][n].imag;
        }
    }
    sum_real /= TOTAL_NEURONS;
    sum_imag /= TOTAL_NEURONS;
    complex_q15_t avg = { .real = (int16_t)sum_real, .imag = (int16_t)sum_imag };
    network.coherence = get_magnitude(&avg);
}

// ============================================================
// Take Snapshot of Network State
// ============================================================

static void take_snapshot(network_snapshot_t* snapshot) {
    // Compute phase correlations between all band pairs
    for (int i = 0; i < NUM_BANDS; i++) {
        for (int j = 0; j < NUM_BANDS; j++) {
            if (i == j) {
                snapshot->band_correlation[i][j] = 1.0f;
                continue;
            }
            
            // Correlation = cos(phase_i - phase_j) averaged over neurons
            float corr_sum = 0.0f;
            for (int n = 0; n < NEURONS_PER_BAND; n++) {
                uint8_t phase_i = get_phase_idx(&network.oscillator[i][n]);
                uint8_t phase_j = get_phase_idx(&network.oscillator[j][n]);
                int diff = (int)phase_i - (int)phase_j;
                // Correlation = cos(diff * 2pi / 256)
                float angle = (float)diff * 2.0f * M_PI / 256.0f;
                corr_sum += cosf(angle);
            }
            snapshot->band_correlation[i][j] = corr_sum / NEURONS_PER_BAND;
        }
    }
    
    // Compute output phase
    uint8_t gamma_phase = get_phase_idx(&network.oscillator[BAND_GAMMA][0]);
    uint8_t delta_phase = get_phase_idx(&network.oscillator[BAND_DELTA][0]);
    snapshot->output_phase = (int16_t)gamma_phase - (int16_t)delta_phase;
}

// ============================================================
// Equilibrium Propagation: Full Learning Step
// ============================================================

static float eqprop_learn(const uint8_t* input_q4, int16_t target_phase) {
    // ========== FREE PHASE ==========
    // Let system evolve without output constraint
    reset_oscillators();
    
    for (int t = 0; t < FREE_PHASE_STEPS; t++) {
        evolve_step(input_q4, NULL, 0.0f);
    }
    
    take_snapshot(&snapshot_free);
    
    // ========== NUDGED PHASE ==========
    // Continue evolution with output clamped toward target
    for (int t = 0; t < NUDGE_PHASE_STEPS; t++) {
        evolve_step(input_q4, &target_phase, NUDGE_STRENGTH);
    }
    
    take_snapshot(&snapshot_nudged);
    
    // ========== WEIGHT UPDATE ==========
    // ΔW_ij ∝ (correlation_nudged - correlation_free)
    // This is the key insight: the weight update is a LOCAL computation
    // based on the difference in correlations between the two phases
    
    for (int i = 0; i < NUM_BANDS; i++) {
        for (int j = 0; j < NUM_BANDS; j++) {
            if (i == j) { continue; }
            
            float delta_corr = snapshot_nudged.band_correlation[i][j] - 
                               snapshot_free.band_correlation[i][j];
            
            // Update coupling: if nudging increased correlation, strengthen coupling
            network.coupling[i][j] += LEARNING_RATE * delta_corr;
            
            // Keep coupling in reasonable range
            if (network.coupling[i][j] < 0.01f) { network.coupling[i][j] = 0.01f; }
            if (network.coupling[i][j] > 1.0f) { network.coupling[i][j] = 1.0f; }
        }
    }
    
    // Return the loss (phase error)
    int16_t error = target_phase - snapshot_free.output_phase;
    while (error > 127) { error -= 256; }
    while (error < -128) { error += 256; }
    
    return (float)(error * error) / (256.0f * 256.0f);
}

// ============================================================
// Forward Pass Only (for inference)
// ============================================================

static int16_t forward_pass(const uint8_t* input_q4) {
    reset_oscillators();
    
    for (int t = 0; t < FREE_PHASE_STEPS; t++) {
        evolve_step(input_q4, NULL, 0.0f);
    }
    
    uint8_t gamma_phase = get_phase_idx(&network.oscillator[BAND_GAMMA][0]);
    uint8_t delta_phase = get_phase_idx(&network.oscillator[BAND_DELTA][0]);
    return (int16_t)gamma_phase - (int16_t)delta_phase;
}

// ============================================================
// Training Task: Learn to map 4 input patterns to 4 output phases
// ============================================================

static void train_pattern_mapping(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  EQUILIBRIUM PROPAGATION TRAINING                                 ║\n");
    printf("║  Learn to map 4 input patterns to 4 output phases                 ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Training data: 4 patterns, 4 target phases
    uint8_t patterns[4][INPUT_DIM] = {
        {4, 4, 12, 12},    // Pattern 0: low-low-high-high
        {12, 4, 4, 12},    // Pattern 1: high-low-low-high
        {4, 12, 12, 4},    // Pattern 2: low-high-high-low
        {12, 12, 4, 4},    // Pattern 3: high-high-low-low
    };
    
    // Target phases (spread across circle)
    int16_t targets[4] = {
        0,      // Pattern 0 → phase 0
        64,     // Pattern 1 → phase 64 (90°)
        128,    // Pattern 2 → phase 128 (180°)
        192,    // Pattern 3 → phase 192 (270°)
    };
    
    printf("  Training data:\n");
    for (int p = 0; p < 4; p++) {
        printf("    Pattern %d: [%d,%d,%d,%d] → target phase %d\n",
               p, patterns[p][0], patterns[p][1], patterns[p][2], patterns[p][3], targets[p]);
    }
    printf("\n");
    
    // Training loop
    int num_epochs = 50;
    printf("  Epoch | Loss    | Coupling[0][3] | Output phases\n");
    printf("  ------+---------+----------------+---------------\n");
    
    for (int epoch = 0; epoch < num_epochs; epoch++) {
        float total_loss = 0.0f;
        
        // Train on each pattern
        for (int p = 0; p < 4; p++) {
            float loss = eqprop_learn(patterns[p], targets[p]);
            total_loss += loss;
        }
        
        // Evaluate
        int16_t outputs[4];
        for (int p = 0; p < 4; p++) {
            outputs[p] = forward_pass(patterns[p]);
        }
        
        if (epoch % 5 == 0 || epoch == num_epochs - 1) {
            printf("  %5d | %.5f |     %.3f      | %4d %4d %4d %4d\n",
                   epoch, total_loss / 4.0f, network.coupling[0][3],
                   outputs[0], outputs[1], outputs[2], outputs[3]);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Final evaluation
    printf("\n  Final Results:\n");
    printf("  Pattern | Target | Output | Error\n");
    printf("  --------+--------+--------+-------\n");
    
    int total_error = 0;
    for (int p = 0; p < 4; p++) {
        int16_t output = forward_pass(patterns[p]);
        int16_t error = targets[p] - output;
        while (error > 127) { error -= 256; }
        while (error < -128) { error += 256; }
        if (error < 0) { error = -error; }
        total_error += error;
        
        printf("     %d    |  %4d  |  %4d  |  %3d\n",
               p, targets[p], output, error);
    }
    
    printf("\n  Average error: %.1f (out of 256 max)\n", (float)total_error / 4.0f);
    
    printf("\n  Final coupling matrix:\n");
    for (int i = 0; i < NUM_BANDS; i++) {
        printf("    ");
        for (int j = 0; j < NUM_BANDS; j++) {
            printf("%.3f ", network.coupling[i][j]);
        }
        printf("\n");
    }
}

// ============================================================
// Benchmark
// ============================================================

static void benchmark_eqprop(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  EQUILIBRIUM PROPAGATION BENCHMARK                                ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    uint8_t input[INPUT_DIM] = {8, 10, 6, 12};
    int16_t target = 64;
    
    // Benchmark learning step
    int num_iters = 20;
    int64_t start = esp_timer_get_time();
    
    for (int i = 0; i < num_iters; i++) {
        eqprop_learn(input, target);
    }
    
    int64_t end = esp_timer_get_time();
    float per_learn_us = (float)(end - start) / num_iters;
    
    printf("  Learning step: %.1f us (%.0f Hz)\n", per_learn_us, 1000000.0f / per_learn_us);
    
    // Benchmark inference only
    start = esp_timer_get_time();
    
    for (int i = 0; i < 100; i++) {
        forward_pass(input);
    }
    
    end = esp_timer_get_time();
    float per_inference_us = (float)(end - start) / 100;
    
    printf("  Inference only: %.1f us (%.0f Hz)\n", per_inference_us, 1000000.0f / per_inference_us);
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  EQUILIBRIUM PROPAGATION FOR SPECTRAL CfC FFN                     ║\n");
    printf("║  The backward pass IS the forward dynamics, perturbed             ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    ESP_LOGI(TAG, "Initializing trig tables...");
    init_trig_tables();
    
    ESP_LOGI(TAG, "Setting up hardware...");
    ESP_ERROR_CHECK(setup_hardware());
    
    ESP_LOGI(TAG, "Initializing network...");
    init_network();
    
    ESP_LOGI(TAG, "Ready!");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Run benchmark
    benchmark_eqprop();
    
    // Run training
    train_pattern_mapping();
    
    // Summary
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                          SUMMARY                                  ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
    printf("║  Equilibrium Propagation:                                         ║\n");
    printf("║    - Free phase: system evolves to equilibrium                    ║\n");
    printf("║    - Nudged phase: output clamped, system re-evolves              ║\n");
    printf("║    - Weight update: Δw ∝ (corr_nudged - corr_free)                ║\n");
    printf("║                                                                   ║\n");
    printf("║  Key Insight:                                                     ║\n");
    printf("║    The backward pass uses THE SAME DYNAMICS as forward            ║\n");
    printf("║    No separate gradient computation                               ║\n");
    printf("║    Learning emerges from contrastive perturbation                 ║\n");
    printf("║                                                                   ║\n");
    printf("║  The strange loop learns.                                         ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    
    ESP_LOGI(TAG, "Complete.");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
