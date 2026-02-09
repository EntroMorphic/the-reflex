/*
 * reflex_vdb.c — Ternary Vector Database API Implementation
 *
 * HP-side interface to the LP core's ternary vector database.
 * Handles trit packing, LP SRAM writes, command dispatch, and
 * result readback. The actual search runs on the LP core in
 * hand-written RISC-V assembly (main.S).
 *
 * Node layout in LP SRAM (24 bytes, will grow to 32 for HNSW):
 *   [0..11]   pos_mask[3]   (3 x uint32_t)
 *   [12..23]  neg_mask[3]   (3 x uint32_t)
 *   16 trits per word, 3 words = 48 trits per vector.
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

/* Internal: words per node in LP SRAM (24 bytes = 6 words) */
#define VDB_NODE_WORDS    6

/* ══════════════════════════════════════════════════════════════════
 *  PUBLIC API
 * ══════════════════════════════════════════════════════════════════ */

void vdb_init(void) {
    ulp_vdb_node_count = 0;
}

int vdb_insert(const int8_t *trit_vec) {
    uint32_t count = ulp_vdb_node_count;
    if (count >= VDB_MAX_NODES) return -1;

    /* Write directly to the node slot in LP SRAM */
    volatile uint32_t *nodes = (volatile uint32_t *)vdb_ulp_addr(&ulp_vdb_nodes);
    volatile uint32_t *node_pos = &nodes[count * VDB_NODE_WORDS];
    volatile uint32_t *node_neg = &nodes[count * VDB_NODE_WORDS + VDB_PACKED_WORDS];

    pack_trits(trit_vec, VDB_TRIT_DIM, node_pos, node_neg, VDB_PACKED_WORDS);

    /* Increment count — this makes the node visible to LP core searches */
    ulp_vdb_node_count = count + 1;

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

    /* Wait for LP core to complete (wakes every 10ms, search takes <1ms) */
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
