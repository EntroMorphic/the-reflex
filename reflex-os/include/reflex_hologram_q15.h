/**
 * reflex_hologram_q15.h - Holographic Intelligence with Yinsen Q15 CfC
 *
 * Each node is not a PART of the brain. Each node IS the brain.
 * The intelligence exists in the INTERFERENCE PATTERN.
 *
 * This version uses the Yinsen Q15 fixed-point CfC:
 *   - Zero floating-point in hot path
 *   - Q15 format: 1.0 = 32767
 *   - Sparse ternary weights: {-1, 0, +1}
 *   - LUT-based sigmoid/tanh
 *
 * Core Principles:
 *   1. Every node contains the whole (at its resolution)
 *   2. Interference IS intelligence (agreement = confidence)
 *   3. Entropy drives learning (crystallization from agreement)
 */

#ifndef REFLEX_HOLOGRAM_Q15_H
#define REFLEX_HOLOGRAM_Q15_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "activation_q15.h"
#include "cfc_cell_q15.h"

#ifdef __cplusplus
extern "C" {
#endif

// ════════════════════════════════════════════════════════════════════════════
// Configuration
// ════════════════════════════════════════════════════════════════════════════

#define HOLO_Q15_INPUT_DIM          4       // Input dimension
#define HOLO_Q15_HIDDEN_DIM         64      // Hidden dimension - 64x64 sparse matvec!
#define HOLO_Q15_CONCAT_DIM         (HOLO_Q15_INPUT_DIM + HOLO_Q15_HIDDEN_DIM)
#define HOLO_Q15_MAX_CONCAT         128     // For sparse weight arrays

#define HOLO_Q15_MAX_NEIGHBORS      8       // Max nodes in mesh
#define HOLO_Q15_NEIGHBOR_TIMEOUT   1000    // Ticks before neighbor expires

// Entropy thresholds
#define HOLO_Q15_ENTROPY_CRYSTALLIZE  10    // Below = stable
#define HOLO_Q15_ENTROPY_POTENTIAL    200   // Above = ready to change
#define HOLO_Q15_ENTROPY_INITIAL      128   // Starting entropy

// Confidence thresholds (Q15: 32767 = 1.0)
#define HOLO_Q15_CONF_HIGH          22937   // ~0.7
#define HOLO_Q15_CONF_LOW           9830    // ~0.3

// ════════════════════════════════════════════════════════════════════════════
// Sparse Weight Structure (from cfc_cell_chip.h)
// ════════════════════════════════════════════════════════════════════════════

// CfcSparseRow and CfcSparseWeights are defined in cfc_cell_chip.h
// We use them directly for the ternary sparse weights

// ════════════════════════════════════════════════════════════════════════════
// Q15 CfC State
// ════════════════════════════════════════════════════════════════════════════

typedef struct {
    // Hidden state in Q15 [HOLO_Q15_HIDDEN_DIM]
    int16_t hidden[HOLO_Q15_HIDDEN_DIM];
    
    // Sparse weights (built at init, read-only after)
    CfcSparseWeights sparse;
    
    // Biases in Q4.11 [HOLO_Q15_HIDDEN_DIM]
    int16_t b_gate[HOLO_Q15_HIDDEN_DIM];
    int16_t b_cand[HOLO_Q15_HIDDEN_DIM];
    
    // Precomputed decay in Q15 [HOLO_Q15_HIDDEN_DIM]
    int16_t decay[HOLO_Q15_HIDDEN_DIM];
    
} holo_cfc_q15_t;

// ════════════════════════════════════════════════════════════════════════════
// Packet Format (16 bytes)
// Hidden state: 8 neurons × int16_t = 16 bytes... too big.
// Compress: pack 8 Q15 values to 8 bytes (take high byte only)
// ════════════════════════════════════════════════════════════════════════════

typedef struct __attribute__((packed, aligned(4))) {
    uint8_t  hidden[HOLO_Q15_HIDDEN_DIM]; // Compressed hidden state (high byte of Q15)
    uint8_t  node_id;       // Source node
    uint8_t  confidence;    // Self-assessed (0-255)
    uint8_t  entropy_high;  // Count of high-entropy neurons
    uint8_t  flags;         // Status flags
    uint16_t sequence;      // Ordering/dedup
    uint16_t checksum;      // Integrity
} holo_q15_packet_t;

#define HOLO_Q15_FLAG_ANOMALY   0x01
#define HOLO_Q15_FLAG_WAKE      0x02
#define HOLO_Q15_FLAG_LEARNING  0x04
#define HOLO_Q15_FLAG_LEADER    0x08

// ════════════════════════════════════════════════════════════════════════════
// Neighbor State
// ════════════════════════════════════════════════════════════════════════════

typedef struct {
    int16_t  hidden[HOLO_Q15_HIDDEN_DIM];   // Full Q15 hidden (expanded)
    uint8_t  node_id;
    uint8_t  confidence;
    uint16_t last_sequence;
    uint32_t last_seen_tick;
    bool     active;
} holo_q15_neighbor_t;

// ════════════════════════════════════════════════════════════════════════════
// Hologram Node (complete state)
// ════════════════════════════════════════════════════════════════════════════

typedef struct {
    // ─────────────────────────────────────────────────────────
    // Q15 CfC Engine
    // ─────────────────────────────────────────────────────────
    holo_cfc_q15_t cfc;
    
    // ─────────────────────────────────────────────────────────
    // Identity
    // ─────────────────────────────────────────────────────────
    uint8_t node_id;
    
    // ─────────────────────────────────────────────────────────
    // Neighbors
    // ─────────────────────────────────────────────────────────
    holo_q15_neighbor_t neighbors[HOLO_Q15_MAX_NEIGHBORS];
    uint8_t neighbor_count;
    
    // ─────────────────────────────────────────────────────────
    // Interference (per neuron, in Q15)
    // ─────────────────────────────────────────────────────────
    int16_t constructive[HOLO_Q15_HIDDEN_DIM];  // Agreement
    int16_t destructive[HOLO_Q15_HIDDEN_DIM];   // Disagreement magnitude
    int16_t confidence;                          // Overall (Q15)
    
    // ─────────────────────────────────────────────────────────
    // Entropy & Learning (per neuron)
    // ─────────────────────────────────────────────────────────
    uint8_t neuron_entropy[HOLO_Q15_HIDDEN_DIM];
    uint64_t crystallized_mask;  // Bitmask: which neurons are stable (up to 64)
    uint64_t potential_mask;     // Bitmask: which neurons are ready (up to 64)
    
    // ─────────────────────────────────────────────────────────
    // Output
    // ─────────────────────────────────────────────────────────
    int16_t last_output[HOLO_Q15_HIDDEN_DIM];
    int16_t last_input[HOLO_Q15_INPUT_DIM];
    
    // ─────────────────────────────────────────────────────────
    // Statistics
    // ─────────────────────────────────────────────────────────
    uint32_t tick_count;
    uint32_t agreement_count;
    uint32_t disagreement_count;
    uint32_t crystallization_count;
    
} holo_q15_node_t;

// ════════════════════════════════════════════════════════════════════════════
// Weight Initialization (Random Ternary)
// ════════════════════════════════════════════════════════════════════════════

/**
 * Simple LCG PRNG for weight init
 */
static inline uint32_t holo_q15_rand(uint32_t* state) {
    *state = *state * 1103515245 + 12345;
    return (*state >> 16) & 0x7FFF;
}

/**
 * Initialize sparse ternary weights with ~80% sparsity
 */
static inline void holo_q15_init_sparse(
    CfcSparseWeights* sw,
    uint32_t* rng_state,
    int input_dim,
    int hidden_dim
) {
    const int concat_dim = input_dim + hidden_dim;
    
    for (int h = 0; h < hidden_dim; h++) {
        int pos_count = 0;
        int neg_count = 0;
        
        // Gate row
        for (int c = 0; c < concat_dim; c++) {
            uint32_t r = holo_q15_rand(rng_state) % 10;
            if (r == 0 && pos_count < HOLO_Q15_MAX_CONCAT - 1) {
                sw->gate[h].pos_idx[pos_count++] = (int8_t)c;
            } else if (r == 1 && neg_count < HOLO_Q15_MAX_CONCAT - 1) {
                sw->gate[h].neg_idx[neg_count++] = (int8_t)c;
            }
        }
        sw->gate[h].pos_idx[pos_count] = -1;  // Sentinel
        sw->gate[h].neg_idx[neg_count] = -1;
        
        pos_count = 0;
        neg_count = 0;
        
        // Candidate row
        for (int c = 0; c < concat_dim; c++) {
            uint32_t r = holo_q15_rand(rng_state) % 10;
            if (r == 0 && pos_count < HOLO_Q15_MAX_CONCAT - 1) {
                sw->cand[h].pos_idx[pos_count++] = (int8_t)c;
            } else if (r == 1 && neg_count < HOLO_Q15_MAX_CONCAT - 1) {
                sw->cand[h].neg_idx[neg_count++] = (int8_t)c;
            }
        }
        sw->cand[h].pos_idx[pos_count] = -1;
        sw->cand[h].neg_idx[neg_count] = -1;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Initialization
// ════════════════════════════════════════════════════════════════════════════

static inline void holo_q15_init(holo_q15_node_t* node, uint8_t node_id, uint32_t seed) {
    memset(node, 0, sizeof(holo_q15_node_t));
    
    node->node_id = node_id;
    
    // Initialize LUTs (required before first use)
    Q15_LUT_INIT();
    
    // Initialize sparse weights
    uint32_t rng = seed;
    holo_q15_init_sparse(&node->cfc.sparse, &rng,
                         HOLO_Q15_INPUT_DIM, HOLO_Q15_HIDDEN_DIM);
    
    // Initialize biases (small random values in Q4.11)
    for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
        int16_t bias = (int16_t)((holo_q15_rand(&rng) % 512) - 256);
        node->cfc.b_gate[i] = bias;
        node->cfc.b_cand[i] = bias;
    }
    
    // Precompute decay (dt=0.01, tau=0.1 → decay ≈ 0.9)
    // decay = exp(-0.01/0.1) = exp(-0.1) ≈ 0.905
    // In Q15: 0.905 * 32767 ≈ 29654
    for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
        node->cfc.decay[i] = 29654;
    }
    
    // Initialize hidden state to small random values
    for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
        node->cfc.hidden[i] = (int16_t)((holo_q15_rand(&rng) % 4096) - 2048);
    }
    
    // Initialize entropy
    for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
        node->neuron_entropy[i] = HOLO_Q15_ENTROPY_INITIAL;
    }
    
    node->crystallized_mask = 0;
    node->potential_mask = 0xFFFFFFFFFFFFFFFFULL;  // All 64 neurons are potential initially
    node->confidence = Q15_HALF;   // Start at 0.5
}

