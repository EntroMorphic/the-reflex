/**
 * reflex_fabric_cfc.h - Pure ETM Fabric CfC (No CPU in Loop)
 *
 * The CPU sets up the fabric, then SLEEPS.
 * The fabric runs the entire 64-neuron CfC autonomously.
 * When done, ETM signals completion.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *                         THE ARCHITECTURE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *   The problem: CfC needs multiply for the mixer.
 *   The insight: Precompute EVERYTHING as LUTs.
 *
 *   Sparse dot product:
 *     - Convert to pulse count via RMT
 *     - PCNT accumulates (hardware addition!)
 *     - Result is small integer (-12 to +12 typical)
 *
 *   Activations (sigmoid, tanh):
 *     - Already LUTs in Yinsen Q15
 *     - Just memory reads
 *
 *   Mixer: h_new = (1-g) * h_prev * decay + g * candidate
 *     - This needs multiply!
 *     - Solution: precompute as 3D LUT
 *     - mixer_lut[gate_q][h_prev_q][cand_q] = result
 *     - At 4-bit quantization: 16×16×16 = 4096 entries per neuron
 *
 *   Total memory for 64 neurons:
 *     - sparse_dot_lut: not needed (PCNT does addition)
 *     - sigmoid_lut: 256 bytes (shared)
 *     - tanh_lut: 256 bytes (shared)
 *     - mixer_lut: 4096 × 64 = 256 KB (per neuron, 4-bit)
 *              or: 512 × 64 = 32 KB (per neuron, 3-bit)
 *
 *   We'll use 4-bit quantization (16 levels) for better accuracy.
 *   256 KB fits in ESP32-C6's 512 KB SRAM.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *                         THE FABRIC PIPELINE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *   SETUP (CPU does once):
 *     1. Build all LUTs
 *     2. Configure GDMA linked lists
 *     3. Configure ETM channels
 *     4. Start timer, go to sleep
 *
 *   INFERENCE (fabric does autonomously):
 *     For each neuron i:
 *       1. Clear PCNT
 *       2. For each nonzero weight in gate row:
 *          - RMT sends pulses based on input value
 *          - PCNT counts (accumulates sparse dot)
 *       3. GDMA reads sigmoid_lut[PCNT_value] → gate_q
 *       4. Clear PCNT
 *       5. For each nonzero weight in candidate row:
 *          - RMT sends pulses
 *          - PCNT counts
 *       6. GDMA reads tanh_lut[PCNT_value] → cand_q
 *       7. GDMA reads mixer_lut[gate_q][h_prev_q][cand_q] → h_new_q
 *       8. Store h_new_q
 *     ETM signals completion
 *
 *   RESULT (CPU wakes):
 *     1. Read hidden state
 *     2. Process or go back to sleep
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *                         QUANTIZATION
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *   4-bit quantization maps continuous values to 16 discrete levels.
 *
 *   For values in [-1, +1]:
 *     q = (int)((value + 1.0) * 7.5)  // 0-15
 *     value = (q / 7.5) - 1.0         // back to float
 *
 *   Precision: 1/16 = 0.0625 = 6.25% steps
 *   This is coarser than Q15 but sufficient for LUT indexing.
 *
 *   The hidden state remains Q15 internally; we only quantize for LUT access.
 */

#ifndef REFLEX_FABRIC_CFC_H
#define REFLEX_FABRIC_CFC_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_etm.h"
#include "driver/gpio_etm.h"

#include "reflex_turing_fabric.h"

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════════════════════
// Configuration
// ═══════════════════════════════════════════════════════════════════════════

#define FABRIC_CFC_HIDDEN_DIM       64      // Neurons
#define FABRIC_CFC_INPUT_DIM        4       // Input size
#define FABRIC_CFC_CONCAT_DIM       (FABRIC_CFC_HIDDEN_DIM + FABRIC_CFC_INPUT_DIM)

#define FABRIC_CFC_QUANT_BITS       4       // 4-bit quantization
#define FABRIC_CFC_QUANT_LEVELS     16      // 2^4 = 16 levels
#define FABRIC_CFC_QUANT_MASK       0x0F    // 4-bit mask

// Mixer LUT size per neuron: 16 × 16 × 16 = 4096 bytes
#define FABRIC_CFC_MIXER_LUT_SIZE   4096

// Total mixer LUT: 4096 × 64 = 262,144 bytes = 256 KB
#define FABRIC_CFC_TOTAL_MIXER_SIZE (FABRIC_CFC_MIXER_LUT_SIZE * FABRIC_CFC_HIDDEN_DIM)

// Sparse weight representation
#define FABRIC_CFC_MAX_NONZERO      32      // Max nonzero weights per row (with 81% sparsity, expect ~12)

// Batched pulse buffer: max 32 weights × 15 pulses = 480 symbols
#define FABRIC_CFC_MAX_PULSES       512     // RMT symbols per sparse dot
#define FABRIC_CFC_PULSE_HIGH_TICKS 5       // 500ns at 10MHz
#define FABRIC_CFC_PULSE_LOW_TICKS  5       // 500ns gap

// Pulse palette: pre-computed patterns for values 0-15
#define FABRIC_CFC_PALETTE_SIZE     16      // 16 possible 4-bit values
#define FABRIC_CFC_MAX_PALETTE_LEN  16      // Max pulses per pattern (15 + end marker)

// Parallel PCNT configuration
// Note: Base fabric uses 1 RMT + 1 PCNT, so we can only add 3 more
#define FABRIC_CFC_PARALLEL_UNITS   3       // Process 3 neurons at once
#define FABRIC_CFC_PARALLEL_GPIO_0  5       // RMT1 → PCNT1
#define FABRIC_CFC_PARALLEL_GPIO_1  6       // RMT2 → PCNT2  
#define FABRIC_CFC_PARALLEL_GPIO_2  7       // RMT3 → PCNT3

