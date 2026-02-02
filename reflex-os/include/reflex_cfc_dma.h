/**
 * reflex_cfc_dma.h - CfC as Hardware Process via DMA Prefetch
 *
 * The neural network becomes part of the memory hierarchy.
 * DMA continuously prefetches weights. CPU just computes.
 * No syscall. No stalls. Just data flowing through silicon.
 *
 * Architecture:
 *   - Double-buffered weight prefetch
 *   - DMA ring cycles through neurons autonomously
 *   - ETM triggers synchronize DMA and compute
 *   - Memory latency hidden behind compute
 *
 * Expected: 50-100 kHz (vs 7 kHz without prefetch)
 */

#ifndef REFLEX_CFC_DMA_H
#define REFLEX_CFC_DMA_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "reflex_cfc.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// DMA-Optimized Memory Layout
// ============================================================

// Weight buffer size per neuron: pos_mask(8) + neg_mask(8) = 16 bytes
#define CFC_DMA_WEIGHT_SIZE     16

// Double buffer: A and B
#define CFC_DMA_BUFFER_A        0
#define CFC_DMA_BUFFER_B        1

// Prefetch depth: how many neurons ahead to prefetch
#define CFC_DMA_PREFETCH_DEPTH  4

/**
 * Packed weights for DMA-friendly access
 * Contiguous memory = single DMA transfer per neuron
 */
typedef struct __attribute__((packed, aligned(4))) {
    uint64_t pos_mask;
    uint64_t neg_mask;
} cfc_packed_weights_t;

/**
 * DMA-optimized CfC layer
 * Weights reorganized for sequential DMA access
 */
typedef struct {
    // F pathway weights (contiguous for DMA)
    cfc_packed_weights_t f_weights[CFC_NUM_NEURONS];
    int8_t f_bias[CFC_NUM_NEURONS];
    
    // G pathway weights (contiguous for DMA)
    cfc_packed_weights_t g_weights[CFC_NUM_NEURONS];
    int8_t g_bias[CFC_NUM_NEURONS];
    
    // Output weights (contiguous for DMA)
    cfc_packed_weights_t out_weights[CFC_OUTPUT_BITS];
    int8_t out_bias[CFC_OUTPUT_BITS];
    
    // Hidden state
    uint64_t hidden;
    
    // Prefetch buffers (double-buffered)
    cfc_packed_weights_t prefetch_f[2][CFC_DMA_PREFETCH_DEPTH];
    cfc_packed_weights_t prefetch_g[2][CFC_DMA_PREFETCH_DEPTH];
    int8_t prefetch_f_bias[2][CFC_DMA_PREFETCH_DEPTH];
    int8_t prefetch_g_bias[2][CFC_DMA_PREFETCH_DEPTH];
    
    // Current buffer index
    uint8_t current_buffer;
    
    // Prefetch state
    uint8_t prefetch_neuron;  // Next neuron to prefetch
    
} cfc_dma_layer_t;

// ============================================================
// Fast Compute (assumes weights already in prefetch buffer)
// ============================================================

/**
 * Compute single neuron from prefetched weights
 * Weights already in cache/buffer - no memory stall
 */
static inline uint8_t cfc_compute_neuron_prefetched(
    uint64_t input,
    const cfc_packed_weights_t* weights,
    int8_t bias
) {
    int16_t pre_act = popcount64_fast(input & weights->pos_mask)
                    - popcount64_fast(input & weights->neg_mask)
                    + bias + CFC_PREACT_OFFSET;
    
    if (pre_act < 0) pre_act = 0;
    if (pre_act > CFC_PREACT_MAX) pre_act = CFC_PREACT_MAX;
    return (uint8_t)pre_act;
}

// ============================================================
// Simulated DMA Prefetch (software simulation)
// ============================================================

/**
 * Prefetch next batch of weights into buffer
 * In real implementation: DMA descriptor ring does this autonomously
 */
static inline void cfc_dma_prefetch(
    cfc_dma_layer_t* layer,
    uint8_t target_buffer,
    uint8_t start_neuron
) {
    // Prefetch F weights
    for (int i = 0; i < CFC_DMA_PREFETCH_DEPTH && (start_neuron + i) < CFC_NUM_NEURONS; i++) {
        uint8_t n = start_neuron + i;
        layer->prefetch_f[target_buffer][i] = layer->f_weights[n];
        layer->prefetch_f_bias[target_buffer][i] = layer->f_bias[n];
    }
    
    // Prefetch G weights
    for (int i = 0; i < CFC_DMA_PREFETCH_DEPTH && (start_neuron + i) < CFC_NUM_NEURONS; i++) {
        uint8_t n = start_neuron + i;
        layer->prefetch_g[target_buffer][i] = layer->g_weights[n];
        layer->prefetch_g_bias[target_buffer][i] = layer->g_bias[n];
    }
}