// ════════════════════════════════════════════════════════════════════════════
// Neighbor Management
// ════════════════════════════════════════════════════════════════════════════

static inline int holo_q15_find_neighbor(holo_q15_node_t* node, uint8_t nid) {
    for (int i = 0; i < HOLO_Q15_MAX_NEIGHBORS; i++) {
        if (node->neighbors[i].active && node->neighbors[i].node_id == nid) {
            return i;
        }
    }
    return -1;
}

static inline int holo_q15_find_empty(holo_q15_node_t* node) {
    for (int i = 0; i < HOLO_Q15_MAX_NEIGHBORS; i++) {
        if (!node->neighbors[i].active) return i;
    }
    return -1;
}

static inline void holo_q15_expire_neighbors(holo_q15_node_t* node) {
    uint8_t active = 0;
    for (int i = 0; i < HOLO_Q15_MAX_NEIGHBORS; i++) {
        if (node->neighbors[i].active) {
            if (node->tick_count - node->neighbors[i].last_seen_tick > HOLO_Q15_NEIGHBOR_TIMEOUT) {
                node->neighbors[i].active = false;
            } else {
                active++;
            }
        }
    }
    node->neighbor_count = active;
}

// ════════════════════════════════════════════════════════════════════════════
// Packet Handling
// ════════════════════════════════════════════════════════════════════════════

