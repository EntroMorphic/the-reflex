/*
 * ARCHIVED — Historical C Reference Implementation (CMD 1 Only)
 *
 * This file is NOT compiled in production. The authoritative LP core
 * implementation is main.S (hand-written RISC-V assembly, CMD 1-5).
 * This C version implements only CMD 1 (CfC step) and was used during
 * early development to verify the assembly against a C reference.
 * It does NOT implement CMD 2 (VDB search), CMD 3 (VDB insert),
 * CMD 4 (CfC+VDB pipeline), or CMD 5 (CfC+VDB+Feedback).
 *
 * Archived: April 6, 2026
 * See: ../main.S for the complete, production LP core.
 */

/*
 * LP Core Geometric Processor — Ternary CfC on RISC-V ULP
 *
 * This runs on the ESP32-C6's LP core at 16MHz, drawing ~30μA.
 * It implements a ternary CfC liquid neural network using purely
 * geometric operations: intersection (dot product via bitwise AND +
 * popcount), projection (sign), reflection (XOR), and gating.
 *
 * The LP core reads the GIE's hidden state from LP SRAM (written by
 * the HP core's ISR), runs one CfC step, and writes its own hidden
 * state + decision back to LP SRAM.
 *
 * No floating point. No multiplication. Only AND, OR, XOR, popcount,
 * compare. The processor thinks in geometry, not arithmetic.
 *
 * Architecture:
 *   Input:  32 trits (GIE hidden) + 16 trits (LP hidden) = 48 concat
 *   Neurons: 32 (16 f-pathway + 16 g-pathway)
 *   Hidden:  16 trits
 *   Blend:   f=+1 → UPDATE, f=0 → HOLD, f=-1 → INVERT
 *
 * Trit encoding (packed, 2 bits per trit):
 *   Positive mask: bit set where trit == +1
 *   Negative mask: bit set where trit == -1
 *   Zero:          both bits clear
 *   16 trits per uint32_t word (using pos_mask and neg_mask)
 *
 * Memory budget:
 *   Weights:  32 neurons × 6 bytes (48 trits packed) × 2 masks = 384 bytes
 *   State:    ~100 bytes shared
 *   Code:     ~2KB
 *   Stack:    ~1KB
 *   Total:    <4KB
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "ulp_lp_core.h"
#include "ulp_lp_core_utils.h"

/* ══════════════════════════════════════════════════════════════════
 *  GEOMETRIC CONSTANTS
 * ══════════════════════════════════════════════════════════════════ */

#define GIE_HIDDEN_DIM   32   /* Trits from GIE hardware CfC */
#define LP_HIDDEN_DIM    16   /* LP core's own hidden dimension */
#define LP_CONCAT_DIM    (GIE_HIDDEN_DIM + LP_HIDDEN_DIM)  /* 48 */
#define LP_NUM_NEURONS   32   /* 16 f + 16 g pathways */

/* Packed trit words: 16 trits per uint32_t.
 * 48 trits = 3 words. */
#define PACKED_WORDS     3    /* ceil(48/16) = 3 */

/* ══════════════════════════════════════════════════════════════════
 *  SHARED STATE (LP SRAM — visible to both HP and LP cores)
 *
 *  Global variables are exported with ulp_ prefix on HP side.
 *  HP core writes gie_hidden[], LP core reads it.
 *  LP core writes lp_hidden[], lp_decision, lp_step_count.
 * ══════════════════════════════════════════════════════════════════ */

/* GIE → LP: written by HP core ISR after each GIE loop */
volatile int8_t  gie_hidden[GIE_HIDDEN_DIM];

/* LP → HP: written by LP core after each geometric step */
volatile int8_t  lp_hidden[LP_HIDDEN_DIM];
volatile uint32_t lp_step_count;        /* Total LP CfC steps completed */
volatile int8_t  lp_decision;           /* Aggregate decision: +1, 0, -1 */

