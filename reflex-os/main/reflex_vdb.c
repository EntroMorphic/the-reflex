/*
 * reflex_vdb.c — Ternary Vector Database API Implementation (NSW Graph)
 *
 * HP-side interface to the LP core's ternary vector database.
 * Handles trit packing, LP SRAM writes, command dispatch, and
 * result readback. The actual search and graph construction run
 * on the LP core in hand-written RISC-V assembly (main.S).
 *
 * Node layout in LP SRAM (32 bytes):
 *   [0..11]   pos_mask[3]      (3 x uint32_t)
 *   [12..23]  neg_mask[3]      (3 x uint32_t)
 *   [24..30]  neighbors[7]     (7 x uint8_t)
 *   [31]      neighbor_count   (uint8_t)
 *
 * Insert flow:
 *   1. HP packs vector into LP SRAM at node[new_id]
 *   2. HP sets vdb_insert_id = new_id
 *   3. HP sets lp_command = 3
 *   4. LP wakes, brute-force scores all existing nodes
 *   5. LP selects top-M=7 neighbors
 *   6. LP writes forward + reverse edges
 *   7. LP increments vdb_node_count and vdb_insert_count
 */

#include "reflex_vdb.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ulp_lp_core.h"
#include "ulp_main.h"

/* ── LP SRAM access helper ──
 * The ULP build system declares all symbols as `extern uint32_t`.
 * This helper prevents GCC's -Warray-bounds from seeing through
 * the cast when accessing larger data structures. */
static inline void * __attribute__((always_inline))
vdb_ulp_addr(const volatile void *sym) {
    uintptr_t addr;
    __asm__ volatile("" : "=r"(addr) : "0"(sym));
    return (void*)addr;
}

/* ── Trit packing ──
 * Pack a trit vector into (pos_mask, neg_mask) format for LP core.
 * 16 trits per 32-bit word. */
static void pack_trits(const int8_t *trits, int n_trits,
                       volatile uint32_t *pos, volatile uint32_t *neg,
                       int n_words) {
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

/* Internal: number of packed words per vector */
#define VDB_PACKED_WORDS  3   /* ceil(48/16) */

/* Internal: words per node in LP SRAM (32 bytes = 8 words) */
#define VDB_NODE_WORDS    8

/* ══════════════════════════════════════════════════════════════════
 *  PUBLIC API
 * ══════════════════════════════════════════════════════════════════ */

void vdb_init(void) {
    ulp_vdb_node_count = 0;
}

int vdb_insert(const int8_t *trit_vec) {
    uint32_t count = ulp_vdb_node_count;
    if (count >= VDB_MAX_NODES) return -1;

    /* Write the vector portion to the node slot in LP SRAM.
     * Node is 32 bytes: [pos 12B][neg 12B][neighbors 7B][ncnt 1B].
     * We write to the first 24 bytes (vector), LP core handles the rest. */
    volatile uint32_t *nodes = (volatile uint32_t *)vdb_ulp_addr(&ulp_vdb_nodes);
    volatile uint32_t *node_pos = &nodes[count * VDB_NODE_WORDS];
    volatile uint32_t *node_neg = &nodes[count * VDB_NODE_WORDS + VDB_PACKED_WORDS];

    pack_trits(trit_vec, VDB_TRIT_DIM, node_pos, node_neg, VDB_PACKED_WORDS);

    /* Tell LP core which node to insert and dispatch graph build */
    ulp_vdb_insert_id = count;

    uint32_t insert_before = ulp_vdb_insert_count;
    ulp_lp_command = 3;

    /* Wait for LP core to complete graph construction */
    int timeout = 50;  /* 50 x 5ms = 250ms max */
    while (ulp_vdb_insert_count == insert_before && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(5));
        timeout--;
    }

    if (timeout == 0) return -2;

    return (int)count;
}

int vdb_search(const int8_t *query, vdb_result_t *result) {
    /* Pack query into LP SRAM */
    volatile uint32_t *qpos = (volatile uint32_t *)vdb_ulp_addr(&ulp_vdb_query_pos);
    volatile uint32_t *qneg = (volatile uint32_t *)vdb_ulp_addr(&ulp_vdb_query_neg);
    pack_trits(query, VDB_TRIT_DIM, qpos, qneg, VDB_PACKED_WORDS);

    /* Dispatch search command to LP core */
    uint32_t search_before = ulp_vdb_search_count;
    ulp_lp_command = 2;

    /* Wait for LP core to complete */
    int timeout = 50;  /* 50 x 5ms = 250ms max */
    while (ulp_vdb_search_count == search_before && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(5));
        timeout--;
    }

    if (timeout == 0) return -1;

    /* Read results from LP SRAM */
    volatile uint8_t *ids = (volatile uint8_t *)vdb_ulp_addr(&ulp_vdb_result_ids);
    volatile int32_t *scores = (volatile int32_t *)vdb_ulp_addr(&ulp_vdb_result_scores);

    for (int k = 0; k < VDB_K; k++) {
        result->ids[k] = ids[k];
        result->scores[k] = scores[k];
    }

    return 0;
}

void vdb_clear(void) {
    ulp_vdb_node_count = 0;
}

int vdb_count(void) {
    return (int)ulp_vdb_node_count;
}

int vdb_last_visit_count(void) {
    return (int)ulp_vdb_visit_count;
}

int vdb_cfc_pipeline_step(vdb_result_t *result) {
    /* Record counters before dispatch */
    uint32_t step_before = ulp_lp_step_count;
    uint32_t search_before = ulp_vdb_search_count;

    /* Dispatch combined CfC + VDB command */
    ulp_lp_command = 4;

    /* Wait for BOTH counters to increment (one LP wake cycle) */
    int timeout = 50;  /* 50 x 5ms = 250ms max */
    while (timeout > 0) {
        if (ulp_lp_step_count != step_before &&
            ulp_vdb_search_count != search_before) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        timeout--;
    }

    if (timeout == 0) return -1;

    /* Read results from LP SRAM */
    volatile uint8_t *ids = (volatile uint8_t *)vdb_ulp_addr(&ulp_vdb_result_ids);
    volatile int32_t *scores = (volatile int32_t *)vdb_ulp_addr(&ulp_vdb_result_scores);

    for (int k = 0; k < VDB_K; k++) {
        result->ids[k] = ids[k];
        result->scores[k] = scores[k];
    }

    return 0;
}
