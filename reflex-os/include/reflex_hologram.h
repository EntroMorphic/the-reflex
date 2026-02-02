/**
 * reflex_hologram.h - Holographic Intelligence Engine
 *
 * Each node is not a PART of the brain. Each node IS the brain.
 * The intelligence exists in the INTERFERENCE PATTERN.
 *
 * Core Principles:
 *   1. Every node contains the whole (at its resolution)
 *   2. Interference IS intelligence (agreement = confidence)
 *   3. Entropy drives learning (crystallization from agreement)
 *
 * Like a hologram: cut it in half, you get two complete images.
 */

#ifndef REFLEX_HOLOGRAM_H
#define REFLEX_HOLOGRAM_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "reflex_cfc_turbo.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Configuration
// ============================================================

#define HOLOGRAM_MAX_NEIGHBORS      8       // Max nodes in mesh
#define HOLOGRAM_NEIGHBOR_TIMEOUT   1000    // Ticks before neighbor expires

// Entropy thresholds
#define ENTROPY_CRYSTALLIZE         10      // Below this = stable/crystallized
#define ENTROPY_POTENTIAL           200     // Above this = ready to change
#define ENTROPY_INITIAL             128     // Starting entropy per bit

// Confidence thresholds
#define CONFIDENCE_HIGH             0.7f    // Above = full trust
#define CONFIDENCE_LOW              0.3f    // Below = suppress uncertain bits

// ============================================================
// Packet Format (16 bytes, fits IEEE 802.15.4 nicely)
// ============================================================

typedef struct __attribute__((packed, aligned(4))) {
    uint64_t hidden;        // CfC hidden state (the hologram fragment)
    uint8_t  node_id;       // Source node identifier
    uint8_t  confidence;    // Self-assessed confidence (0-255 = 0.0-1.0)
    uint8_t  entropy_high;  // Count of high-entropy bits (potential)
    uint8_t  flags;         // Status flags (see below)
    uint16_t sequence;      // For ordering and dedup
    uint16_t checksum;      // Simple integrity check
} hologram_packet_t;

// Packet flags
#define HOLOGRAM_FLAG_ANOMALY       0x01    // Anomaly detected
#define HOLOGRAM_FLAG_WAKE          0x02    // Request HP core wake
#define HOLOGRAM_FLAG_LEARNING      0x04    // Active crystallization
#define HOLOGRAM_FLAG_LEADER        0x08    // Highest confidence node

// ============================================================
// Neighbor State
// ============================================================

typedef struct {
    uint64_t hidden;            // Last received hidden state
    uint8_t  node_id;           // Neighbor's ID
    uint8_t  confidence;        // Their self-reported confidence
    uint16_t last_sequence;     // For dedup
    uint32_t last_seen_tick;    // For timeout detection
    bool     active;            // Currently in mesh
} hologram_neighbor_t;

// ============================================================
// Hologram Node (complete state for one node)
// ============================================================

typedef struct {
    // ─────────────────────────────────────────────────────────
    // Neural Network
    // ─────────────────────────────────────────────────────────
    cfc_turbo_layer_t cfc;
    
    // ─────────────────────────────────────────────────────────
    // Identity
    // ─────────────────────────────────────────────────────────
    uint8_t node_id;
    
    // ─────────────────────────────────────────────────────────
    // Neighbor Tracking
    // ─────────────────────────────────────────────────────────
    hologram_neighbor_t neighbors[HOLOGRAM_MAX_NEIGHBORS];
    uint8_t neighbor_count;     // Active neighbors
    
    // ─────────────────────────────────────────────────────────
    // Interference State
    // ─────────────────────────────────────────────────────────
    uint64_t constructive;      // AND of all hidden states (agreement)
    uint64_t destructive;       // XOR bits (where anyone disagrees)
    float    confidence;        // Agreement ratio (0.0 - 1.0)
    
    // ─────────────────────────────────────────────────────────
    // Entropy & Learning
    // ─────────────────────────────────────────────────────────
    uint8_t  bit_entropy[64];   // Per-bit entropy (fluctuation tracking)
    uint64_t crystallized;      // Low-entropy bits (stable features)
    uint64_t potential;         // High-entropy bits (ready to learn)
    
    // ─────────────────────────────────────────────────────────
    // Output State
    // ─────────────────────────────────────────────────────────
    uint64_t last_output;
    uint64_t last_input;
    
    // ─────────────────────────────────────────────────────────
    // Statistics
    // ─────────────────────────────────────────────────────────
    uint32_t tick_count;
    uint32_t agreement_count;
    uint32_t disagreement_count;
    uint32_t crystallization_count;
    uint32_t packets_sent;
    uint32_t packets_received;
    
} hologram_node_t;