/* LP internal: weights (initialized by HP core before launch) */
volatile uint32_t lp_W_f_pos[LP_HIDDEN_DIM][PACKED_WORDS];  /* f-pathway positive masks */
volatile uint32_t lp_W_f_neg[LP_HIDDEN_DIM][PACKED_WORDS];  /* f-pathway negative masks */
volatile uint32_t lp_W_g_pos[LP_HIDDEN_DIM][PACKED_WORDS];  /* g-pathway positive masks */
volatile uint32_t lp_W_g_neg[LP_HIDDEN_DIM][PACKED_WORDS];  /* g-pathway negative masks */

/* Diagnostic: raw dots from last step */
volatile int32_t lp_dots_f[LP_HIDDEN_DIM];
volatile int32_t lp_dots_g[LP_HIDDEN_DIM];

/* Sync flag: HP sets to 1 when gie_hidden is fresh, LP clears to 0 after reading */
volatile uint32_t lp_data_ready;

/* ══════════════════════════════════════════════════════════════════
 *  GEOMETRIC PRIMITIVES
 *
 *  These are the core operations. No multiplication anywhere.
 *  Everything is AND, XOR, popcount, add, compare.
 * ══════════════════════════════════════════════════════════════════ */

/* Byte-wise popcount lookup table (256 bytes) */
static const uint8_t popcount_lut[256] = {
    0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8,
};

/* Popcount of a 32-bit word using LUT */
static inline int popcount32(uint32_t x) {
    return popcount_lut[x & 0xFF]
         + popcount_lut[(x >> 8) & 0xFF]
         + popcount_lut[(x >> 16) & 0xFF]
         + popcount_lut[(x >> 24) & 0xFF];
}

/*
 * INTERSECT — The fundamental geometric operation.
 *
 * Computes the ternary dot product of two vectors packed as
 * (pos_mask, neg_mask) pairs. This measures the signed volume
 * of overlap between two regions in discrete ternary space.
 *
 *   agree    = popcount(a_pos & b_pos) + popcount(a_neg & b_neg)
 *   disagree = popcount(a_pos & b_neg) + popcount(a_neg & b_pos)
 *   dot      = agree - disagree
 *
 * No multiplication. Only AND + popcount + subtract.
 */
static int trit_intersect(const uint32_t *a_pos, const uint32_t *a_neg,
                           const uint32_t *b_pos, const uint32_t *b_neg,
                           int n_words) {
    int agree = 0, disagree = 0;
    for (int i = 0; i < n_words; i++) {
        agree    += popcount32(a_pos[i] & b_pos[i]);
        agree    += popcount32(a_neg[i] & b_neg[i]);
        disagree += popcount32(a_pos[i] & b_neg[i]);
        disagree += popcount32(a_neg[i] & b_pos[i]);
    }
    return agree - disagree;
}

/*
 * PROJECT — Map a continuous value back onto the ternary axis.
 *
 * sign(x): x>0 → +1, x<0 → -1, x==0 → 0
 *
 * This is the projection from "how much overlap?" to
 * "which direction?" — the discrete geometric decision.
 */
static inline int8_t trit_sign(int x) {
    if (x > 0) return 1;
    if (x < 0) return -1;
    return 0;
}

/*
 * GATE — Conditional geometric movement.
 *
 * f == +1 (UPDATE):  state ← candidate  (move to new position)
 * f ==  0 (HOLD):    state ← keep       (stay where you are)
 * f == -1 (INVERT):  state ← -candidate (reflect across origin)
 *
 * No multiplication. Uses XOR on sign bits:
 *   same sign → +1, opposite sign → -1, either zero → 0
 *
 * The three blend modes create non-gradient dynamics:
 * oscillation, convergence resistance, and path-dependent memory.
 */
static inline int8_t trit_gate(int8_t f, int8_t keep, int8_t candidate) {
    if (f == 0) return keep;
    /* Ternary multiply without MUL:
     * If f == +1: return candidate
     * If f == -1: return -candidate (negate = 0 - candidate) */
    if (f > 0) return candidate;
    return -candidate;  /* neg is subtract, not MUL */
}