/**
 * Forward pass with simulated DMA prefetch
 * Demonstrates the pipelining concept
 */
static inline void cfc_dma_forward(
    cfc_dma_layer_t* layer,
    uint64_t input,
    uint64_t* output
) {
    uint64_t combined = input | layer->hidden;
    uint64_t f_gate = 0;
    uint64_t g_bits = 0;
    
    // Initial prefetch into buffer A
    layer->current_buffer = CFC_DMA_BUFFER_A;
    cfc_dma_prefetch(layer, CFC_DMA_BUFFER_A, 0);
    
    // Process neurons in batches with double-buffered prefetch
    for (uint8_t batch = 0; batch < CFC_NUM_NEURONS; batch += CFC_DMA_PREFETCH_DEPTH) {
        uint8_t compute_buffer = layer->current_buffer;
        uint8_t prefetch_buffer = 1 - compute_buffer;
        
        // Start prefetch for NEXT batch (overlapped with compute)
        uint8_t next_batch = batch + CFC_DMA_PREFETCH_DEPTH;
        if (next_batch < CFC_NUM_NEURONS) {
            cfc_dma_prefetch(layer, prefetch_buffer, next_batch);
        }
        
        // Compute current batch from prefetched weights
        for (int i = 0; i < CFC_DMA_PREFETCH_DEPTH && (batch + i) < CFC_NUM_NEURONS; i++) {
            uint8_t n = batch + i;
            
            // F pathway (from prefetch buffer)
            uint8_t f_pre = cfc_compute_neuron_prefetched(
                combined,
                &layer->prefetch_f[compute_buffer][i],
                layer->prefetch_f_bias[compute_buffer][i]
            );
            
            // G pathway (from prefetch buffer)
            uint8_t g_pre = cfc_compute_neuron_prefetched(
                combined,
                &layer->prefetch_g[compute_buffer][i],
                layer->prefetch_g_bias[compute_buffer][i]
            );
            
            // Accumulate results
            if (sigmoid_lut[f_pre] > 128) f_gate |= (1ULL << n);
            if (g_pre > (CFC_PREACT_OFFSET + 8)) g_bits |= (1ULL << n);
        }
        
        // Swap buffers
        layer->current_buffer = prefetch_buffer;
    }
    
    // Apply CfC dynamics
    layer->hidden = (f_gate & g_bits) | ((~f_gate) & layer->hidden);
    
    // Output layer (direct access, could also be prefetched)
    *output = 0;
    for (int o = 0; o < CFC_OUTPUT_BITS; o++) {
        uint8_t out_pre = cfc_compute_neuron_prefetched(
            layer->hidden,
            &layer->out_weights[o],
            layer->out_bias[o]
        );
        if (sigmoid_lut[out_pre] > 128) {
            *output |= (1ULL << o);
        }
    }
}

// ============================================================
// Streaming Forward Pass (process as weights arrive)
// ============================================================

/**
 * Streaming state machine
 * Processes one neuron at a time as weights become available
 */
typedef struct {
    cfc_dma_layer_t* layer;
    uint64_t input;
    uint64_t combined;
    uint64_t f_gate;
    uint64_t g_bits;
    uint8_t current_neuron;
    uint8_t phase;  // 0=hidden, 1=output
} cfc_stream_state_t;

/**
 * Initialize streaming forward pass
 */
static inline void cfc_stream_begin(
    cfc_stream_state_t* state,
    cfc_dma_layer_t* layer,
    uint64_t input
) {
    state->layer = layer;
    state->input = input;
    state->combined = input | layer->hidden;
    state->f_gate = 0;
    state->g_bits = 0;
    state->current_neuron = 0;
    state->phase = 0;
}

/**
 * Process one neuron (called when DMA signals weights ready)
 * Returns: true if more neurons to process, false if done
 */
