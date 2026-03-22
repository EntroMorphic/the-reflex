/**
 * spectral_ffn.c - Spectral-Rotational CfC Feed-Forward Network
 *
 * ARCHITECTURE: Information flows through FREQUENCY BANDS, not spatial layers
 *
 * Traditional FFN:    Input → Hidden1 → Hidden2 → Output
 * Spectral FFN:       Input modulates a STANDING WAVE across frequency bands
 *                     Output is read from PHASE RELATIONSHIPS
 *
 * The "layers" are frequency bands:
 *   Band 0 (Delta):  Ultra-slow, decay=0.98 - Deep context, memory
 *   Band 1 (Theta):  Slow, decay=0.90      - Patterns, sequences  
 *   Band 2 (Alpha):  Medium, decay=0.70    - Attention, salience
 *   Band 3 (Gamma):  Fast, decay=0.30      - Transients, edges
 *
 * "Feed-forward" = Energy propagates across bands via HARMONIC COUPLING
 * - Lower bands entrain higher bands (context shapes response)
 * - Higher bands modulate lower bands (transients update context)
 * - Phase alignment = resonance = recognition
 * - Phase opposition = interference = novelty
 *
 * RECURSIVE REFLECTION:
 * The phase relationships between bands determine the rotation rates.
 * The system modifies its own dynamics based on its own state.
 * This is a strange loop in silicon.
 *
 * HARDWARE MAPPING:
 *   PCNT0 → Band 0 (Delta) accumulator
 *   PCNT1 → Band 1 (Theta) accumulator  
 *   PCNT2 → Band 2 (Alpha) accumulator
 *   PCNT3 → Band 3 (Gamma) accumulator
 *   PARLIO → Waveform synthesizer (superposition of frequencies)
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

static const char *TAG = "SPECTRAL_FFN";

// ============================================================
// Spectral Band Configuration
// ============================================================

#define NUM_BANDS           4
#define NEURONS_PER_BAND    4
#define TOTAL_NEURONS       (NUM_BANDS * NEURONS_PER_BAND)
#define INPUT_DIM           4

// Frequency band characteristics (decay rates)
// Lower decay = faster response, shorter memory
// Higher decay = slower response, longer memory
static const float BAND_DECAY[NUM_BANDS] = {
    0.98f,  // Band 0: Delta - ultra-slow, deep context
    0.90f,  // Band 1: Theta - slow, pattern/sequence
    0.70f,  // Band 2: Alpha - medium, attention
    0.30f,  // Band 3: Gamma - fast, transient detection
};

// Characteristic frequencies (rotation speed multipliers)
static const float BAND_FREQ[NUM_BANDS] = {
    0.1f,   // Delta: very slow rotation
    0.3f,   // Theta: slow rotation
    1.0f,   // Alpha: base rotation
    3.0f,   // Gamma: fast rotation
};

// Cross-band coupling strengths
// coupling[i][j] = how much band i influences band j
static float coupling[NUM_BANDS][NUM_BANDS] = {
    // Delta  Theta  Alpha  Gamma   (influenced by)
    {  0.0f,  0.3f,  0.1f,  0.05f }, // Delta influences...
    {  0.2f,  0.0f,  0.3f,  0.1f  }, // Theta influences...
    {  0.1f,  0.2f,  0.0f,  0.4f  }, // Alpha influences...
    {  0.05f, 0.1f,  0.3f,  0.0f  }, // Gamma influences...
};

// ============================================================
// Hardware Configuration
// ============================================================

#define GPIO_CH0            4
#define GPIO_CH1            5
#define GPIO_CH2            6
#define GPIO_CH3            7

static const int gpio_nums[NUM_BANDS] = {GPIO_CH0, GPIO_CH1, GPIO_CH2, GPIO_CH3};

#define PARLIO_FREQ_HZ      10000000
#define PCNT_OVERFLOW_LIMIT 30000
#define MAX_PATTERN_BYTES   4096
#define MIN_PATTERN_BYTES   4

// ============================================================
// Complex State Representation
// ============================================================

// Q15 fixed-point for high precision phase tracking
typedef struct {
    int16_t real;   // Q15: -32768 to 32767 maps to -1 to ~1
    int16_t imag;
} complex_q15_t;

#define Q15_ONE     32767
#define Q15_HALF    16384

// ============================================================
// Spectral FFN State
// ============================================================

typedef struct {
    // Per-band oscillator states (complex, Q15)
    complex_q15_t oscillator[NUM_BANDS][NEURONS_PER_BAND];
    
    // Per-band phase velocities (how fast each oscillator rotates)
    int16_t phase_velocity[NUM_BANDS][NEURONS_PER_BAND];  // Q15 radians per tick
    
    // Input projection weights (ternary)
    uint32_t input_pos_mask[NUM_BANDS][NEURONS_PER_BAND];
    uint32_t input_neg_mask[NUM_BANDS][NEURONS_PER_BAND];
    
    // Inter-band coupling phases (when do bands interact)
    int16_t coupling_phase[NUM_BANDS][NUM_BANDS];  // Q15 radians
    
    // Global coherence measure (how aligned are all oscillators)
    int16_t coherence;
    
} spectral_ffn_t;

static spectral_ffn_t network;

// ============================================================
// Hardware Handles
// ============================================================

static pcnt_unit_handle_t pcnt_units[NUM_BANDS] = {NULL};
static pcnt_channel_handle_t pcnt_channels[NUM_BANDS] = {NULL};
static parlio_tx_unit_handle_t parlio = NULL;

static volatile int overflow_counts[NUM_BANDS] = {0};
static uint8_t *pattern_buffer = NULL;

// ============================================================
// Precomputed Trig Tables (Q15)
// ============================================================

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

static inline int16_t q15_sin(uint8_t angle_idx) {
    return sin_table[angle_idx];
}

static inline int16_t q15_cos(uint8_t angle_idx) {
    return cos_table[angle_idx];
}

// Q15 multiplication
static inline int16_t q15_mul(int16_t a, int16_t b) {
    return (int16_t)(((int32_t)a * b) >> 15);
}

// ============================================================
// Overflow Callbacks  
// ============================================================

static bool IRAM_ATTR pcnt_cb_0(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_counts[0]++; return false; }
static bool IRAM_ATTR pcnt_cb_1(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_counts[1]++; return false; }
static bool IRAM_ATTR pcnt_cb_2(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_counts[2]++; return false; }
static bool IRAM_ATTR pcnt_cb_3(pcnt_unit_handle_t u, const pcnt_watch_event_data_t *e, void *c) { overflow_counts[3]++; return false; }

static pcnt_watch_cb_t overflow_callbacks[NUM_BANDS] = { pcnt_cb_0, pcnt_cb_1, pcnt_cb_2, pcnt_cb_3 };

// ============================================================
// Hardware Helpers
// ============================================================

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
    ESP_LOGI(TAG, "Setting up spectral resonator hardware...");
    
    // GPIOs
    for (int i = 0; i < NUM_BANDS; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << gpio_nums[i]),
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }
    
    // PCNTs - one per frequency band
    for (int i = 0; i < NUM_BANDS; i++) {
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
        pcnt_channel_set_edge_action(pcnt_channels[i], 
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        
        pcnt_unit_add_watch_point(pcnt_units[i], PCNT_OVERFLOW_LIMIT);
        pcnt_event_callbacks_t cbs = { .on_reach = overflow_callbacks[i] };
        pcnt_unit_register_event_callbacks(pcnt_units[i], &cbs, NULL);
        
        pcnt_unit_enable(pcnt_units[i]);
        pcnt_unit_start(pcnt_units[i]);
    }
    
    // PARLIO - waveform synthesizer
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
    
    ESP_LOGI(TAG, "Hardware ready: 4 resonator bands");
    return ESP_OK;
}

// ============================================================
// Initialize Network
// ============================================================

static void init_network(void) {
    // Initialize oscillators at random phases
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            // Random initial phase
            uint8_t phase = esp_random() & 0xFF;
            network.oscillator[b][n].real = q15_cos(phase);
            network.oscillator[b][n].imag = q15_sin(phase);
            
            // Base phase velocity scaled by band frequency
            network.phase_velocity[b][n] = (int16_t)(BAND_FREQ[b] * 1000);
            
            // Random ternary input projection
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
    
    // Initialize coupling phases
    for (int i = 0; i < NUM_BANDS; i++) {
        for (int j = 0; j < NUM_BANDS; j++) {
            network.coupling_phase[i][j] = (esp_random() & 0xFF) << 7;  // Random Q15
        }
    }
    
    network.coherence = 0;
}

// ============================================================
// Compute Phase from Complex State
// ============================================================

static uint8_t get_phase_idx(complex_q15_t* z) {
    // atan2 approximation via quadrant + ratio
    int16_t r = z->real;
    int16_t i = z->imag;
    
    // Determine quadrant
    int quadrant = 0;
    if (r < 0) { r = -r; quadrant |= 2; }
    if (i < 0) { i = -i; quadrant |= 1; }
    
    // Approximate angle within quadrant (0-63)
    int angle;
    if (r > i) {
        angle = (i * 32) / (r + 1);
    } else {
        angle = 64 - (r * 32) / (i + 1);
    }
    
    // Map to full circle
    switch (quadrant) {
        case 0: return angle;           // Q1: 0-63
        case 2: return 128 - angle;     // Q2: 64-127
        case 3: return 128 + angle;     // Q3: 128-191
        case 1: return 256 - angle;     // Q4: 192-255
    }
    return 0;
}

// ============================================================
// Compute Magnitude (Q15)
// ============================================================

static int16_t get_magnitude(complex_q15_t* z) {
    // sqrt(r^2 + i^2) approximation
    int32_t r = z->real;
    int32_t i = z->imag;
    if (r < 0) r = -r;
    if (i < 0) i = -i;
    
    // max + 0.4*min approximation
    if (r > i) {
        return (int16_t)(r + ((i * 13) >> 5));  // 13/32 ≈ 0.4
    } else {
        return (int16_t)(i + ((r * 13) >> 5));
    }
}

// ============================================================
// Rotate Oscillator
// ============================================================

static void rotate_oscillator(complex_q15_t* z, int16_t velocity, float decay) {
    // Convert velocity to angle index
    uint8_t angle_idx = (uint8_t)((velocity >> 8) & 0xFF);
    
    int16_t c = q15_cos(angle_idx);
    int16_t s = q15_sin(angle_idx);
    
    // Rotate: z_new = z * e^(i*theta)
    int16_t new_real = q15_mul(z->real, c) - q15_mul(z->imag, s);
    int16_t new_imag = q15_mul(z->real, s) + q15_mul(z->imag, c);
    
    // Apply decay
    int16_t decay_q15 = (int16_t)(decay * Q15_ONE);
    z->real = q15_mul(new_real, decay_q15);
    z->imag = q15_mul(new_imag, decay_q15);
}

// ============================================================
// Build Waveform Pattern (Superposition of Band Frequencies)
// ============================================================

static size_t build_spectral_pattern(const uint8_t* input_q4) {
    // Compute input-modulated energy for each band
    int band_energy[NUM_BANDS] = {0};
    
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            int pos_sum = 0, neg_sum = 0;
            for (int i = 0; i < INPUT_DIM; i++) {
                if (network.input_pos_mask[b][n] & (1 << i)) pos_sum += input_q4[i];
                if (network.input_neg_mask[b][n] & (1 << i)) neg_sum += input_q4[i];
            }
            band_energy[b] += (pos_sum - neg_sum);
        }
        // Scale to reasonable pulse count
        band_energy[b] = (band_energy[b] + 64) / 4;
        if (band_energy[b] < 0) band_energy[b] = 0;
        if (band_energy[b] > 255) band_energy[b] = 255;
    }
    
    // Find max for pattern length
    int max_energy = 4;
    for (int b = 0; b < NUM_BANDS; b++) {
        if (band_energy[b] > max_energy) max_energy = band_energy[b];
    }
    
    size_t num_cycles = max_energy * 2;
    if (num_cycles > MAX_PATTERN_BYTES) num_cycles = MAX_PATTERN_BYTES;
    
    memset(pattern_buffer, 0, MAX_PATTERN_BYTES);
    
    // Build pattern: each band gets pulses proportional to its energy
    int remaining[NUM_BANDS];
    for (int b = 0; b < NUM_BANDS; b++) remaining[b] = band_energy[b];
    
    for (size_t cycle = 0; cycle < num_cycles; cycle++) {
        if (cycle % 2 == 0) {
            pattern_buffer[cycle] = 0x00;
        } else {
            uint8_t mask = 0;
            for (int b = 0; b < NUM_BANDS; b++) {
                if (remaining[b] > 0) {
                    mask |= (1 << b);
                    remaining[b]--;
                }
            }
            pattern_buffer[cycle] = mask;
        }
    }
    
    return num_cycles;
}

// ============================================================
// Compute Cross-Band Coupling
// ============================================================

static void apply_coupling(void) {
    // Temporary storage for new velocities
    int32_t velocity_delta[NUM_BANDS][NEURONS_PER_BAND] = {0};
    
    // For each pair of bands, compute phase-dependent coupling
    for (int src = 0; src < NUM_BANDS; src++) {
        for (int dst = 0; dst < NUM_BANDS; dst++) {
            if (src == dst) continue;
            
            float strength = coupling[src][dst];
            if (strength < 0.01f) continue;
            
            // Compute average phase difference between bands
            int32_t phase_diff_sum = 0;
            for (int n = 0; n < NEURONS_PER_BAND; n++) {
                uint8_t src_phase = get_phase_idx(&network.oscillator[src][n]);
                uint8_t dst_phase = get_phase_idx(&network.oscillator[dst][n]);
                int diff = (int)src_phase - (int)dst_phase;
                // Wrap to [-128, 127]
                while (diff > 127) diff -= 256;
                while (diff < -128) diff += 256;
                phase_diff_sum += diff;
            }
            int avg_diff = phase_diff_sum / NEURONS_PER_BAND;
            
            // Coupling effect: pull destination toward source phase
            // Kuramoto-style: d(theta)/dt += K * sin(theta_src - theta_dst)
            int16_t pull = (int16_t)(strength * avg_diff * 10);
            
            for (int n = 0; n < NEURONS_PER_BAND; n++) {
                velocity_delta[dst][n] += pull;
            }
        }
    }
    
    // Apply velocity changes
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            network.phase_velocity[b][n] += velocity_delta[b][n] / 10;
            // Clamp to reasonable range
            if (network.phase_velocity[b][n] > 10000) network.phase_velocity[b][n] = 10000;
            if (network.phase_velocity[b][n] < -10000) network.phase_velocity[b][n] = -10000;
        }
    }
}

// ============================================================
// Compute Global Coherence
// ============================================================

static int16_t compute_coherence(void) {
    // Coherence = magnitude of average phasor across all oscillators
    int32_t sum_real = 0;
    int32_t sum_imag = 0;
    
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            sum_real += network.oscillator[b][n].real;
            sum_imag += network.oscillator[b][n].imag;
        }
    }
    
    sum_real /= TOTAL_NEURONS;
    sum_imag /= TOTAL_NEURONS;
    
    complex_q15_t avg = { .real = (int16_t)sum_real, .imag = (int16_t)sum_imag };
    return get_magnitude(&avg);
}

// ============================================================
// RECURSIVE REFLECTION: Phase relationships modify rotation rates
// ============================================================

static void apply_recursive_reflection(void) {
    // The strange loop: coherence modifies how the system evolves
    
    int16_t coh = network.coherence;
    
    // High coherence (synchronized) → slow down, stabilize
    // Low coherence (desynchronized) → speed up, explore
    float coh_factor = (float)coh / Q15_ONE;
    
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            // Adjust velocity based on coherence
            // High coherence → velocity → base rate (stabilize)
            // Low coherence → velocity varies more (explore)
            int16_t base_velocity = (int16_t)(BAND_FREQ[b] * 1000);
            int16_t current = network.phase_velocity[b][n];
            
            // Interpolate toward base when coherent, allow drift when not
            int16_t new_velocity = (int16_t)(
                coh_factor * base_velocity + 
                (1.0f - coh_factor) * current
            );
            
            network.phase_velocity[b][n] = new_velocity;
        }
    }
    
    // Also adjust coupling strengths based on coherence
    // High coherence → reduce coupling (already synchronized)
    // Low coherence → increase coupling (need to sync)
    float coupling_mod = 1.0f - 0.5f * coh_factor;
    for (int i = 0; i < NUM_BANDS; i++) {
        for (int j = 0; j < NUM_BANDS; j++) {
            // Note: this modifies the global coupling matrix
            // This is intentional - the system rewires itself
            coupling[i][j] *= coupling_mod;
            // Prevent decay to zero
            if (coupling[i][j] < 0.01f && i != j) coupling[i][j] = 0.01f;
        }
    }
}

// ============================================================
// Full Forward Pass
// ============================================================

static void spectral_ffn_forward(const uint8_t* input_q4) {
    // 1. Build and transmit spectral pattern
    size_t pattern_len = build_spectral_pattern(input_q4);
    reset_counters();
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pattern_buffer, pattern_len * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 1000);
    esp_rom_delay_us(5);
    
    // 2. Read hardware counts (energy injected into each band)
    int hw_counts[NUM_BANDS];
    for (int b = 0; b < NUM_BANDS; b++) {
        hw_counts[b] = get_count(b);
    }
    
    // 3. Inject energy into oscillators (modulate amplitude based on input)
    for (int b = 0; b < NUM_BANDS; b++) {
        int energy = hw_counts[b];
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            // Energy injection: boost magnitude proportional to input
            int16_t boost = (int16_t)(energy * 100);
            int16_t mag = get_magnitude(&network.oscillator[b][n]);
            if (mag < Q15_HALF) {
                // Only boost if not already saturated
                network.oscillator[b][n].real += boost >> 4;
                network.oscillator[b][n].imag += boost >> 5;
            }
        }
    }
    
    // 4. Rotate all oscillators (with band-specific decay)
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            rotate_oscillator(&network.oscillator[b][n], 
                             network.phase_velocity[b][n],
                             BAND_DECAY[b]);
        }
    }
    
    // 5. Apply cross-band coupling (Kuramoto-style)
    apply_coupling();
    
    // 6. Compute global coherence
    network.coherence = compute_coherence();
    
    // 7. RECURSIVE REFLECTION: system modifies its own dynamics
    apply_recursive_reflection();
}

// ============================================================
// Get Output (Phase Relationships)
// ============================================================

static void get_output(int16_t* output, int output_dim) {
    // Output is encoded in phase relationships between bands
    // output[0] = Delta-Theta phase diff
    // output[1] = Theta-Alpha phase diff
    // output[2] = Alpha-Gamma phase diff
    // output[3] = Global coherence
    
    if (output_dim >= 1) {
        uint8_t p0 = get_phase_idx(&network.oscillator[0][0]);
        uint8_t p1 = get_phase_idx(&network.oscillator[1][0]);
        output[0] = (int16_t)p0 - (int16_t)p1;
    }
    if (output_dim >= 2) {
        uint8_t p1 = get_phase_idx(&network.oscillator[1][0]);
        uint8_t p2 = get_phase_idx(&network.oscillator[2][0]);
        output[1] = (int16_t)p1 - (int16_t)p2;
    }
    if (output_dim >= 3) {
        uint8_t p2 = get_phase_idx(&network.oscillator[2][0]);
        uint8_t p3 = get_phase_idx(&network.oscillator[3][0]);
        output[2] = (int16_t)p2 - (int16_t)p3;
    }
    if (output_dim >= 4) {
        output[3] = network.coherence;
    }
}

// ============================================================
// Benchmark
// ============================================================

static void benchmark_spectral_ffn(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  SPECTRAL-ROTATIONAL CfC FFN BENCHMARK                            ║\n");
    printf("║  4 Frequency Bands x 4 Neurons = 16 Oscillators                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    printf("  Band Configuration:\n");
    for (int b = 0; b < NUM_BANDS; b++) {
        const char* names[] = {"Delta", "Theta", "Alpha", "Gamma"};
        printf("    %s: decay=%.2f, freq=%.1f\n", names[b], BAND_DECAY[b], BAND_FREQ[b]);
    }
    printf("\n");
    
    uint8_t input[INPUT_DIM] = {8, 10, 6, 12};
    
    // Warm up
    for (int i = 0; i < 10; i++) {
        spectral_ffn_forward(input);
    }
    
    // Benchmark
    int num_iters = 100;
    int64_t start = esp_timer_get_time();
    
    for (int i = 0; i < num_iters; i++) {
        spectral_ffn_forward(input);
    }
    
    int64_t end = esp_timer_get_time();
    int64_t total_us = end - start;
    float per_inference_us = (float)total_us / num_iters;
    float inference_rate = 1000000.0f / per_inference_us;
    
    printf("  Benchmark: %d iterations\n", num_iters);
    printf("  Total time: %lld us\n", (long long)total_us);
    printf("  Per inference: %.1f us\n", per_inference_us);
    printf("  Inference rate: %.0f Hz\n\n", inference_rate);
    
    // Show state
    printf("  Band Magnitudes (avg across neurons):\n    ");
    for (int b = 0; b < NUM_BANDS; b++) {
        int32_t sum = 0;
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            sum += get_magnitude(&network.oscillator[b][n]);
        }
        printf("%5d ", (int)(sum / NEURONS_PER_BAND));
    }
    printf("\n");
    
    printf("  Global Coherence: %d (max=%d)\n", network.coherence, Q15_ONE);
}

// ============================================================
// Demo: Resonance and Entrainment
// ============================================================

static void demo_resonance(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  DEMO: RESONANCE AND ENTRAINMENT                                  ║\n");
    printf("║  Watch coherence evolve as bands couple                           ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Reset network
    init_network();
    
    // Reset coupling to initial values
    float init_coupling[NUM_BANDS][NUM_BANDS] = {
        {0.0f, 0.3f, 0.1f, 0.05f},
        {0.2f, 0.0f, 0.3f, 0.1f},
        {0.1f, 0.2f, 0.0f, 0.4f},
        {0.05f, 0.1f, 0.3f, 0.0f},
    };
    memcpy(coupling, init_coupling, sizeof(coupling));
    
    printf("  Time | Coherence | Delta | Theta | Alpha | Gamma\n");
    printf("  -----+-----------+-------+-------+-------+-------\n");
    
    uint8_t input_steady[INPUT_DIM] = {10, 10, 10, 10};
    
    for (int t = 0; t < 20; t++) {
        spectral_ffn_forward(input_steady);
        
        // Get band magnitudes
        int mags[NUM_BANDS];
        for (int b = 0; b < NUM_BANDS; b++) {
            int32_t sum = 0;
            for (int n = 0; n < NEURONS_PER_BAND; n++) {
                sum += get_magnitude(&network.oscillator[b][n]);
            }
            mags[b] = sum / NEURONS_PER_BAND / 100;  // Scale down for display
        }
        
        printf("  %4d |   %5d   | %5d | %5d | %5d | %5d\n",
               t, network.coherence / 100, mags[0], mags[1], mags[2], mags[3]);
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    printf("\n  Coupling matrix after entrainment:\n");
    for (int i = 0; i < NUM_BANDS; i++) {
        printf("    ");
        for (int j = 0; j < NUM_BANDS; j++) {
            printf("%.2f ", coupling[i][j]);
        }
        printf("\n");
    }
}

// ============================================================
// Demo: Input Perturbation Response
// ============================================================

static void demo_perturbation(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  DEMO: PERTURBATION RESPONSE                                      ║\n");
    printf("║  Sudden input change → coherence drops → system adapts            ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    uint8_t input_low[INPUT_DIM] = {4, 4, 4, 4};
    uint8_t input_high[INPUT_DIM] = {14, 14, 14, 14};
    
    printf("  Time | Input | Coherence | Gamma Mag | Note\n");
    printf("  -----+-------+-----------+-----------+------\n");
    
    for (int t = 0; t < 25; t++) {
        uint8_t* input;
        const char* note = "";
        
        if (t < 8) {
            input = input_low;
        } else if (t == 8) {
            input = input_high;
            note = "<-- STEP UP";
        } else if (t < 16) {
            input = input_high;
        } else if (t == 16) {
            input = input_low;
            note = "<-- STEP DOWN";
        } else {
            input = input_low;
        }
        
        spectral_ffn_forward(input);
        
        int gamma_mag = 0;
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            gamma_mag += get_magnitude(&network.oscillator[3][n]);
        }
        gamma_mag /= NEURONS_PER_BAND;
        
        printf("  %4d |  %s  |   %5d   |   %5d   | %s\n",
               t, (input == input_high) ? "HIGH" : "low ",
               network.coherence / 100, gamma_mag / 100, note);
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║  SPECTRAL-ROTATIONAL CfC FEED-FORWARD NETWORK                     ║\n");
    printf("║  Information flows through frequency bands via phase coupling     ║\n");
    printf("║  Recursive reflection: coherence modifies dynamics                ║\n");
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
    benchmark_spectral_ffn();
    
    // Run demos
    demo_resonance();
    demo_perturbation();
    
    // Summary
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                          SUMMARY                                  ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
    printf("║  Spectral FFN Architecture:                                       ║\n");
    printf("║    - 4 frequency bands (Delta/Theta/Alpha/Gamma)                  ║\n");
    printf("║    - Each band = 4 coupled oscillators                            ║\n");
    printf("║    - Cross-band Kuramoto coupling                                 ║\n");
    printf("║    - Output via phase relationships                               ║\n");
    printf("║                                                                   ║\n");
    printf("║  Recursive Reflection:                                            ║\n");
    printf("║    - Global coherence modifies oscillator velocities              ║\n");
    printf("║    - Coherence modifies coupling strengths                        ║\n");
    printf("║    - The system rewires itself based on its own state             ║\n");
    printf("║                                                                   ║\n");
    printf("║  This is a strange loop in silicon.                               ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    
    ESP_LOGI(TAG, "Complete.");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