// ============================================================
// Initialization
// ============================================================

/**
 * Initialize hologram node with random CfC weights
 */
static inline void hologram_init(hologram_node_t* node, uint8_t node_id, uint32_t seed) {
    memset(node, 0, sizeof(hologram_node_t));
    
    node->node_id = node_id;
    
    // Initialize CfC
    cfc_turbo_init_random(&node->cfc, seed);
    
    // Initialize entropy to middle value (uncertain)
    for (int i = 0; i < 64; i++) {
        node->bit_entropy[i] = ENTROPY_INITIAL;
    }
    
    // Nothing crystallized yet
    node->crystallized = 0;
    node->potential = ~0ULL;  // All bits are potential initially
    
    node->confidence = 0.5f;  // Start uncertain
}

// ============================================================
// Neighbor Management
// ============================================================

/**
 * Find neighbor slot by ID, or return -1
 */
static inline int hologram_find_neighbor(hologram_node_t* node, uint8_t neighbor_id) {
    for (int i = 0; i < HOLOGRAM_MAX_NEIGHBORS; i++) {
        if (node->neighbors[i].active && node->neighbors[i].node_id == neighbor_id) {
            return i;
        }
    }
    return -1;
}

/**
 * Find empty neighbor slot, or return -1
 */
static inline int hologram_find_empty_slot(hologram_node_t* node) {
    for (int i = 0; i < HOLOGRAM_MAX_NEIGHBORS; i++) {
        if (!node->neighbors[i].active) {
            return i;
        }
    }
    return -1;
}

/**
 * Expire old neighbors
 */
static inline void hologram_expire_neighbors(hologram_node_t* node) {
    uint8_t active = 0;
    for (int i = 0; i < HOLOGRAM_MAX_NEIGHBORS; i++) {
        if (node->neighbors[i].active) {
            if (node->tick_count - node->neighbors[i].last_seen_tick > HOLOGRAM_NEIGHBOR_TIMEOUT) {
                node->neighbors[i].active = false;
            } else {
                active++;
            }
        }
    }
    node->neighbor_count = active;
}

/**
 * Receive neighbor packet
 */
static inline bool hologram_receive(hologram_node_t* node, const hologram_packet_t* pkt) {
    // Ignore our own packets
    if (pkt->node_id == node->node_id) return false;
    
    // Find or allocate slot
    int slot = hologram_find_neighbor(node, pkt->node_id);
    if (slot < 0) {
        slot = hologram_find_empty_slot(node);
        if (slot < 0) return false;  // No room
        node->neighbor_count++;
    }
    
    // Update neighbor state
    hologram_neighbor_t* n = &node->neighbors[slot];
    n->hidden = pkt->hidden;
    n->node_id = pkt->node_id;
    n->confidence = pkt->confidence;
    n->last_sequence = pkt->sequence;
    n->last_seen_tick = node->tick_count;
    n->active = true;
    
    node->packets_received++;
    return true;
}

// ============================================================
// Interference Computation
// ============================================================

/**
 * Compute interference pattern from all known hidden states
 * 
 * Constructive: bits where ALL nodes agree (AND)
 * Destructive: bits where ANY nodes disagree (accumulated XOR)
 */