static inline bool cfc_stream_step(
    cfc_stream_state_t* state,
    const cfc_packed_weights_t* f_weights,
    int8_t f_bias,
    const cfc_packed_weights_t* g_weights,
    int8_t g_bias
) {
    if (state->phase == 0) {
        // Hidden layer
        uint8_t n = state->current_neuron;
        
        uint8_t f_pre = cfc_compute_neuron_prefetched(state->combined, f_weights, f_bias);
        uint8_t g_pre = cfc_compute_neuron_prefetched(state->combined, g_weights, g_bias);
        
        if (sigmoid_lut[f_pre] > 128) state->f_gate |= (1ULL << n);
        if (g_pre > (CFC_PREACT_OFFSET + 8)) state->g_bits |= (1ULL << n);
        
        state->current_neuron++;
        
        if (state->current_neuron >= CFC_NUM_NEURONS) {
            // Apply CfC dynamics, switch to output phase
            state->layer->hidden = (state->f_gate & state->g_bits) | 
                                   ((~state->f_gate) & state->layer->hidden);
            state->phase = 1;
            state->current_neuron = 0;
        }
        return true;
    }
    
    // Output phase handled separately
    return false;
}

/**
 * Finalize and get output
 */
static inline uint64_t cfc_stream_finish(cfc_stream_state_t* state) {
    uint64_t output = 0;
    for (int o = 0; o < CFC_OUTPUT_BITS; o++) {
        uint8_t out_pre = cfc_compute_neuron_prefetched(
            state->layer->hidden,
            &state->layer->out_weights[o],
            state->layer->out_bias[o]
        );
        if (sigmoid_lut[out_pre] > 128) {
            output |= (1ULL << o);
        }
    }
    return output;
}

// ============================================================
// Fully Pipelined Forward (maximum throughput)
// ============================================================

/**
 * Process neurons in tight loop with manual prefetch hints
 * Compiler/CPU prefetch + cache-friendly access pattern
 */
static inline void cfc_dma_forward_pipelined(
    cfc_dma_layer_t* layer,
    uint64_t input,
    uint64_t* output
) {
    uint64_t combined = input | layer->hidden;
    uint64_t f_gate = 0;
    uint64_t g_bits = 0;
    
    // Process all neurons with explicit prefetch
    const cfc_packed_weights_t* f_ptr = layer->f_weights;
    const cfc_packed_weights_t* g_ptr = layer->g_weights;
    const int8_t* fb_ptr = layer->f_bias;
    const int8_t* gb_ptr = layer->g_bias;
    
    // Prefetch first weights
    __builtin_prefetch(f_ptr, 0, 3);
    __builtin_prefetch(g_ptr, 0, 3);
    
    for (uint8_t n = 0; n < CFC_NUM_NEURONS; n++) {
        // Prefetch next neuron's weights while processing current
        if (n + 1 < CFC_NUM_NEURONS) {
            __builtin_prefetch(f_ptr + 1, 0, 3);
            __builtin_prefetch(g_ptr + 1, 0, 3);
        }
        
        // Compute current neuron
        uint8_t f_pre = cfc_compute_neuron_prefetched(combined, f_ptr, *fb_ptr);
        uint8_t g_pre = cfc_compute_neuron_prefetched(combined, g_ptr, *gb_ptr);
        
        if (sigmoid_lut[f_pre] > 128) f_gate |= (1ULL << n);
        if (g_pre > (CFC_PREACT_OFFSET + 8)) g_bits |= (1ULL << n);
        
        f_ptr++;
        g_ptr++;
        fb_ptr++;
        gb_ptr++;
    }
    
    // Apply CfC dynamics
    layer->hidden = (f_gate & g_bits) | ((~f_gate) & layer->hidden);
    
    // Output layer
    *output = 0;
    const cfc_packed_weights_t* o_ptr = layer->out_weights;
    const int8_t* ob_ptr = layer->out_bias;
    
    __builtin_prefetch(o_ptr, 0, 3);
    
    for (int o = 0; o < CFC_OUTPUT_BITS; o++) {
        if (o + 1 < CFC_OUTPUT_BITS) {
            __builtin_prefetch(o_ptr + 1, 0, 3);
        }
        
        uint8_t out_pre = cfc_compute_neuron_prefetched(layer->hidden, o_ptr, *ob_ptr);
        if (sigmoid_lut[out_pre] > 128) {
            *output |= (1ULL << o);
        }
        
        o_ptr++;
        ob_ptr++;
    }
}

