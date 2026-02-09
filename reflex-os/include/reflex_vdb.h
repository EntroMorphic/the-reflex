/*
 * reflex_vdb.h — Ternary Vector Database API (NSW Graph)
 *
 * HP-side interface to the LP core's ternary vector database with
 * NSW (Navigable Small World) graph for sub-linear search.
 *
 * The VDB stores up to 64 vectors of 48 trits each, packed as
 * (pos_mask, neg_mask) pairs in LP SRAM. Each node has up to M=7
 * graph neighbors for fast approximate search.
 *
 * Insert: HP packs vector into LP SRAM, LP core builds graph edges
 *   via brute-force candidate selection + top-M + bidirectional edges.
 * Search: LP core performs two-list graph search (ef=16) for N>=8,
 *   falls back to brute-force for N<8.
 *
 * All operations are ternary: {-1, 0, +1}. No floating point.
 * No multiplication. Just AND, popcount, add, sub, negate.
 *
 * Usage:
 *   vdb_init();                          // after LP core binary loaded
 *   vdb_clear();                         // reset to empty
 *   int id = vdb_insert(trit_vec);       // add vector + build edges
 *   vdb_search(query, &result);          // search, fills result
 *   int n = vdb_count();                 // current node count
 *   int v = vdb_last_visit_count();      // nodes visited in last search
 */

#pragma once

#include <stdint.h>

/* Constants */
#define VDB_MAX_NODES    64
#define VDB_TRIT_DIM     48
#define VDB_K            4    /* top-K results per search */
#define VDB_M            7    /* max neighbors per node (NSW graph) */

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
 * vdb_insert — Insert a ternary vector and build graph edges.
 *
 * @param trit_vec  Array of VDB_TRIT_DIM int8_t values, each {-1, 0, +1}.
 * @return          Node ID (0..63) on success, -1 if full, -2 on timeout.
 *
 * Packs the vector into LP SRAM, then dispatches cmd=3 to the LP core
 * which builds NSW graph edges (brute-force candidates + top-M select
 * + bidirectional edges). Waits for LP core completion.
 */
int vdb_insert(const int8_t *trit_vec);

/*
 * vdb_search — Search the database for top-K nearest matches.
 *
 * @param query     Array of VDB_TRIT_DIM int8_t values, each {-1, 0, +1}.
 * @param result    Output: top-K results sorted descending by score.
 * @return          0 on success, -1 on timeout.
 *
 * For N >= 8: two-list graph search (ef=16, follows NSW edges).
 * For N < 8: brute-force scan.
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

/*
 * vdb_last_visit_count — Nodes visited in the last search.
 *
 * For graph search, this is the number of dot products computed.
 * For brute-force, this equals N (all nodes).
 * Useful for verifying graph search is actually sub-linear.
 */
int vdb_last_visit_count(void);
