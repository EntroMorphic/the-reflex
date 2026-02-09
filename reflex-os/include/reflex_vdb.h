/*
 * reflex_vdb.h — Ternary Vector Database API
 *
 * HP-side interface to the LP core's ternary vector database.
 * The VDB stores up to 64 vectors of 48 trits each, packed as
 * (pos_mask, neg_mask) pairs in LP SRAM. Search runs on the LP
 * core at 16MHz via brute-force top-K (K=4) in hand-written
 * RISC-V assembly.
 *
 * All operations are ternary: {-1, 0, +1}. No floating point.
 * No multiplication. Just AND, popcount, add, sub, negate.
 *
 * Usage:
 *   vdb_init();                          // after LP core binary loaded
 *   vdb_clear();                         // reset to empty
 *   int id = vdb_insert(trit_vec);       // add vector, returns node ID
 *   vdb_search(query, results);          // search, fills results
 *   int n = vdb_count();                 // current node count
 */

#pragma once

#include <stdint.h>

/* Constants */
#define VDB_MAX_NODES    64
#define VDB_TRIT_DIM     48
#define VDB_K            4    /* top-K results per search */

/* Search result — K entries sorted descending by score */
typedef struct {
    uint8_t  ids[VDB_K];
    int32_t  scores[VDB_K];
} vdb_result_t;

/*
 * vdb_init — Initialize the VDB subsystem.
 *
 * Must be called AFTER ulp_lp_core_load_binary() and weight packing,
 * but BEFORE any insert/search/clear. Sets node count to 0.
 */
void vdb_init(void);

/*
 * vdb_insert — Insert a ternary vector into the database.
 *
 * @param trit_vec  Array of VDB_TRIT_DIM int8_t values, each {-1, 0, +1}.
 * @return          Node ID (0..63) on success, -1 if database is full.
 *
 * Packs the trit vector into LP SRAM at the next available node slot.
 * This is a direct SRAM write — the LP core is not involved.
 */
int vdb_insert(const int8_t *trit_vec);

/*
 * vdb_search — Search the database for top-K nearest matches.
 *
 * @param query     Array of VDB_TRIT_DIM int8_t values, each {-1, 0, +1}.
 * @param result    Output: top-K results sorted descending by score.
 * @return          0 on success, -1 on timeout.
 *
 * Packs the query, sends command to LP core, waits for completion
 * (up to 250ms), and reads back the results. The LP core performs
 * brute-force search over all inserted nodes.
 */
int vdb_search(const int8_t *query, vdb_result_t *result);

/*
 * vdb_clear — Remove all nodes from the database.
 *
 * Resets node count to 0. Does not zero the node data in LP SRAM
 * (it will be overwritten by future inserts).
 */
void vdb_clear(void);

/*
 * vdb_count — Return the current number of nodes in the database.
 */
int vdb_count(void);