// ═══════════════════════════════════════════════════════════════════════════
// Quantization Functions
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Quantize Q15 value to 4-bit (0-15)
 * Q15 range: [-32768, 32767] maps to [-1, +1)
 * Output: 0-15
 */
static inline uint8_t fabric_q15_to_q4(int16_t q15) {
    // Map [-32768, 32767] to [0, 15]
    // (q15 + 32768) >> 12 gives 0-15
    int32_t shifted = (int32_t)q15 + 32768;
    return (uint8_t)((shifted >> 12) & 0x0F);
}

/**
 * Dequantize 4-bit back to Q15
 */
static inline int16_t fabric_q4_to_q15(uint8_t q4) {
    // Map 0-15 back to Q15
    // q4 * 4369 - 32768 (4369 ≈ 65536/15)
    return (int16_t)((int32_t)q4 * 4369 - 32768);
}

/**
 * Quantize float to 4-bit
 */
static inline uint8_t fabric_float_to_q4(float f) {
    // Clamp to [-1, 1]
    if (f < -1.0f) f = -1.0f;
    if (f > 1.0f) f = 1.0f;
    // Map to 0-15
    return (uint8_t)((f + 1.0f) * 7.5f);
}

/**
 * Dequantize 4-bit to float
 */
static inline float fabric_q4_to_float(uint8_t q4) {
    return ((float)q4 / 7.5f) - 1.0f;
}

// ═══════════════════════════════════════════════════════════════════════════
// Sparse Weight Structure (for fabric)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Sparse row for fabric - stores indices and signs
 * For pulse generation, we need to know which inputs contribute
 */
typedef struct {
    uint8_t pos_indices[FABRIC_CFC_MAX_NONZERO];  // Indices with +1 weight
    uint8_t neg_indices[FABRIC_CFC_MAX_NONZERO];  // Indices with -1 weight
    uint8_t pos_count;                             // Number of +1 weights
    uint8_t neg_count;                             // Number of -1 weights
} fabric_sparse_row_t;

/**
 * Full sparse weights for CfC
 */
typedef struct {
    fabric_sparse_row_t gate[FABRIC_CFC_HIDDEN_DIM];
    fabric_sparse_row_t cand[FABRIC_CFC_HIDDEN_DIM];
} fabric_cfc_sparse_weights_t;

// ═══════════════════════════════════════════════════════════════════════════
// Pulse Palette - PRE-COMPUTED PATTERNS FOR VALUES 0-15
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Pulse Palette: Pre-computed RMT patterns for each 4-bit value
 * 
 * Instead of building pulses dynamically, we index into this palette.
 * Value N maps to pattern with N pulses.
 * 
 * Memory: 16 patterns × 16 symbols × 4 bytes = 1 KB
 */
typedef struct {
    rmt_symbol_word_t patterns[FABRIC_CFC_PALETTE_SIZE][FABRIC_CFC_MAX_PALETTE_LEN];
    uint8_t lengths[FABRIC_CFC_PALETTE_SIZE];  // Actual length of each pattern
} fabric_pulse_palette_t;

/**
 * Generate pulse palette
 */