static inline uint16_t holo_q15_checksum(const holo_q15_packet_t* pkt) {
    const uint8_t* data = (const uint8_t*)pkt;
    uint16_t sum = 0;
    // Checksum everything except the checksum field itself
    const int len = sizeof(holo_q15_packet_t) - sizeof(uint16_t);
    for (int i = 0; i < len; i++) {
        sum ^= data[i];
        sum = (sum << 1) | (sum >> 15);
    }
    return sum;
}

static inline void holo_q15_create_packet(const holo_q15_node_t* node, holo_q15_packet_t* pkt) {
    // Compress hidden state: take high byte of each Q15
    for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
        pkt->hidden[i] = (uint8_t)((node->cfc.hidden[i] >> 8) + 128);
    }
    
    pkt->node_id = node->node_id;
    pkt->confidence = (uint8_t)((node->confidence >> 7) & 0xFF);  // Q15 to 0-255
    pkt->entropy_high = __builtin_popcountll(node->potential_mask);
    pkt->flags = 0;
    if (node->crystallization_count > 0) pkt->flags |= HOLO_Q15_FLAG_LEARNING;
    pkt->sequence = (uint16_t)(node->tick_count & 0xFFFF);
    pkt->checksum = holo_q15_checksum(pkt);
}

static inline bool holo_q15_verify_packet(const holo_q15_packet_t* pkt) {
    return pkt->checksum == holo_q15_checksum(pkt);
}