// ============================================================
// Initialization
// ============================================================

/**
 * Initialize DMA-optimized layer from standard layer
 */
static inline void cfc_dma_init_from_layer(
    cfc_dma_layer_t* dma_layer,
    const cfc_layer_t* layer
) {
    // Copy and repack F weights
    for (int n = 0; n < CFC_NUM_NEURONS; n++) {
        memcpy(&dma_layer->f_weights[n].pos_mask, layer->f_weights[n].pos_mask, 8);
        memcpy(&dma_layer->f_weights[n].neg_mask, layer->f_weights[n].neg_mask, 8);
        dma_layer->f_bias[n] = layer->f_bias[n];
    }
    
    // Copy and repack G weights
    for (int n = 0; n < CFC_NUM_NEURONS; n++) {
        memcpy(&dma_layer->g_weights[n].pos_mask, layer->g_weights[n].pos_mask, 8);
        memcpy(&dma_layer->g_weights[n].neg_mask, layer->g_weights[n].neg_mask, 8);
        dma_layer->g_bias[n] = layer->g_bias[n];
    }
    
    // Copy and repack output weights
    for (int o = 0; o < CFC_OUTPUT_BITS; o++) {
        memcpy(&dma_layer->out_weights[o].pos_mask, layer->out_weights[o].pos_mask, 8);
        memcpy(&dma_layer->out_weights[o].neg_mask, layer->out_weights[o].neg_mask, 8);
        dma_layer->out_bias[o] = layer->out_bias[o];
    }
    
    // Copy hidden state
    memcpy(&dma_layer->hidden, layer->hidden, 8);
    
    // Initialize prefetch state
    dma_layer->current_buffer = 0;
    dma_layer->prefetch_neuron = 0;
    
    // Clear prefetch buffers
    memset(dma_layer->prefetch_f, 0, sizeof(dma_layer->prefetch_f));
    memset(dma_layer->prefetch_g, 0, sizeof(dma_layer->prefetch_g));
}

/**
 * Initialize with random weights
 */
static inline void cfc_dma_init_random(cfc_dma_layer_t* layer, uint32_t seed) {
    uint32_t state = seed;
    #define RAND() (state ^= state << 13, state ^= state >> 17, state ^= state << 5, state)
    
    // Initialize F weights (sparse ternary)
    for (int n = 0; n < CFC_NUM_NEURONS; n++) {
        layer->f_weights[n].pos_mask = 0;
        layer->f_weights[n].neg_mask = 0;
        
        for (int i = 0; i < 64; i++) {
            uint32_t r = RAND() % 100;
            if (r < 5) {
                layer->f_weights[n].pos_mask |= (1ULL << i);
            } else if (r < 10) {
                layer->f_weights[n].neg_mask |= (1ULL << i);
            }
        }
        layer->f_bias[n] = (int8_t)((RAND() % 17) - 8);
    }
    
    // Initialize G weights
    for (int n = 0; n < CFC_NUM_NEURONS; n++) {
        layer->g_weights[n].pos_mask = 0;
        layer->g_weights[n].neg_mask = 0;
        
        for (int i = 0; i < 64; i++) {
            uint32_t r = RAND() % 100;
            if (r < 5) {
                layer->g_weights[n].pos_mask |= (1ULL << i);
            } else if (r < 10) {
                layer->g_weights[n].neg_mask |= (1ULL << i);
            }
        }
        layer->g_bias[n] = (int8_t)((RAND() % 17) - 8);
    }
    
    // Initialize output weights
    for (int o = 0; o < CFC_OUTPUT_BITS; o++) {
        layer->out_weights[o].pos_mask = 0;
        layer->out_weights[o].neg_mask = 0;
        
        for (int i = 0; i < 64; i++) {
            uint32_t r = RAND() % 100;
            if (r < 10) {
                layer->out_weights[o].pos_mask |= (1ULL << i);
            } else if (r < 20) {
                layer->out_weights[o].neg_mask |= (1ULL << i);
            }
        }
        layer->out_bias[o] = (int8_t)((RAND() % 17) - 8);
    }
    
    layer->hidden = 0;
    layer->current_buffer = 0;
    layer->prefetch_neuron = 0;
    
    #undef RAND
}

/**
 * Get memory statistics
 */
static inline uint32_t cfc_dma_memory_size(void) {
    return sizeof(cfc_dma_layer_t);
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_CFC_DMA_H