/* ══════════════════════════════════════════════════════════════════
 *  PACKING / UNPACKING
 *
 *  Convert between int8_t trit arrays and packed (pos, neg) masks.
 *  16 trits per 32-bit word.
 * ══════════════════════════════════════════════════════════════════ */

/*
 * Pack a trit vector into (pos_mask[], neg_mask[]) format.
 * Bit i of word j corresponds to trit[j*16 + i].
 */
static void pack_trits(const int8_t *trits, int n_trits,
                       uint32_t *pos, uint32_t *neg, int n_words) {
    for (int w = 0; w < n_words; w++) {
        uint32_t p = 0, n = 0;
        for (int b = 0; b < 16 && (w * 16 + b) < n_trits; b++) {
            int8_t t = trits[w * 16 + b];
            if (t > 0) p |= (1u << b);
            if (t < 0) n |= (1u << b);
        }
        pos[w] = p;
        neg[w] = n;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  LP CORE MAIN — Geometric CfC Step
 *
 *  Called every ~10ms by LP timer wakeup. Runs one CfC step:
 *    1. Read GIE hidden state from shared LP SRAM
 *    2. Concatenate with LP hidden state
 *    3. Pack into geometric format
 *    4. For each hidden neuron: INTERSECT, PROJECT, GATE
 *    5. Write new LP hidden state + decision
 * ══════════════════════════════════════════════════════════════════ */

int main(void) {
    /* Skip if no fresh data from GIE */
    if (!lp_data_ready) {
        return 0;
    }
    lp_data_ready = 0;

    /* Read LP timer cycle count for performance measurement */
    /* (LP core has no cycle counter, use step count as proxy) */

    /* ── 1. Build concatenated input: [gie_hidden | lp_hidden] ── */
    int8_t concat[LP_CONCAT_DIM];
    for (int i = 0; i < GIE_HIDDEN_DIM; i++) {
        concat[i] = gie_hidden[i];
    }
    for (int i = 0; i < LP_HIDDEN_DIM; i++) {
        concat[GIE_HIDDEN_DIM + i] = lp_hidden[i];
    }

    /* ── 2. Pack into geometric format ── */
    uint32_t x_pos[PACKED_WORDS], x_neg[PACKED_WORDS];
    pack_trits(concat, LP_CONCAT_DIM, x_pos, x_neg, PACKED_WORDS);

    /* ── 3. Geometric CfC step ── */
    int8_t h_new[LP_HIDDEN_DIM];

    for (int n = 0; n < LP_HIDDEN_DIM; n++) {
        /* INTERSECT: measure alignment with f and g weight directions */
        int dot_f = trit_intersect(
            (const uint32_t *)lp_W_f_pos[n], (const uint32_t *)lp_W_f_neg[n],
            x_pos, x_neg, PACKED_WORDS);
        int dot_g = trit_intersect(
            (const uint32_t *)lp_W_g_pos[n], (const uint32_t *)lp_W_g_neg[n],
            x_pos, x_neg, PACKED_WORDS);

        /* PROJECT: map intersection magnitude to discrete direction */
        int8_t f = trit_sign(dot_f);
        int8_t g = trit_sign(dot_g);

        /* GATE: geometric state transition */
        h_new[n] = trit_gate(f, lp_hidden[n], g);

        /* Publish raw dots for diagnostics */
        lp_dots_f[n] = dot_f;
        lp_dots_g[n] = dot_g;
    }

    /* ── 4. Commit new hidden state ── */
    for (int i = 0; i < LP_HIDDEN_DIM; i++) {
        lp_hidden[i] = h_new[i];
    }

    /* ── 5. Compute decision: majority vote of hidden state ──
     * This is itself a geometric operation: project the hidden
     * vector onto the all-ones direction. Positive = "yes",
     * negative = "no", zero = "uncertain". */
    int vote = 0;
    for (int i = 0; i < LP_HIDDEN_DIM; i++) {
        vote += lp_hidden[i];
    }
    lp_decision = trit_sign(vote);

    /* ── 6. Increment step counter ── */
    lp_step_count++;

    /* ulp_lp_core_halt() is called automatically when main returns */
    return 0;
}