static inline bool holo_q15_receive(holo_q15_node_t* node, const holo_q15_packet_t* pkt) {
    if (pkt->node_id == node->node_id) return false;
    
    int slot = holo_q15_find_neighbor(node, pkt->node_id);
    if (slot < 0) {
        slot = holo_q15_find_empty(node);
        if (slot < 0) return false;
        node->neighbor_count++;
    }
    
    holo_q15_neighbor_t* n = &node->neighbors[slot];
    
    // Expand compressed hidden state back to Q15
    for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
        n->hidden[i] = (int16_t)(((int16_t)pkt->hidden[i] - 128) << 8);
    }
    
    n->node_id = pkt->node_id;
    n->confidence = pkt->confidence;
    n->last_sequence = pkt->sequence;
    n->last_seen_tick = node->tick_count;
    n->active = true;
    
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
// Interference Computation (Q15)
// ════════════════════════════════════════════════════════════════════════════

static inline void holo_q15_compute_interference(holo_q15_node_t* node) {
    // Start with our hidden state
    for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
        node->constructive[i] = node->cfc.hidden[i];
        node->destructive[i] = 0;
    }
    
    int total_agree = 0;
    int total_disagree = 0;
    
    // Fold in neighbors
    for (int n = 0; n < HOLO_Q15_MAX_NEIGHBORS; n++) {
        if (!node->neighbors[n].active) continue;
        
        for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
            int16_t theirs = node->neighbors[n].hidden[i];
            int16_t ours = node->cfc.hidden[i];
            
            // Agreement: average (moves constructive toward consensus)
            node->constructive[i] = (int16_t)((node->constructive[i] + theirs) / 2);
            
            // Disagreement: absolute difference
            int16_t diff = (int16_t)(ours > theirs ? ours - theirs : theirs - ours);
            if (diff > node->destructive[i]) {
                node->destructive[i] = diff;
            }
            
            // Count for confidence
            if (diff < 4096) {  // Within ~12.5% agreement
                total_agree++;
            } else {
                total_disagree++;
            }
        }
    }
    
    // Confidence = agree / (agree + disagree), in Q15
    if (total_agree + total_disagree > 0) {
        node->confidence = (int16_t)((total_agree * Q15_ONE) / (total_agree + total_disagree));
    } else {
        node->confidence = Q15_HALF;
    }
    
    node->agreement_count += total_agree;
    node->disagreement_count += total_disagree;
}

// ════════════════════════════════════════════════════════════════════════════
// Entropy & Crystallization
// ════════════════════════════════════════════════════════════════════════════