static inline void hologram_compute_interference(hologram_node_t* node) {
    // Start with our own hidden state
    node->constructive = node->cfc.hidden;
    node->destructive = 0;
    
    uint8_t total_nodes = 1;  // Count ourselves
    
    // Fold in each active neighbor
    for (int i = 0; i < HOLOGRAM_MAX_NEIGHBORS; i++) {
        if (!node->neighbors[i].active) continue;
        
        uint64_t their_hidden = node->neighbors[i].hidden;
        
        // Constructive: AND (bits where we ALL agree)
        node->constructive &= their_hidden;
        
        // Destructive: XOR (bits where we differ)
        node->destructive |= (node->cfc.hidden ^ their_hidden);
        
        total_nodes++;
    }
    
    // Compute confidence as agreement ratio
    uint8_t agree_bits = __builtin_popcountll(node->constructive);
    uint8_t disagree_bits = __builtin_popcountll(node->destructive);
    
    if (agree_bits + disagree_bits > 0) {
        node->confidence = (float)agree_bits / (float)(agree_bits + disagree_bits);
    } else {
        node->confidence = 1.0f;  // No data = assume agreement
    }
    
    // Boost confidence if we have more neighbors (more perspectives)
    // Single node = 50% max confidence, 8 nodes = 100% possible
    float neighbor_factor = (float)(total_nodes) / (float)(HOLOGRAM_MAX_NEIGHBORS + 1);
    node->confidence *= (0.5f + 0.5f * neighbor_factor);
}

// ============================================================
// Entropy & Crystallization
// ============================================================

/**
 * Update entropy based on current interference
 * Entropy increases with disagreement, decreases with agreement
 */
static inline void hologram_update_entropy(hologram_node_t* node) {
    for (int b = 0; b < 64; b++) {
        uint64_t mask = 1ULL << b;
        
        if (node->destructive & mask) {
            // Disagreement on this bit → entropy increases
            if (node->bit_entropy[b] < 255) {
                node->bit_entropy[b]++;
            }
            node->disagreement_count++;
        }
        else if (node->constructive & mask) {
            // Agreement on this bit → entropy decreases
            if (node->bit_entropy[b] > 0) {
                node->bit_entropy[b]--;
            }
            node->agreement_count++;
        }
        
        // Check for crystallization (low entropy = stable)
        if (node->bit_entropy[b] < ENTROPY_CRYSTALLIZE) {
            if (!(node->crystallized & mask)) {
                node->crystallized |= mask;
                node->potential &= ~mask;
                node->crystallization_count++;
            }
        }
        // Check for potential (high entropy = ready to change)
        else if (node->bit_entropy[b] > ENTROPY_POTENTIAL) {
            node->potential |= mask;
            // Crystallized bits can "melt" if entropy rises
            node->crystallized &= ~mask;
        }
    }
}

// ============================================================
// Core Tick Function
// ============================================================

/**
 * Main hologram tick - run at ~1kHz on LP Core
 *
 * Input: local sensor data (32 bits used, rest for hologram augmentation)
 * Output: action/response (64 bits, modulated by confidence)
 */
static inline uint64_t hologram_tick(hologram_node_t* node, uint64_t local_input) {
    node->tick_count++;
    node->last_input = local_input;
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 1: Expire old neighbors
    // ═══════════════════════════════════════════════════════════
    if ((node->tick_count % 100) == 0) {
        hologram_expire_neighbors(node);
    }
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 2: Compute interference from mesh
    // ═══════════════════════════════════════════════════════════
    hologram_compute_interference(node);
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 3: Augment input with hologram (agreed crystallized bits)
    // ═══════════════════════════════════════════════════════════
    
    // Local input in lower 32 bits
    // Hologram context in upper 32 bits
    uint64_t hologram_context = (node->constructive & node->crystallized) >> 32;
    uint64_t augmented_input = (local_input & 0xFFFFFFFFULL) | (hologram_context << 32);
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 4: Run CfC inference
    // ═══════════════════════════════════════════════════════════
    uint64_t raw_output;
    cfc_turbo_forward(&node->cfc, augmented_input, &raw_output);
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 5: Update entropy (learning)
    // ═══════════════════════════════════════════════════════════
    hologram_update_entropy(node);
    
    // ═══════════════════════════════════════════════════════════
    // PHASE 6: Modulate output by confidence
    // ═══════════════════════════════════════════════════════════
    uint64_t output = raw_output;
    
    if (node->confidence < CONFIDENCE_LOW) {
        // Very low confidence: only output crystallized bits
        output &= node->crystallized;
    }
    else if (node->confidence < CONFIDENCE_HIGH) {
        // Medium confidence: prefer crystallized, allow some potential
        uint64_t allowed = node->crystallized | (node->potential & raw_output);
        output &= allowed;
    }
    // High confidence: full output
    
    node->last_output = output;
    return output;
}