static inline void fabric_generate_pulse_palette(fabric_pulse_palette_t* palette) {
    for (int val = 0; val < FABRIC_CFC_PALETTE_SIZE; val++) {
        // Generate 'val' pulses
        for (int p = 0; p < val; p++) {
            palette->patterns[val][p].duration0 = FABRIC_CFC_PULSE_HIGH_TICKS;
            palette->patterns[val][p].level0 = 1;
            palette->patterns[val][p].duration1 = FABRIC_CFC_PULSE_LOW_TICKS;
            palette->patterns[val][p].level1 = 0;
        }
        // End marker
        palette->patterns[val][val].duration0 = 0;
        palette->patterns[val][val].level0 = 0;
        palette->patterns[val][val].duration1 = 0;
        palette->patterns[val][val].level1 = 0;
        
        palette->lengths[val] = val + 1;  // Include end marker
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Activation LUTs (shared across all neurons)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Sigmoid LUT: maps pre-activation (Q4.11 quantized to 8-bit) to gate (4-bit)
 * Input: 0-255 (representing roughly -8 to +8)
 * Output: 0-15 (representing 0 to 1)
 */
typedef struct {
    uint8_t sigmoid[256];   // 8-bit input → 4-bit output
    uint8_t tanh_lut[256];  // 8-bit input → 4-bit output (signed, stored as 0-15)
} fabric_activation_luts_t;

/**
 * Generate activation LUTs
 */
static inline void fabric_generate_activation_luts(fabric_activation_luts_t* luts) {
    for (int i = 0; i < 256; i++) {
        // Map 0-255 to input range [-8, +8]
        float x = ((float)i / 255.0f) * 16.0f - 8.0f;
        
        // Sigmoid: 1 / (1 + exp(-x))
        float sig = 1.0f / (1.0f + expf(-x));
        luts->sigmoid[i] = (uint8_t)(sig * 15.0f);  // 0-15
        
        // Tanh: output is [-1, +1], map to [0, 15]
        float t = tanhf(x);
        luts->tanh_lut[i] = (uint8_t)((t + 1.0f) * 7.5f);  // 0-15
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Mixer LUT - THE KEY INNOVATION
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Mixer LUT for one neuron
 * 
 * CfC mixer: h_new = (1 - gate) * h_prev * decay + gate * candidate
 * 
 * With 4-bit quantization:
 *   gate:      0-15 (representing 0 to 1)
 *   h_prev:    0-15 (representing -1 to +1)
 *   candidate: 0-15 (representing -1 to +1)
 *   decay:     fixed per neuron (baked into LUT)
 *   output:    0-15 (representing -1 to +1)
 *
 * Size: 16 × 16 × 16 = 4096 bytes per neuron
 */
typedef struct {
    uint8_t lut[16][16][16];  // [gate][h_prev][candidate] → h_new
} fabric_mixer_lut_t;

/**
 * Generate mixer LUT for one neuron
 * 
 * @param lut      Output LUT
 * @param decay    Decay value for this neuron (0 to 1)
 */
static inline void fabric_generate_mixer_lut(fabric_mixer_lut_t* lut, float decay) {
    for (int g = 0; g < 16; g++) {
        float gate = (float)g / 15.0f;  // 0 to 1
        float one_minus_gate = 1.0f - gate;
        
        for (int h = 0; h < 16; h++) {
            float h_prev = ((float)h / 7.5f) - 1.0f;  // -1 to +1
            float retention = one_minus_gate * h_prev * decay;
            
            for (int c = 0; c < 16; c++) {
                float cand = ((float)c / 7.5f) - 1.0f;  // -1 to +1
                float update = gate * cand;
                float h_new = retention + update;
                
                // Clamp and quantize
                if (h_new < -1.0f) h_new = -1.0f;
                if (h_new > 1.0f) h_new = 1.0f;
                
                lut->lut[g][h][c] = (uint8_t)((h_new + 1.0f) * 7.5f);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Fabric CfC Engine
// ═══════════════════════════════════════════════════════════════════════════

typedef struct {
    // Activation LUTs (shared)
    fabric_activation_luts_t activations;
    
    // Mixer LUTs (one per neuron) - THIS IS THE BIG ONE
    fabric_mixer_lut_t* mixer_luts;  // Array of 64 LUTs = 256 KB
    
    // Sparse weights
    fabric_cfc_sparse_weights_t weights;
    
    // Decay values (for mixer LUT generation)
    float decay[FABRIC_CFC_HIDDEN_DIM];
    
    // Quantized state (4-bit packed)
    uint8_t hidden_q4[FABRIC_CFC_HIDDEN_DIM];
    
    // Full precision hidden state for comparison
    int16_t hidden_q15[FABRIC_CFC_HIDDEN_DIM];
    
    // Input buffer (quantized)
    uint8_t input_q4[FABRIC_CFC_INPUT_DIM];
    
    // Hardware handles
    fabric_engine_t* fabric;  // Base PCNT/RMT fabric
    
    // Batched pulse buffer for DMA
    rmt_symbol_word_t* pulse_buffer;  // Pre-allocated for batching
    
    // Pulse palette (pre-computed patterns for values 0-15)
    fabric_pulse_palette_t palette;
    
    // Statistics
    uint32_t inference_count;
    bool initialized;
    
} fabric_cfc_engine_t;

// ═══════════════════════════════════════════════════════════════════════════
// Initialization
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Initialize the fabric CfC engine
 * This allocates ~256 KB for mixer LUTs
 */
static inline esp_err_t fabric_cfc_init(fabric_cfc_engine_t* engine, fabric_engine_t* base_fabric) {
    memset(engine, 0, sizeof(fabric_cfc_engine_t));
    
    engine->fabric = base_fabric;
    
    // Allocate mixer LUTs (256 KB!)
    engine->mixer_luts = (fabric_mixer_lut_t*)malloc(
        sizeof(fabric_mixer_lut_t) * FABRIC_CFC_HIDDEN_DIM
    );
    if (!engine->mixer_luts) {
        return ESP_ERR_NO_MEM;
    }
    
    // Generate activation LUTs
    fabric_generate_activation_luts(&engine->activations);
    
    // Initialize decay values (default tau)
    float dt = 0.01f;  // 10ms timestep
    float tau = 0.1f;  // 100ms time constant
    float decay_val = expf(-dt / tau);
    for (int i = 0; i < FABRIC_CFC_HIDDEN_DIM; i++) {
        engine->decay[i] = decay_val;
    }
    
    // Generate mixer LUTs for each neuron
    printf("  Generating mixer LUTs for %d neurons...\n", FABRIC_CFC_HIDDEN_DIM);
    for (int i = 0; i < FABRIC_CFC_HIDDEN_DIM; i++) {
        fabric_generate_mixer_lut(&engine->mixer_luts[i], engine->decay[i]);
    }
    printf("  Mixer LUTs ready: %d KB\n", 
           (int)(sizeof(fabric_mixer_lut_t) * FABRIC_CFC_HIDDEN_DIM / 1024));
    
    // Initialize random sparse weights (~19% density)
    uint32_t seed = 42;
    #define RAND() (seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5, seed)
    
    for (int n = 0; n < FABRIC_CFC_HIDDEN_DIM; n++) {
        engine->weights.gate[n].pos_count = 0;
        engine->weights.gate[n].neg_count = 0;
        engine->weights.cand[n].pos_count = 0;
        engine->weights.cand[n].neg_count = 0;
        
        for (int c = 0; c < FABRIC_CFC_CONCAT_DIM; c++) {
            uint32_t r = RAND() % 10;
            if (r == 0 && engine->weights.gate[n].pos_count < FABRIC_CFC_MAX_NONZERO) {
                engine->weights.gate[n].pos_indices[engine->weights.gate[n].pos_count++] = c;
            } else if (r == 1 && engine->weights.gate[n].neg_count < FABRIC_CFC_MAX_NONZERO) {
                engine->weights.gate[n].neg_indices[engine->weights.gate[n].neg_count++] = c;
            }
            
            r = RAND() % 10;
            if (r == 0 && engine->weights.cand[n].pos_count < FABRIC_CFC_MAX_NONZERO) {
                engine->weights.cand[n].pos_indices[engine->weights.cand[n].pos_count++] = c;
            } else if (r == 1 && engine->weights.cand[n].neg_count < FABRIC_CFC_MAX_NONZERO) {
                engine->weights.cand[n].neg_indices[engine->weights.cand[n].neg_count++] = c;
            }
        }
    }
    #undef RAND
    
    // Initialize hidden state
    for (int i = 0; i < FABRIC_CFC_HIDDEN_DIM; i++) {
        engine->hidden_q4[i] = 8;   // 0 in [-1, +1] range
        engine->hidden_q15[i] = 0;
    }
    
    // Allocate batched pulse buffer (2 KB)
    engine->pulse_buffer = (rmt_symbol_word_t*)malloc(
        sizeof(rmt_symbol_word_t) * FABRIC_CFC_MAX_PULSES
    );
    if (!engine->pulse_buffer) {
        free(engine->mixer_luts);
        return ESP_ERR_NO_MEM;
    }
    printf("  Pulse buffer ready: %d bytes\n", 
           (int)(sizeof(rmt_symbol_word_t) * FABRIC_CFC_MAX_PULSES));
    
    // Generate pulse palette (pre-computed patterns for 0-15)
    fabric_generate_pulse_palette(&engine->palette);
    printf("  Pulse palette ready: %d bytes (16 patterns)\n",
           (int)sizeof(fabric_pulse_palette_t));
    
    engine->initialized = true;
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════════════════════
// Software Reference (for verification)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Run one CfC step in software using the fabric's quantized LUTs
 * This is the REFERENCE implementation - fabric should produce same result
 */
static inline void fabric_cfc_step_sw(
    fabric_cfc_engine_t* engine,
    const uint8_t* input_q4
) {
    // Build concat vector (input + hidden)
    uint8_t concat_q4[FABRIC_CFC_CONCAT_DIM];
    memcpy(concat_q4, input_q4, FABRIC_CFC_INPUT_DIM);
    memcpy(concat_q4 + FABRIC_CFC_INPUT_DIM, engine->hidden_q4, FABRIC_CFC_HIDDEN_DIM);
    
    uint8_t new_hidden[FABRIC_CFC_HIDDEN_DIM];
    
    for (int n = 0; n < FABRIC_CFC_HIDDEN_DIM; n++) {
        // Sparse dot product for gate
        int gate_sum = 0;
        for (int i = 0; i < engine->weights.gate[n].pos_count; i++) {
            gate_sum += concat_q4[engine->weights.gate[n].pos_indices[i]];
        }
        for (int i = 0; i < engine->weights.gate[n].neg_count; i++) {
            gate_sum -= concat_q4[engine->weights.gate[n].neg_indices[i]];
        }
        
        // Map gate_sum to LUT index (clamp to 0-255)
        int gate_idx = gate_sum + 128;
        if (gate_idx < 0) gate_idx = 0;
        if (gate_idx > 255) gate_idx = 255;
        uint8_t gate_q4 = engine->activations.sigmoid[gate_idx];
        
        // Sparse dot product for candidate
        int cand_sum = 0;
        for (int i = 0; i < engine->weights.cand[n].pos_count; i++) {
            cand_sum += concat_q4[engine->weights.cand[n].pos_indices[i]];
        }
        for (int i = 0; i < engine->weights.cand[n].neg_count; i++) {
            cand_sum -= concat_q4[engine->weights.cand[n].neg_indices[i]];
        }
        
        int cand_idx = cand_sum + 128;
        if (cand_idx < 0) cand_idx = 0;
        if (cand_idx > 255) cand_idx = 255;
        uint8_t cand_q4 = engine->activations.tanh_lut[cand_idx];
        
        // Mixer via LUT
        uint8_t h_prev_q4 = engine->hidden_q4[n];
        new_hidden[n] = engine->mixer_luts[n].lut[gate_q4][h_prev_q4][cand_q4];
    }
    
    // Update hidden state
    memcpy(engine->hidden_q4, new_hidden, FABRIC_CFC_HIDDEN_DIM);
    engine->inference_count++;
}

// ═══════════════════════════════════════════════════════════════════════════
// Hardware Fabric Implementation
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Run one CfC step using HARDWARE fabric (PCNT for addition, LUT for rest)
 * 
 * This uses PCNT pulse counting for sparse dot products,
 * demonstrating that the addition CAN be done in hardware.
 * 
 * In a full ETM implementation, the CPU wouldn't even call this -
 * ETM would chain everything automatically.
 */
static inline void fabric_cfc_step_hw(
    fabric_cfc_engine_t* engine,
    const uint8_t* input_q4
) {
    // Build concat vector
    uint8_t concat_q4[FABRIC_CFC_CONCAT_DIM];
    memcpy(concat_q4, input_q4, FABRIC_CFC_INPUT_DIM);
    memcpy(concat_q4 + FABRIC_CFC_INPUT_DIM, engine->hidden_q4, FABRIC_CFC_HIDDEN_DIM);
    
    uint8_t new_hidden[FABRIC_CFC_HIDDEN_DIM];
    
    for (int n = 0; n < FABRIC_CFC_HIDDEN_DIM; n++) {
        // === GATE SPARSE DOT via PCNT ===
        fabric_clear_accumulator(engine->fabric);
        
        // Send pulses for positive weights
        for (int i = 0; i < engine->weights.gate[n].pos_count; i++) {
            uint8_t val = concat_q4[engine->weights.gate[n].pos_indices[i]];
            if (val > 0) {
                fabric_send_pulses(engine->fabric, val);
                rmt_tx_wait_all_done(engine->fabric->rmt_chan, portMAX_DELAY);
            }
        }
        int pos_sum = fabric_read_accumulator(engine->fabric);
        
        // For negative weights, we compute separately (PCNT could use second channel)
        int neg_sum = 0;
        for (int i = 0; i < engine->weights.gate[n].neg_count; i++) {
            neg_sum += concat_q4[engine->weights.gate[n].neg_indices[i]];
        }
        
        int gate_sum = pos_sum - neg_sum;
        int gate_idx = gate_sum + 128;
        if (gate_idx < 0) gate_idx = 0;
        if (gate_idx > 255) gate_idx = 255;
        uint8_t gate_q4 = engine->activations.sigmoid[gate_idx];
        
        // === CANDIDATE SPARSE DOT via PCNT ===
        fabric_clear_accumulator(engine->fabric);
        
        for (int i = 0; i < engine->weights.cand[n].pos_count; i++) {
            uint8_t val = concat_q4[engine->weights.cand[n].pos_indices[i]];
            if (val > 0) {
                fabric_send_pulses(engine->fabric, val);
                rmt_tx_wait_all_done(engine->fabric->rmt_chan, portMAX_DELAY);
            }
        }
        pos_sum = fabric_read_accumulator(engine->fabric);
        
        neg_sum = 0;
        for (int i = 0; i < engine->weights.cand[n].neg_count; i++) {
            neg_sum += concat_q4[engine->weights.cand[n].neg_indices[i]];
        }
        
        int cand_sum = pos_sum - neg_sum;
        int cand_idx = cand_sum + 128;
        if (cand_idx < 0) cand_idx = 0;
        if (cand_idx > 255) cand_idx = 255;
        uint8_t cand_q4 = engine->activations.tanh_lut[cand_idx];
        
        // === MIXER via LUT (no computation!) ===
        uint8_t h_prev_q4 = engine->hidden_q4[n];
        new_hidden[n] = engine->mixer_luts[n].lut[gate_q4][h_prev_q4][cand_q4];
    }
    
    memcpy(engine->hidden_q4, new_hidden, FABRIC_CFC_HIDDEN_DIM);
    engine->inference_count++;
}

// ═══════════════════════════════════════════════════════════════════════════
// BATCHED Hardware Fabric Implementation
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Build batched pulse buffer for a sparse dot product
 * Returns the number of RMT symbols written
 * 
 * Instead of calling rmt_transmit() for each weight, we build ALL pulses
 * into one buffer and transmit once. PCNT counts all pulses = sum!
 */
static inline int fabric_build_batched_pulses(
    rmt_symbol_word_t* buffer,
    const uint8_t* concat_q4,
    const uint8_t* indices,
    int count
) {
    int pos = 0;
    
    for (int i = 0; i < count; i++) {
        uint8_t val = concat_q4[indices[i]];
        
        // Generate 'val' pulses for this weight
        for (int p = 0; p < val && pos < FABRIC_CFC_MAX_PULSES - 1; p++) {
            buffer[pos].duration0 = FABRIC_CFC_PULSE_HIGH_TICKS;
            buffer[pos].level0 = 1;
            buffer[pos].duration1 = FABRIC_CFC_PULSE_LOW_TICKS;
            buffer[pos].level1 = 0;
            pos++;
        }
    }
    
    // End marker
    buffer[pos].duration0 = 0;
    buffer[pos].level0 = 0;
    buffer[pos].duration1 = 0;
    buffer[pos].level1 = 0;
    pos++;
    
    return pos;
}

/**
 * Run one CfC step using BATCHED hardware fabric
 * 
 * Key difference: ONE rmt_transmit() per sparse dot instead of N.
 * This dramatically reduces RMT setup overhead.
 */
static inline void fabric_cfc_step_hw_batched(
    fabric_cfc_engine_t* engine,
    const uint8_t* input_q4
) {
    // Build concat vector
    uint8_t concat_q4[FABRIC_CFC_CONCAT_DIM];
    memcpy(concat_q4, input_q4, FABRIC_CFC_INPUT_DIM);
    memcpy(concat_q4 + FABRIC_CFC_INPUT_DIM, engine->hidden_q4, FABRIC_CFC_HIDDEN_DIM);
    
    uint8_t new_hidden[FABRIC_CFC_HIDDEN_DIM];
    
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    
    for (int n = 0; n < FABRIC_CFC_HIDDEN_DIM; n++) {
        // === GATE SPARSE DOT via BATCHED PCNT ===
        fabric_clear_accumulator(engine->fabric);
        
        // Build ALL positive pulses into one buffer
        int num_symbols = fabric_build_batched_pulses(
            engine->pulse_buffer,
            concat_q4,
            engine->weights.gate[n].pos_indices,
            engine->weights.gate[n].pos_count
        );
        
        // ONE transmission for all positive weights!
        if (num_symbols > 1) {
            rmt_transmit(engine->fabric->rmt_chan, engine->fabric->rmt_encoder,
                engine->pulse_buffer, num_symbols * sizeof(rmt_symbol_word_t),
                &tx_config);
            rmt_tx_wait_all_done(engine->fabric->rmt_chan, portMAX_DELAY);
        }
        int pos_sum = fabric_read_accumulator(engine->fabric);
        
        // Negative weights (software for now - could use second PCNT channel)
        int neg_sum = 0;
        for (int i = 0; i < engine->weights.gate[n].neg_count; i++) {
            neg_sum += concat_q4[engine->weights.gate[n].neg_indices[i]];
        }
        
        int gate_sum = pos_sum - neg_sum;
        int gate_idx = gate_sum + 128;
        if (gate_idx < 0) gate_idx = 0;
        if (gate_idx > 255) gate_idx = 255;
        uint8_t gate_q4 = engine->activations.sigmoid[gate_idx];
        
        // === CANDIDATE SPARSE DOT via BATCHED PCNT ===
        fabric_clear_accumulator(engine->fabric);
        
        num_symbols = fabric_build_batched_pulses(
            engine->pulse_buffer,
            concat_q4,
            engine->weights.cand[n].pos_indices,
            engine->weights.cand[n].pos_count
        );
        
        if (num_symbols > 1) {
            rmt_transmit(engine->fabric->rmt_chan, engine->fabric->rmt_encoder,
                engine->pulse_buffer, num_symbols * sizeof(rmt_symbol_word_t),
                &tx_config);
            rmt_tx_wait_all_done(engine->fabric->rmt_chan, portMAX_DELAY);
        }
        pos_sum = fabric_read_accumulator(engine->fabric);
        
        neg_sum = 0;
        for (int i = 0; i < engine->weights.cand[n].neg_count; i++) {
            neg_sum += concat_q4[engine->weights.cand[n].neg_indices[i]];
        }
        
        int cand_sum = pos_sum - neg_sum;
        int cand_idx = cand_sum + 128;
        if (cand_idx < 0) cand_idx = 0;
        if (cand_idx > 255) cand_idx = 255;
        uint8_t cand_q4 = engine->activations.tanh_lut[cand_idx];
        
        // === MIXER via LUT (no computation!) ===
        uint8_t h_prev_q4 = engine->hidden_q4[n];
        new_hidden[n] = engine->mixer_luts[n].lut[gate_q4][h_prev_q4][cand_q4];
    }
    
    memcpy(engine->hidden_q4, new_hidden, FABRIC_CFC_HIDDEN_DIM);
    engine->inference_count++;
}

// ═══════════════════════════════════════════════════════════════════════════
// PALLETIZED Hardware Fabric Implementation
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Build pulse buffer using PALETTE (memcpy instead of building)
 * 
 * Instead of generating pulses one by one, we memcpy pre-computed patterns.
 * This eliminates the per-pulse loop overhead.
 */
static inline int fabric_build_palletized_pulses(
    rmt_symbol_word_t* buffer,
    const fabric_pulse_palette_t* palette,
    const uint8_t* concat_q4,
    const uint8_t* indices,
    int count
) {
    int pos = 0;
    
    for (int i = 0; i < count && pos < FABRIC_CFC_MAX_PULSES - 16; i++) {
        uint8_t val = concat_q4[indices[i]];
        if (val > 0) {
            // Memcpy the pre-computed pattern (excluding end marker)
            memcpy(&buffer[pos], palette->patterns[val], 
                   val * sizeof(rmt_symbol_word_t));
            pos += val;
        }
    }
    
    // End marker
    buffer[pos].duration0 = 0;
    buffer[pos].level0 = 0;
    buffer[pos].duration1 = 0;
    buffer[pos].level1 = 0;
    pos++;
    
    return pos;
}

/**
 * Run one CfC step using PALLETIZED hardware fabric
 * 
 * Key difference: memcpy from pre-computed palette instead of building pulses.
 * Eliminates the per-pulse generation loop.
 */
static inline void fabric_cfc_step_hw_palletized(
    fabric_cfc_engine_t* engine,
    const uint8_t* input_q4
) {
    // Build concat vector
    uint8_t concat_q4[FABRIC_CFC_CONCAT_DIM];
    memcpy(concat_q4, input_q4, FABRIC_CFC_INPUT_DIM);
    memcpy(concat_q4 + FABRIC_CFC_INPUT_DIM, engine->hidden_q4, FABRIC_CFC_HIDDEN_DIM);
    
    uint8_t new_hidden[FABRIC_CFC_HIDDEN_DIM];
    
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    
    for (int n = 0; n < FABRIC_CFC_HIDDEN_DIM; n++) {
        // === GATE SPARSE DOT via PALLETIZED PCNT ===
        fabric_clear_accumulator(engine->fabric);
        
        int num_symbols = fabric_build_palletized_pulses(
            engine->pulse_buffer,
            &engine->palette,
            concat_q4,
            engine->weights.gate[n].pos_indices,
            engine->weights.gate[n].pos_count
        );
        
        if (num_symbols > 1) {
            rmt_transmit(engine->fabric->rmt_chan, engine->fabric->rmt_encoder,
                engine->pulse_buffer, num_symbols * sizeof(rmt_symbol_word_t),
                &tx_config);
            rmt_tx_wait_all_done(engine->fabric->rmt_chan, portMAX_DELAY);
        }
        int pos_sum = fabric_read_accumulator(engine->fabric);
        
        int neg_sum = 0;
        for (int i = 0; i < engine->weights.gate[n].neg_count; i++) {
            neg_sum += concat_q4[engine->weights.gate[n].neg_indices[i]];
        }
        
        int gate_sum = pos_sum - neg_sum;
        int gate_idx = gate_sum + 128;
        if (gate_idx < 0) gate_idx = 0;
        if (gate_idx > 255) gate_idx = 255;
        uint8_t gate_q4 = engine->activations.sigmoid[gate_idx];
        
        // === CANDIDATE SPARSE DOT via PALLETIZED PCNT ===
        fabric_clear_accumulator(engine->fabric);
        
        num_symbols = fabric_build_palletized_pulses(
            engine->pulse_buffer,
            &engine->palette,
            concat_q4,
            engine->weights.cand[n].pos_indices,
            engine->weights.cand[n].pos_count
        );
        
        if (num_symbols > 1) {
            rmt_transmit(engine->fabric->rmt_chan, engine->fabric->rmt_encoder,
                engine->pulse_buffer, num_symbols * sizeof(rmt_symbol_word_t),
                &tx_config);
            rmt_tx_wait_all_done(engine->fabric->rmt_chan, portMAX_DELAY);
        }
        pos_sum = fabric_read_accumulator(engine->fabric);
        
        neg_sum = 0;
        for (int i = 0; i < engine->weights.cand[n].neg_count; i++) {
            neg_sum += concat_q4[engine->weights.cand[n].neg_indices[i]];
        }
        
        int cand_sum = pos_sum - neg_sum;
        int cand_idx = cand_sum + 128;
        if (cand_idx < 0) cand_idx = 0;
        if (cand_idx > 255) cand_idx = 255;
        uint8_t cand_q4 = engine->activations.tanh_lut[cand_idx];
        
        // === MIXER via LUT ===
        uint8_t h_prev_q4 = engine->hidden_q4[n];
        new_hidden[n] = engine->mixer_luts[n].lut[gate_q4][h_prev_q4][cand_q4];
    }
    
    memcpy(engine->hidden_q4, new_hidden, FABRIC_CFC_HIDDEN_DIM);
    engine->inference_count++;
}

// ═══════════════════════════════════════════════════════════════════════════
// PARALLEL Hardware Fabric (4 PCNT units, 4 RMT channels)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Parallel fabric engine - uses all 4 PCNT units and 4 RMT channels
 * 
 * This processes 4 neurons simultaneously:
 *   RMT0 → GPIO4 → PCNT0 (neuron 0, 4, 8, ...)
 *   RMT1 → GPIO5 → PCNT1 (neuron 1, 5, 9, ...)
 *   RMT2 → GPIO6 → PCNT2 (neuron 2, 6, 10, ...)
 *   RMT3 → GPIO7 → PCNT3 (neuron 3, 7, 11, ...)
 */
typedef struct {
    pcnt_unit_handle_t pcnt_units[FABRIC_CFC_PARALLEL_UNITS];
    pcnt_channel_handle_t pcnt_chans[FABRIC_CFC_PARALLEL_UNITS];
    rmt_channel_handle_t rmt_chans[FABRIC_CFC_PARALLEL_UNITS];
    rmt_encoder_handle_t rmt_encoders[FABRIC_CFC_PARALLEL_UNITS];
    rmt_symbol_word_t* pulse_buffers[FABRIC_CFC_PARALLEL_UNITS];
    bool initialized;
} fabric_parallel_t;

static const int fabric_parallel_gpios[FABRIC_CFC_PARALLEL_UNITS] = {
    FABRIC_CFC_PARALLEL_GPIO_0,
    FABRIC_CFC_PARALLEL_GPIO_1,
    FABRIC_CFC_PARALLEL_GPIO_2
};

/**
 * Initialize parallel fabric (4 RMT + 4 PCNT)
 */
static inline esp_err_t fabric_parallel_init(fabric_parallel_t* parallel) {
    memset(parallel, 0, sizeof(fabric_parallel_t));
    esp_err_t ret;
    
    for (int i = 0; i < FABRIC_CFC_PARALLEL_UNITS; i++) {
        // PCNT unit
        pcnt_unit_config_t pcnt_config = {
            .low_limit = -32768,
            .high_limit = 32767,
            .flags.accum_count = true,
        };
        ret = pcnt_new_unit(&pcnt_config, &parallel->pcnt_units[i]);
        if (ret != ESP_OK) return ret;
        
        // PCNT channel
        pcnt_chan_config_t chan_config = {
            .edge_gpio_num = fabric_parallel_gpios[i],
            .level_gpio_num = -1,
            .flags.io_loop_back = true,
        };
        ret = pcnt_new_channel(parallel->pcnt_units[i], &chan_config, &parallel->pcnt_chans[i]);
        if (ret != ESP_OK) return ret;
        
        ret = pcnt_channel_set_edge_action(parallel->pcnt_chans[i],
            PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
        if (ret != ESP_OK) return ret;
        
        pcnt_glitch_filter_config_t filter = {.max_glitch_ns = 100};
        ret = pcnt_unit_set_glitch_filter(parallel->pcnt_units[i], &filter);
        if (ret != ESP_OK) return ret;
        
        ret = pcnt_unit_enable(parallel->pcnt_units[i]);
        if (ret != ESP_OK) return ret;
        
        ret = pcnt_unit_start(parallel->pcnt_units[i]);
        if (ret != ESP_OK) return ret;
        
        // RMT channel
        rmt_tx_channel_config_t rmt_config = {
            .gpio_num = fabric_parallel_gpios[i],
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000,
            .mem_block_symbols = 48,
            .trans_queue_depth = 4,
        };
        ret = rmt_new_tx_channel(&rmt_config, &parallel->rmt_chans[i]);
        if (ret != ESP_OK) return ret;
        
        ret = rmt_enable(parallel->rmt_chans[i]);
        if (ret != ESP_OK) return ret;
        
        // RMT encoder (copy encoder)
        rmt_copy_encoder_config_t enc_config = {};
        ret = rmt_new_copy_encoder(&enc_config, &parallel->rmt_encoders[i]);
        if (ret != ESP_OK) return ret;
        
        // Pulse buffer
        parallel->pulse_buffers[i] = (rmt_symbol_word_t*)malloc(
            sizeof(rmt_symbol_word_t) * FABRIC_CFC_MAX_PULSES);
        if (!parallel->pulse_buffers[i]) return ESP_ERR_NO_MEM;
    }
    
    parallel->initialized = true;
    return ESP_OK;
}

/**
 * Run one CfC step using PARALLEL hardware (4 neurons at once!)
 */
static inline void fabric_cfc_step_hw_parallel(
    fabric_cfc_engine_t* engine,
    fabric_parallel_t* parallel,
    const uint8_t* input_q4
) {
    uint8_t concat_q4[FABRIC_CFC_CONCAT_DIM];
    memcpy(concat_q4, input_q4, FABRIC_CFC_INPUT_DIM);
    memcpy(concat_q4 + FABRIC_CFC_INPUT_DIM, engine->hidden_q4, FABRIC_CFC_HIDDEN_DIM);
    
    uint8_t new_hidden[FABRIC_CFC_HIDDEN_DIM];
    rmt_transmit_config_t tx_config = {.loop_count = 0};
    
    // Process 4 neurons at a time
    for (int batch = 0; batch < FABRIC_CFC_HIDDEN_DIM; batch += FABRIC_CFC_PARALLEL_UNITS) {
        
        // === GATE: Build and transmit 4 neurons in parallel ===
        for (int i = 0; i < FABRIC_CFC_PARALLEL_UNITS && (batch + i) < FABRIC_CFC_HIDDEN_DIM; i++) {
            int n = batch + i;
            pcnt_unit_clear_count(parallel->pcnt_units[i]);
            
            int num_symbols = fabric_build_batched_pulses(
                parallel->pulse_buffers[i], concat_q4,
                engine->weights.gate[n].pos_indices,
                engine->weights.gate[n].pos_count);
            
            if (num_symbols > 1) {
                rmt_transmit(parallel->rmt_chans[i], parallel->rmt_encoders[i],
                    parallel->pulse_buffers[i], num_symbols * sizeof(rmt_symbol_word_t),
                    &tx_config);
            }
        }
        
        // Wait for all 4 RMT channels (they transmit in parallel!)
        for (int i = 0; i < FABRIC_CFC_PARALLEL_UNITS && (batch + i) < FABRIC_CFC_HIDDEN_DIM; i++) {
            rmt_tx_wait_all_done(parallel->rmt_chans[i], portMAX_DELAY);
        }
        
        // Read 4 PCNT values and compute gates
        int gate_q4[FABRIC_CFC_PARALLEL_UNITS];
        for (int i = 0; i < FABRIC_CFC_PARALLEL_UNITS && (batch + i) < FABRIC_CFC_HIDDEN_DIM; i++) {
            int n = batch + i;
            int pos_sum;
            pcnt_unit_get_count(parallel->pcnt_units[i], &pos_sum);
            
            int neg_sum = 0;
            for (int j = 0; j < engine->weights.gate[n].neg_count; j++) {
                neg_sum += concat_q4[engine->weights.gate[n].neg_indices[j]];
            }
            
            int gate_sum = pos_sum - neg_sum;
            int gate_idx = gate_sum + 128;
            if (gate_idx < 0) gate_idx = 0;
            if (gate_idx > 255) gate_idx = 255;
            gate_q4[i] = engine->activations.sigmoid[gate_idx];
        }
        
        // === CANDIDATE: Build and transmit 4 neurons in parallel ===
        for (int i = 0; i < FABRIC_CFC_PARALLEL_UNITS && (batch + i) < FABRIC_CFC_HIDDEN_DIM; i++) {
            int n = batch + i;
            pcnt_unit_clear_count(parallel->pcnt_units[i]);
            
            int num_symbols = fabric_build_batched_pulses(
                parallel->pulse_buffers[i], concat_q4,
                engine->weights.cand[n].pos_indices,
                engine->weights.cand[n].pos_count);
            
            if (num_symbols > 1) {
                rmt_transmit(parallel->rmt_chans[i], parallel->rmt_encoders[i],
                    parallel->pulse_buffers[i], num_symbols * sizeof(rmt_symbol_word_t),
                    &tx_config);
            }
        }
        
        for (int i = 0; i < FABRIC_CFC_PARALLEL_UNITS && (batch + i) < FABRIC_CFC_HIDDEN_DIM; i++) {
            rmt_tx_wait_all_done(parallel->rmt_chans[i], portMAX_DELAY);
        }
        
        // Read 4 PCNT values and compute outputs
        for (int i = 0; i < FABRIC_CFC_PARALLEL_UNITS && (batch + i) < FABRIC_CFC_HIDDEN_DIM; i++) {
            int n = batch + i;
            int pos_sum;
            pcnt_unit_get_count(parallel->pcnt_units[i], &pos_sum);
            
            int neg_sum = 0;
            for (int j = 0; j < engine->weights.cand[n].neg_count; j++) {
                neg_sum += concat_q4[engine->weights.cand[n].neg_indices[j]];
            }
            
            int cand_sum = pos_sum - neg_sum;
            int cand_idx = cand_sum + 128;
            if (cand_idx < 0) cand_idx = 0;
            if (cand_idx > 255) cand_idx = 255;
            uint8_t cand_q4 = engine->activations.tanh_lut[cand_idx];
            
            // Mixer via LUT
            uint8_t h_prev_q4 = engine->hidden_q4[n];
            new_hidden[n] = engine->mixer_luts[n].lut[gate_q4[i]][h_prev_q4][cand_q4];
        }
    }
    
    memcpy(engine->hidden_q4, new_hidden, FABRIC_CFC_HIDDEN_DIM);
    engine->inference_count++;
}

/**
 * Cleanup parallel fabric
 */
static inline void fabric_parallel_deinit(fabric_parallel_t* parallel) {
    for (int i = 0; i < FABRIC_CFC_PARALLEL_UNITS; i++) {
        if (parallel->pulse_buffers[i]) {
            free(parallel->pulse_buffers[i]);
        }
    }
    parallel->initialized = false;
}

// ═══════════════════════════════════════════════════════════════════════════
// Cleanup
// ═══════════════════════════════════════════════════════════════════════════

static inline void fabric_cfc_deinit(fabric_cfc_engine_t* engine) {
    if (engine->mixer_luts) {
        free(engine->mixer_luts);
        engine->mixer_luts = NULL;
    }
    if (engine->pulse_buffer) {
        free(engine->pulse_buffer);
        engine->pulse_buffer = NULL;
    }
    engine->initialized = false;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_FABRIC_CFC_H