static inline void holo_q15_update_entropy(holo_q15_node_t* node) {
    for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
        // High destructive → high entropy
        if (node->destructive[i] > 8192) {  // > 25% disagreement
            if (node->neuron_entropy[i] < 255) {
                node->neuron_entropy[i]++;
            }
        }
        // Low destructive → low entropy
        else if (node->destructive[i] < 2048) {  // < 6% disagreement
            if (node->neuron_entropy[i] > 0) {
                node->neuron_entropy[i]--;
            }
        }
        
        // Update masks
        uint64_t bit = (1ULL << i);
        if (node->neuron_entropy[i] < HOLO_Q15_ENTROPY_CRYSTALLIZE) {
            if (!(node->crystallized_mask & bit)) {
                node->crystallized_mask |= bit;
                node->potential_mask &= ~bit;
                node->crystallization_count++;
            }
        } else if (node->neuron_entropy[i] > HOLO_Q15_ENTROPY_POTENTIAL) {
            node->potential_mask |= bit;
            node->crystallized_mask &= ~bit;
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Core Tick (Zero Float)
// ════════════════════════════════════════════════════════════════════════════

/**
 * Main tick - runs the Q15 CfC and computes interference
 *
 * Input: 4 × int16_t in Q4.11 format
 * Output: 8 × int16_t hidden state (the hologram fragment)
 */
static inline void holo_q15_tick(
    holo_q15_node_t* node,
    const int16_t* input_q11,
    int16_t* output_q15
) {
    node->tick_count++;
    
    // Save input
    memcpy(node->last_input, input_q11, HOLO_Q15_INPUT_DIM * sizeof(int16_t));
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 1: Expire neighbors (every 100 ticks)
    // ═══════════════════════════════════════════════════════════
    if ((node->tick_count % 100) == 0) {
        holo_q15_expire_neighbors(node);
    }
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 2: Compute interference from mesh
    // ═══════════════════════════════════════════════════════════
    holo_q15_compute_interference(node);
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 3: Augment input with constructive interference
    // Replace lower-confidence inputs with consensus values
    // ═══════════════════════════════════════════════════════════
    int16_t augmented_input[HOLO_Q15_INPUT_DIM];
    for (int i = 0; i < HOLO_Q15_INPUT_DIM; i++) {
        // Blend: local × (1 - conf) + constructive × conf
        // For now, just use local input
        augmented_input[i] = input_q11[i];
    }
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 4: Run Q15 CfC inference (ZERO FLOAT)
    // ═══════════════════════════════════════════════════════════
    int16_t new_hidden[HOLO_Q15_HIDDEN_DIM];
    
    CFC_CELL_SPARSE_Q15(
        augmented_input,
        node->cfc.hidden,
        &node->cfc.sparse,
        node->cfc.b_gate,
        node->cfc.b_cand,
        node->cfc.decay,
        HOLO_Q15_INPUT_DIM,
        HOLO_Q15_HIDDEN_DIM,
        new_hidden
    );
    
    // Update hidden state
    memcpy(node->cfc.hidden, new_hidden, HOLO_Q15_HIDDEN_DIM * sizeof(int16_t));
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 5: Update entropy (learning)
    // ═══════════════════════════════════════════════════════════
    holo_q15_update_entropy(node);
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 6: Modulate output by confidence
    // ═══════════════════════════════════════════════════════════
    for (int i = 0; i < HOLO_Q15_HIDDEN_DIM; i++) {
        uint64_t bit = (1ULL << i);
        
        if (node->confidence < HOLO_Q15_CONF_LOW) {
            // Low confidence: only output crystallized neurons
            if (node->crystallized_mask & bit) {
                output_q15[i] = new_hidden[i];
            } else {
                output_q15[i] = 0;
            }
        } else if (node->confidence < HOLO_Q15_CONF_HIGH) {
            // Medium: scale by confidence
            output_q15[i] = q15_mul(new_hidden[i], node->confidence);
        } else {
            // High confidence: full output
            output_q15[i] = new_hidden[i];
        }
    }
    
    memcpy(node->last_output, output_q15, HOLO_Q15_HIDDEN_DIM * sizeof(int16_t));
}

// ════════════════════════════════════════════════════════════════════════════
// Statistics
// ════════════════════════════════════════════════════════════════════════════

typedef struct {
    uint32_t tick_count;
    uint8_t  neighbor_count;
    int16_t  confidence_q15;
    float    confidence_f;
    uint8_t  crystallized_count;
    uint8_t  potential_count;
    uint32_t agreements;
    uint32_t disagreements;
    uint32_t crystallizations;
} holo_q15_stats_t;

static inline holo_q15_stats_t holo_q15_get_stats(const holo_q15_node_t* node) {
    holo_q15_stats_t s;
    s.tick_count = node->tick_count;
    s.neighbor_count = node->neighbor_count;
    s.confidence_q15 = node->confidence;
    s.confidence_f = q15_to_float(node->confidence);
    s.crystallized_count = __builtin_popcountll(node->crystallized_mask);
    s.potential_count = __builtin_popcountll(node->potential_mask);
    s.agreements = node->agreement_count;
    s.disagreements = node->disagreement_count;
    s.crystallizations = node->crystallization_count;
    return s;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_HOLOGRAM_Q15_H