// ============================================================
// Packet Creation
// ============================================================

/**
 * Simple checksum (XOR all bytes)
 */
static inline uint16_t hologram_checksum(const hologram_packet_t* pkt) {
    const uint8_t* data = (const uint8_t*)pkt;
    uint16_t sum = 0;
    for (int i = 0; i < 14; i++) {  // All bytes except checksum
        sum ^= data[i];
        sum = (sum << 1) | (sum >> 15);  // Rotate
    }
    return sum;
}

/**
 * Create packet for broadcast
 */
static inline void hologram_create_packet(const hologram_node_t* node, hologram_packet_t* pkt) {
    pkt->hidden = node->cfc.hidden;
    pkt->node_id = node->node_id;
    pkt->confidence = (uint8_t)(node->confidence * 255.0f);
    pkt->entropy_high = __builtin_popcountll(node->potential);
    pkt->flags = 0;
    
    // Set flags based on state
    if (node->last_output & 0x01) pkt->flags |= HOLOGRAM_FLAG_ANOMALY;
    if (node->crystallization_count > 0 && 
        (node->tick_count - node->crystallization_count) < 100) {
        pkt->flags |= HOLOGRAM_FLAG_LEARNING;
    }
    
    pkt->sequence = (uint16_t)(node->tick_count & 0xFFFF);
    pkt->checksum = hologram_checksum(pkt);
}

/**
 * Verify packet checksum
 */
static inline bool hologram_verify_packet(const hologram_packet_t* pkt) {
    return pkt->checksum == hologram_checksum(pkt);
}

// ============================================================
// Statistics & Debugging
// ============================================================

typedef struct {
    uint32_t tick_count;
    uint8_t  neighbor_count;
    float    confidence;
    uint8_t  crystallized_bits;
    uint8_t  potential_bits;
    uint32_t agreements;
    uint32_t disagreements;
    uint32_t crystallizations;
    uint32_t packets_sent;
    uint32_t packets_received;
} hologram_stats_t;

static inline hologram_stats_t hologram_get_stats(const hologram_node_t* node) {
    hologram_stats_t stats;
    stats.tick_count = node->tick_count;
    stats.neighbor_count = node->neighbor_count;
    stats.confidence = node->confidence;
    stats.crystallized_bits = __builtin_popcountll(node->crystallized);
    stats.potential_bits = __builtin_popcountll(node->potential);
    stats.agreements = node->agreement_count;
    stats.disagreements = node->disagreement_count;
    stats.crystallizations = node->crystallization_count;
    stats.packets_sent = node->packets_sent;
    stats.packets_received = node->packets_received;
    return stats;
}

/**
 * Print entropy map (for debugging)
 * Shows which bits are crystallized vs potential
 */
static inline void hologram_print_entropy_map(const hologram_node_t* node, char* buf, int buf_size) {
    if (buf_size < 65) return;
    for (int i = 0; i < 64; i++) {
        if (node->crystallized & (1ULL << i)) {
            buf[i] = '#';  // Crystallized
        } else if (node->potential & (1ULL << i)) {
            buf[i] = '.';  // Potential
        } else {
            buf[i] = '-';  // Neutral
        }
    }
    buf[64] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_HOLOGRAM_H
