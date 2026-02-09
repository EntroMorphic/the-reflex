/*
 * LP Core Geometric Processor + Ternary Vector Database (HNSW)
 * Hand-Written RISC-V Assembly
 *
 * Every instruction on this core is specified here. No compiler
 * decisions, no hidden register allocation, no spills.
 *
 * Three modes, selected by lp_command:
 *   CMD 1: CfC neural network step (ternary liquid neural network)
 *   CMD 2: VDB search (graph-aware beam search, brute-force fallback)
 *   CMD 3: VDB graph insert (brute-force candidates, top-M select,
 *          bidirectional edges)
 *
 * All modes use the same INTERSECT primitive:
 *   dot = popcount(a_pos & b_pos) + popcount(a_neg & b_neg)
 *       - popcount(a_pos & b_neg) - popcount(a_neg & b_pos)
 *
 * The CfC computes 32 intersections against weight matrices.
 * The VDB computes intersections against stored vectors — either
 * all N (brute-force/insert) or graph neighbors (beam search).
 * Same AND+popcount core. Same assembly. Different traversal.
 *
 * Runs on ESP32-C6 LP core: RV32IMAC, 16MHz, ~30μA.
 * Called as main() by SDK startup every ~10ms (LP timer wakeup).
 *
 * NOT used: MUL, MULH, DIV, REM — the M extension never executes.
 */

    .text
    .global main
    .type   main, @function

/* ═══════════════════════════════════════════════════════════════
 *  SHARED STATE — LP SRAM globals
 *  These .global symbols are exported as ulp_<name> on HP side.
 * ═══════════════════════════════════════════════════════════════ */
    .bss
    .align 4

/* ── Command dispatch ── */
    .global lp_command
lp_command:     .space 4        /* 0=noop, 1=CfC, 2=VDB search, 3=VDB insert */

/* ═══════════════════════════════════════════════════════════════
 *  CfC STATE — Ternary liquid neural network
 * ═══════════════════════════════════════════════════════════════ */
    .global gie_hidden
gie_hidden:     .space 32       /* 32 × int8_t: GIE hidden state */

    .global lp_hidden
lp_hidden:      .space 16       /* 16 × int8_t: LP hidden state */

    .global lp_step_count
lp_step_count:  .space 4        /* uint32_t: total CfC steps completed */

    .global lp_decision
lp_decision:    .space 4        /* int8_t (padded to 4): aggregate decision */

    .global lp_W_f_pos
lp_W_f_pos:     .space 192      /* 16 neurons × 3 words × 4 bytes */

    .global lp_W_f_neg
lp_W_f_neg:     .space 192      /* 16 neurons × 3 words × 4 bytes */

    .global lp_W_g_pos
lp_W_g_pos:     .space 192      /* 16 neurons × 3 words × 4 bytes */

    .global lp_W_g_neg
lp_W_g_neg:     .space 192      /* 16 neurons × 3 words × 4 bytes */

    .global lp_dots_f
lp_dots_f:      .space 64       /* 16 × int32_t: f-pathway dots */

    .global lp_dots_g
lp_dots_g:      .space 64       /* 16 × int32_t: g-pathway dots */

    .global lp_data_ready
lp_data_ready:  .space 4        /* uint32_t: sync flag (for CfC mode) */

/* ═══════════════════════════════════════════════════════════════
 *  VDB STATE — Ternary Vector Database with NSW Graph
 *
 *  48-trit vectors packed as (pos_mask[3], neg_mask[3])
 *  = 6 words = 24 bytes per vector, + 8 bytes graph metadata.
 *
 *  Node layout (32 bytes):
 *    [0..11]   pos_mask[3]      (3 × uint32_t)
 *    [12..23]  neg_mask[3]      (3 × uint32_t)
 *    [24..30]  neighbors[7]     (7 × uint8_t neighbor IDs)
 *    [31]      neighbor_count   (uint8_t, 0..7)
 *
 *  64 nodes × 32 bytes = 2,048 bytes
 *
 *  M=7 neighbors per node (NSW flat graph, no layers).
 *  Insert: brute-force candidates → top-M select → bidir edges.
 *  Search: two-list graph search (ef=16) with brute-force fallback for N<8.
 * ═══════════════════════════════════════════════════════════════ */

#define VDB_MAX_NODES   64
#define VDB_NODE_BYTES  32      /* 24B vector + 8B graph metadata */
#define VDB_VEC_BYTES   24      /* just the vector portion */
#define VDB_K_MAX       4       /* max results returned */
#define VDB_M           7       /* max neighbors per node */
#define VDB_NEIGH_OFF   24      /* offset to neighbors[] within node */
#define VDB_NCNT_OFF    31      /* offset to neighbor_count within node */

    .global vdb_node_count
vdb_node_count: .space 4        /* uint32_t: number of stored vectors */

    .global vdb_nodes
vdb_nodes:      .space (VDB_MAX_NODES * VDB_NODE_BYTES)  /* 2048 bytes */

    .global vdb_query_pos
vdb_query_pos:  .space 12       /* 3 × uint32_t: query positive mask */

    .global vdb_query_neg
vdb_query_neg:  .space 12       /* 3 × uint32_t: query negative mask */

    .global vdb_result_ids
vdb_result_ids: .space VDB_K_MAX   /* uint8_t[4]: best match IDs */

    .global vdb_result_scores
vdb_result_scores: .space (VDB_K_MAX * 4)  /* int32_t[4]: best match scores */

    .global vdb_search_count
vdb_search_count: .space 4      /* uint32_t: total searches completed */

    .global vdb_insert_id
vdb_insert_id:  .space 4        /* uint32_t: node index to insert (set by HP) */

    .global vdb_insert_count
vdb_insert_count: .space 4      /* uint32_t: total inserts completed */

    .global vdb_visit_count
vdb_visit_count: .space 4       /* uint32_t: nodes visited in last search */


/* ═══════════════════════════════════════════════════════════════
 *  POPCOUNT LUT — 256 bytes, read-only
 * ═══════════════════════════════════════════════════════════════ */
    .section .rodata
    .align 4
popcount_lut:
    .byte 0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4
    .byte 1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5
    .byte 1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5
    .byte 2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6
    .byte 1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5
    .byte 2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6
    .byte 2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6
    .byte 3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7
    .byte 1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5
    .byte 2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6
    .byte 2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6
    .byte 3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7
    .byte 2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6
    .byte 3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7
    .byte 3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7
    .byte 4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8


/* ═══════════════════════════════════════════════════════════════
 *  POPCOUNT MACRO — inline, no function call
 *
 *  Input:  \src  = 32-bit word
 *  Output: \dst  = popcount (0..32)
 *  Clobbers: \tmp1, \tmp2
 *  Uses:   s8    = &popcount_lut (must be set up before use)
 * ═══════════════════════════════════════════════════════════════ */
.macro POPCOUNT dst, src, tmp1, tmp2
    andi    \tmp1, \src, 0xFF
    add     \tmp1, s8, \tmp1
    lbu     \dst, 0(\tmp1)

    srli    \tmp1, \src, 8
    andi    \tmp1, \tmp1, 0xFF
    add     \tmp1, s8, \tmp1
    lbu     \tmp2, 0(\tmp1)
    add     \dst, \dst, \tmp2

    srli    \tmp1, \src, 16
    andi    \tmp1, \tmp1, 0xFF
    add     \tmp1, s8, \tmp1
    lbu     \tmp2, 0(\tmp1)
    add     \dst, \dst, \tmp2

    srli    \tmp1, \src, 24
    add     \tmp1, s8, \tmp1
    lbu     \tmp2, 0(\tmp1)
    add     \dst, \dst, \tmp2
.endm


/* ═══════════════════════════════════════════════════════════════
 *  INTERSECT MACRO — fully unrolled ternary dot product
 *
 *  Inputs:
 *    \abase  = base address of vector A pos masks
 *    \anbase = base address of vector A neg masks
 *    s6      = &b_pos[0]  (vector B pos masks)
 *    s7      = &b_neg[0]  (vector B neg masks)
 *    s8      = &popcount_lut
 *
 *  Output: \dot_out = signed dot product
 *  Clobbers: a0-a7, t0-t2
 * ═══════════════════════════════════════════════════════════════ */
.macro INTERSECT dot_out, abase, anbase
    li      a6, 0                       /* agree = 0 */
    li      a7, 0                       /* disagree = 0 */

    /* ── Word 0 ── */
    lw      a0, 0(\abase)
    lw      a1, 0(\anbase)
    lw      a2, 0(s6)
    lw      a3, 0(s7)

    and     a4, a0, a2
    POPCOUNT t0, a4, t1, t2
    add     a6, a6, t0

    and     a4, a1, a3
    POPCOUNT t0, a4, t1, t2
    add     a6, a6, t0

    and     a4, a0, a3
    POPCOUNT t0, a4, t1, t2
    add     a7, a7, t0

    and     a4, a1, a2
    POPCOUNT t0, a4, t1, t2
    add     a7, a7, t0

    /* ── Word 1 ── */
    lw      a0, 4(\abase)
    lw      a1, 4(\anbase)
    lw      a2, 4(s6)
    lw      a3, 4(s7)

    and     a4, a0, a2
    POPCOUNT t0, a4, t1, t2
    add     a6, a6, t0

    and     a4, a1, a3
    POPCOUNT t0, a4, t1, t2
    add     a6, a6, t0

    and     a4, a0, a3
    POPCOUNT t0, a4, t1, t2
    add     a7, a7, t0

    and     a4, a1, a2
    POPCOUNT t0, a4, t1, t2
    add     a7, a7, t0

    /* ── Word 2 ── */
    lw      a0, 8(\abase)
    lw      a1, 8(\anbase)
    lw      a2, 8(s6)
    lw      a3, 8(s7)

    and     a4, a0, a2
    POPCOUNT t0, a4, t1, t2
    add     a6, a6, t0

    and     a4, a1, a3
    POPCOUNT t0, a4, t1, t2
    add     a6, a6, t0

    and     a4, a0, a3
    POPCOUNT t0, a4, t1, t2
    add     a7, a7, t0

    and     a4, a1, a2
    POPCOUNT t0, a4, t1, t2
    add     a7, a7, t0

    sub     \dot_out, a6, a7
.endm


/* ═══════════════════════════════════════════════════════════════
 *  INTERSECT_LOOP — looped version for VDB search/insert
 *
 *  Same math as INTERSECT but uses a loop over 3 words instead
 *  of full unrolling. Saves code space (~200 bytes per use vs
 *  ~600 for the unrolled version). Slightly slower but called
 *  once per node, and VDB operations are not latency-critical.
 *
 *  Inputs:
 *    s2 = &a_pos[0]  (node pos masks)
 *    s3 = &a_neg[0]  (node neg masks, = s2 + 12)
 *    s6 = &b_pos[0]  (query pos masks)
 *    s7 = &b_neg[0]  (query neg masks)
 *    s8 = &popcount_lut
 *
 *  Output: s4 = signed dot product
 *  Clobbers: a0-a7, t0-t5, s4, s5
 * ═══════════════════════════════════════════════════════════════ */
.macro INTERSECT_LOOP
    li      s4, 0                       /* agree = 0 */
    li      s5, 0                       /* disagree = 0 */
    li      t5, 0                       /* word offset = 0 */
    li      t3, 12                      /* end = 3 words × 4 bytes */
1:
    add     t4, s2, t5                  /* &a_pos[w] */
    lw      a0, 0(t4)
    add     t4, s3, t5                  /* &a_neg[w] */
    lw      a1, 0(t4)
    add     t4, s6, t5                  /* &b_pos[w] */
    lw      a2, 0(t4)
    add     t4, s7, t5                  /* &b_neg[w] */
    lw      a3, 0(t4)

    and     a4, a0, a2                  /* pos & pos */
    POPCOUNT a5, a4, t1, t2
    add     s4, s4, a5

    and     a4, a1, a3                  /* neg & neg */
    POPCOUNT a5, a4, t1, t2
    add     s4, s4, a5

    and     a4, a0, a3                  /* pos & neg */
    POPCOUNT a5, a4, t1, t2
    add     s5, s5, a5

    and     a4, a1, a2                  /* neg & pos */
    POPCOUNT a5, a4, t1, t2
    add     s5, s5, a5

    addi    t5, t5, 4
    bne     t5, t3, 1b

    sub     s4, s4, s5                  /* dot = agree - disagree */
.endm


/* ═══════════════════════════════════════════════════════════════
 *  DOT_NODE — compute dot(query, node[index])
 *
 *  Input:  a register holding the node index (0..63)
 *  Sets s2, s3 to node's pos/neg base, runs INTERSECT_LOOP.
 *  Result in s4.
 *
 *  Expects:
 *    s6 = &query_pos, s7 = &query_neg, s8 = &popcount_lut
 *    s11 = &vdb_nodes (base of node array)
 *
 *  Clobbers: s2, s3, s4, s5, a0-a7, t0-t5
 * ═══════════════════════════════════════════════════════════════ */
.macro DOT_NODE idx_reg
    slli    t0, \idx_reg, 5             /* t0 = idx * 32 */
    add     s2, s11, t0                 /* s2 = &node[idx].pos */
    addi    s3, s2, 12                  /* s3 = &node[idx].neg */
    INTERSECT_LOOP
.endm


/* ═══════════════════════════════════════════════════════════════
 *  DOT_TWO_NODES — compute dot(node[A], node[B])
 *
 *  Computes the dot product between two stored nodes' vectors.
 *  Used for diversity selection during insert.
 *
 *  Input:  \areg = node index A, \breg = node index B
 *  Sets up s6/s7 from node B, s2/s3 from node A, runs INTERSECT_LOOP.
 *  Result in s4.
 *
 *  Expects: s8 = &popcount_lut, s11 = &vdb_nodes
 *  Clobbers: s2, s3, s4, s5, s6, s7, a0-a7, t0-t5
 *  WARNING: Destroys s6/s7 (query pointers). Caller must restore.
 * ═══════════════════════════════════════════════════════════════ */
.macro DOT_TWO_NODES areg, breg
    slli    t0, \breg, 5               /* t0 = B * 32 */
    add     s6, s11, t0                /* s6 = &node[B].pos */
    addi    s7, s6, 12                 /* s7 = &node[B].neg */
    slli    t0, \areg, 5               /* t0 = A * 32 */
    add     s2, s11, t0                /* s2 = &node[A].pos */
    addi    s3, s2, 12                 /* s3 = &node[A].neg */
    INTERSECT_LOOP
.endm


/* ═══════════════════════════════════════════════════════════════
 *  main() — Command Dispatch
 *
 *  Called by SDK lp_core_startup(). Returns 0 to a0.
 *  SDK handles LP timer re-arm and halt after we return.
 *
 *  lp_command:
 *    0 = no-op (return immediately)
 *    1 = CfC step (read gie_hidden, compute, write lp_hidden)
 *    2 = VDB search (graph beam search or brute-force)
 *    3 = VDB insert (build graph edges for new node)
 * ═══════════════════════════════════════════════════════════════ */
    .text
    .align 2
main:
    /* ── Read and clear command ── */
    la      t0, lp_command
    lw      t1, 0(t0)
    beqz    t1, .Learly_exit
    sw      zero, 0(t0)                 /* clear command */

    /* ── Dispatch ── */
    li      t2, 1
    beq     t1, t2, .Lcfc_entry
    li      t2, 2
    beq     t1, t2, .Lvdb_entry
    li      t2, 3
    beq     t1, t2, .Lvdb_insert_entry
    /* Unknown command — ignore */
    j       .Learly_exit


/* ═══════════════════════════════════════════════════════════════
 *  CfC MODE — Ternary Liquid Neural Network Step
 *
 *  Reads gie_hidden[32] + lp_hidden[16], packs to 48 trits,
 *  computes 16×(INTERSECT, PROJECT, GATE), writes lp_hidden.
 * ═══════════════════════════════════════════════════════════════ */
.Lcfc_entry:
    /* ── Save callee-saved registers ── */
    addi    sp, sp, -96
    sw      ra,  40(sp)
    sw      s0,  48(sp)
    sw      s1,  52(sp)
    sw      s2,  56(sp)
    sw      s3,  60(sp)
    sw      s4,  64(sp)
    sw      s5,  68(sp)
    sw      s6,  72(sp)
    sw      s7,  76(sp)
    sw      s8,  80(sp)
    sw      s9,  84(sp)
    sw      s10, 88(sp)
    sw      s11, 92(sp)

    /* ── Load base addresses ── */
    la      s8, popcount_lut
    la      s9, lp_hidden
    addi    s6, sp, 0                   /* &x_pos[0] */
    addi    s7, sp, 12                  /* &x_neg[0] */
    addi    s10, sp, 24                 /* &h_new[0] */

    /* ── Pack word 0: gie_hidden[0..15] ── */
    la      t3, gie_hidden
    li      a0, 0
    li      a1, 0
    li      t4, 1

    .set i, 0
    .rept 16
    lb      t0, i(t3)
    blez    t0, 1f
    or      a0, a0, t4
    j       2f
1:  bgez    t0, 2f
    or      a1, a1, t4
2:  slli    t4, t4, 1
    .set i, i+1
    .endr

    sw      a0, 0(s6)
    sw      a1, 0(s7)

    /* ── Pack word 1: gie_hidden[16..31] ── */
    addi    t3, t3, 16
    li      a0, 0
    li      a1, 0
    li      t4, 1

    .set i, 0
    .rept 16
    lb      t0, i(t3)
    blez    t0, 1f
    or      a0, a0, t4
    j       2f
1:  bgez    t0, 2f
    or      a1, a1, t4
2:  slli    t4, t4, 1
    .set i, i+1
    .endr

    sw      a0, 4(s6)
    sw      a1, 4(s7)

    /* ── Pack word 2: lp_hidden[0..15] ── */
    mv      t3, s9
    li      a0, 0
    li      a1, 0
    li      t4, 1

    .set i, 0
    .rept 16
    lb      t0, i(t3)
    blez    t0, 1f
    or      a0, a0, t4
    j       2f
1:  bgez    t0, 2f
    or      a1, a1, t4
2:  slli    t4, t4, 1
    .set i, i+1
    .endr

    sw      a0, 8(s6)
    sw      a1, 8(s7)


    /* ── CfC neuron loop: 16 neurons × (INTERSECT, PROJECT, GATE) ── */
    la      s2, lp_W_f_pos
    la      s3, lp_W_f_neg
    la      s4, lp_W_g_pos
    la      s5, lp_W_g_neg
    la      s11, lp_dots_f

    li      s0, 0                       /* neuron counter */
    li      s1, 0                       /* weight offset */

.Lneuron_loop:
    add     t5, s2, s1
    add     t6, s3, s1
    INTERSECT t3, t5, t6                /* dot_f */

    slli    t5, s0, 2
    add     t5, s11, t5
    sw      t3, 0(t5)

    add     t5, s4, s1
    add     t6, s5, s1
    INTERSECT t4, t5, t6                /* dot_g */

    la      t5, lp_dots_g
    slli    t6, s0, 2
    add     t5, t5, t6
    sw      t4, 0(t5)

    /* PROJECT */
    li      a0, 0
    bgtz    t3, .Lf_pos
    bltz    t3, .Lf_neg
    j       .Lf_done
.Lf_pos:
    li      a0, 1
    j       .Lf_done
.Lf_neg:
    li      a0, -1
.Lf_done:

    li      a1, 0
    bgtz    t4, .Lg_pos
    bltz    t4, .Lg_neg
    j       .Lg_done
.Lg_pos:
    li      a1, 1
    j       .Lg_done
.Lg_neg:
    li      a1, -1
.Lg_done:

    /* GATE */
    add     t0, s9, s0
    lb      t0, 0(t0)

    beqz    a0, .Lgate_hold
    bgtz    a0, .Lgate_update
    neg     a1, a1
.Lgate_update:
    mv      t0, a1
.Lgate_hold:
    add     t1, s10, s0
    sb      t0, 0(t1)

    addi    s0, s0, 1
    addi    s1, s1, 12
    li      t0, 16
    bne     s0, t0, .Lneuron_loop

    /* ── Commit h_new → lp_hidden ── */
    li      t0, 0
.Lcommit_loop:
    add     t1, s10, t0
    lb      t2, 0(t1)
    add     t1, s9, t0
    sb      t2, 0(t1)
    addi    t0, t0, 1
    li      t1, 16
    bne     t0, t1, .Lcommit_loop

    /* ── Decision: majority vote ── */
    li      a0, 0
    li      t0, 0
.Lvote_loop:
    add     t1, s9, t0
    lb      t2, 0(t1)
    add     a0, a0, t2
    addi    t0, t0, 1
    li      t1, 16
    bne     t0, t1, .Lvote_loop

    li      t0, 0
    bgtz    a0, .Ldec_pos
    bltz    a0, .Ldec_neg
    j       .Ldec_done
.Ldec_pos:
    li      t0, 1
    j       .Ldec_done
.Ldec_neg:
    li      t0, -1
.Ldec_done:
    la      t1, lp_decision
    sw      t0, 0(t1)

    /* ── Increment step counter ── */
    la      t0, lp_step_count
    lw      t1, 0(t0)
    addi    t1, t1, 1
    sw      t1, 0(t0)

    j       .Lrestore_and_exit


/* ═══════════════════════════════════════════════════════════════
 *  VDB SEARCH — Two-List Graph Search (LCVDB-style)
 *
 *  If N < 8: brute-force scan all nodes (graph too sparse).
 *  If N >= 8: two-list graph search with candidate/result separation.
 *
 *  Algorithm (adapted from LCVDB):
 *    1. Seed candidate list and result list with entry point (node 0).
 *    2. Main loop: pick best unprocessed candidate (linear scan).
 *    3. Early termination: if best candidate <= worst result, stop.
 *    4. Expand: for each neighbor of best candidate, if unvisited:
 *       - Mark visited, compute dot.
 *       - Add to candidate list (if room).
 *       - Insert into result list (sorted descending, insertion sort).
 *    5. Return top-4 from result list.
 *
 *  Two lists:
 *    Candidate list: unsorted, up to CAND_MAX=32 entries.
 *      cand_cursor advances; we find best via linear scan.
 *    Result list: sorted descending, up to EF_SEARCH=8 entries.
 *      Insertion sort (bubble up). Top-4 returned as answer.
 *
 *  Stack layout (608 bytes):
 *    [sp+0..51]     saved callee-saved regs (13 × 4)
 *    [sp+52..55]    padding
 *    [sp+56..183]   res_scores[32]   (32 × int32, sorted descending)
 *    [sp+184..215]  res_ids[32]      (32 × uint8)
 *    [sp+216..219]  res_count        (uint32)
 *    [sp+220..223]  visited_lo       (uint32: nodes 0..31)
 *    [sp+224..227]  visited_hi       (uint32: nodes 32..63)
 *    [sp+228..231]  visit_counter    (uint32)
 *    [sp+232..487]  cand_scores[64]  (64 × int32)
 *    [sp+488..551]  cand_ids[64]     (64 × uint8)
 *    [sp+552..555]  cand_count       (uint32)
 *    [sp+556..559]  cand_cursor      (uint32)
 *    [sp+560..599]  scratch (10 words)
 *    [sp+600..607]  padding
 * ═══════════════════════════════════════════════════════════════ */

#define TOPK          4
#define EF_SEARCH     32
#define CAND_MAX      64

#define RES_SCORES    56        /* 32 × int32 = 128B */
#define RES_IDS       184       /* 32 × uint8 = 32B */
#define RES_COUNT     216       /* uint32 */
#define VIS_LO        220       /* uint32 */
#define VIS_HI        224       /* uint32 */
#define VISCNT        228       /* uint32 */
#define CAND_SCORES   232       /* 64 × int32 = 256B */
#define CAND_IDS      488       /* 64 × uint8 = 64B */
#define CAND_COUNT    552       /* uint32 */
#define CAND_CURSOR   556       /* uint32 */
#define SCRATCH       560       /* 10 × 4 = 40B */

.Lvdb_entry:
    /* ── Save callee-saved registers ── */
    addi    sp, sp, -608
    sw      ra,  0(sp)
    sw      s0,  4(sp)
    sw      s1,  8(sp)
    sw      s2, 12(sp)
    sw      s3, 16(sp)
    sw      s4, 20(sp)
    sw      s5, 24(sp)
    sw      s6, 28(sp)
    sw      s7, 32(sp)
    sw      s8, 36(sp)
    sw      s9, 40(sp)
    sw      s10, 44(sp)
    sw      s11, 48(sp)

    /* ── Load base addresses ── */
    la      s8, popcount_lut
    la      s6, vdb_query_pos
    la      s7, vdb_query_neg
    la      s11, vdb_nodes

    /* ── Load node count, bail if empty ── */
    la      t0, vdb_node_count
    lw      s1, 0(t0)
    beqz    s1, .Lvdb_no_results

    /* ── If N < 8, use brute-force ── */
    li      t0, 8
    blt     s1, t0, .Lvdb_brute_force

    /* ══════════════════════════════════════════
     *  TWO-LIST GRAPH SEARCH (N >= 8)
     * ══════════════════════════════════════════ */

    /* Zero visited bitset and counters */
    sw      zero, VIS_LO(sp)
    sw      zero, VIS_HI(sp)
    sw      zero, VISCNT(sp)
    sw      zero, RES_COUNT(sp)
    sw      zero, CAND_COUNT(sp)
    sw      zero, CAND_CURSOR(sp)

    /* Initialize result scores to INT32_MIN (EF_SEARCH slots) */
    lui     t0, 0x80000
    li      t1, 0
    addi    t2, sp, RES_SCORES
.Lgs_init_res_scores:
    sw      t0, 0(t2)
    addi    t2, t2, 4
    addi    t1, t1, 1
    li      t3, EF_SEARCH
    blt     t1, t3, .Lgs_init_res_scores

    li      t0, 0xFF
    li      t1, 0
    addi    t2, sp, RES_IDS
.Lgs_init_res_ids:
    sb      t0, 0(t2)
    addi    t2, t2, 1
    addi    t1, t1, 1
    li      t3, EF_SEARCH
    blt     t1, t3, .Lgs_init_res_ids

    /* ── Seed with two entry points (node 0 and node N/2) ──
     * Using two entry points ensures the search explores different
     * regions of the graph, catching nodes that might be unreachable
     * from a single starting point for a given query. */

    /* Entry point 1: node 0 */
    li      s0, 0
    DOT_NODE s0
    /* s4 = dot(query, node[0]) */

    /* Mark node 0 visited */
    li      t0, 1
    sw      t0, VIS_LO(sp)
    li      t0, 1
    sw      t0, VISCNT(sp)

    /* Add node 0 to candidate list */
    sw      s4, CAND_SCORES+0(sp)      /* cand_scores[0] */
    sb      s0, CAND_IDS+0(sp)         /* cand_ids[0] */

    /* Add node 0 to result list */
    sw      s4, RES_SCORES+0(sp)       /* res_scores[0] */
    sb      s0, RES_IDS+0(sp)          /* res_ids[0] */

    /* Entry point 2: node N/2 */
    srli    s0, s1, 1                  /* s0 = N/2 */
    DOT_NODE s0
    /* s4 = dot(query, node[N/2]) */

    /* Mark node N/2 visited */
    li      t0, 32
    bge     s0, t0, .Lgs_seed2_hi
    /* Node N/2 < 32: set bit in VIS_LO */
    lw      t0, VIS_LO(sp)
    li      t1, 1
    sll     t1, t1, s0
    or      t0, t0, t1
    sw      t0, VIS_LO(sp)
    j       .Lgs_seed2_done
.Lgs_seed2_hi:
    /* Node N/2 >= 32: set bit in VIS_HI */
    addi    t2, s0, -32
    li      t1, 1
    sll     t1, t1, t2
    sw      t1, VIS_HI(sp)
.Lgs_seed2_done:

    /* visit count = 2 */
    li      t0, 2
    sw      t0, VISCNT(sp)

    /* Add to candidate list at slot 1 */
    sw      s4, CAND_SCORES+4(sp)      /* cand_scores[1] */
    sb      s0, CAND_IDS+1(sp)         /* cand_ids[1] */
    li      t0, 2
    sw      t0, CAND_COUNT(sp)

    /* Insert into result list (slot 0 or 1 depending on score) */
    lw      t0, RES_SCORES+0(sp)       /* score of node 0 */
    ble     s4, t0, .Lgs_seed2_after_ep0
    /* Node N/2 score > node 0 score: insert at [0], push node 0 to [1] */
    lbu     t1, RES_IDS+0(sp)
    sw      t0, RES_SCORES+4(sp)       /* move node 0 score to [1] */
    sb      t1, RES_IDS+1(sp)
    sw      s4, RES_SCORES+0(sp)       /* node N/2 at [0] */
    sb      s0, RES_IDS+0(sp)
    j       .Lgs_seed2_res_done
.Lgs_seed2_after_ep0:
    /* Node N/2 score <= node 0 score: insert at [1] */
    sw      s4, RES_SCORES+4(sp)
    sb      s0, RES_IDS+1(sp)
.Lgs_seed2_res_done:
    li      t0, 2
    sw      t0, RES_COUNT(sp)

    /* ── Main expansion loop ── */
.Lgs_main_loop:
    lw      s0, CAND_CURSOR(sp)
    lw      s10, CAND_COUNT(sp)
    bge     s0, s10, .Lgs_done         /* no more candidates */

    /* ── Find best unprocessed candidate (linear scan) ── */
    lui     t3, 0x80000                /* best_score = INT32_MIN */
    li      t4, -1                     /* best_idx = -1 */
    mv      t5, s0                     /* scan from cursor */

.Lgs_find_best:
    bge     t5, s10, .Lgs_found_best
    slli    t0, t5, 2
    addi    t1, sp, CAND_SCORES
    add     t0, t1, t0
    lw      t1, 0(t0)                 /* t1 = cand_scores[t5] */
    ble     t1, t3, .Lgs_find_next
    mv      t3, t1                     /* best_score = t1 */
    mv      t4, t5                     /* best_idx = t5 */
.Lgs_find_next:
    addi    t5, t5, 1
    j       .Lgs_find_best

.Lgs_found_best:
    bltz    t4, .Lgs_done              /* no valid candidate */

    /* ── Early termination ──
     * If best candidate score <= worst result score, stop.
     * Worst result = res_scores[res_count - 1] (if full) or INT32_MIN. */
    lw      a5, RES_COUNT(sp)
    li      t0, EF_SEARCH
    blt     a5, t0, .Lgs_no_early_term /* result not full, keep going */
    /* Result is full — check worst */
    addi    t0, a5, -1
    slli    t0, t0, 2
    addi    t1, sp, RES_SCORES
    add     t0, t1, t0
    lw      t0, 0(t0)                 /* t0 = res_scores[res_count-1] */
    ble     t3, t0, .Lgs_done         /* best_cand <= worst_result, stop */
.Lgs_no_early_term:

    /* ── Swap best candidate to cursor position ── */
    /* Swap scores */
    slli    t0, t4, 2
    addi    t1, sp, CAND_SCORES
    add     t0, t1, t0                 /* &cand_scores[best_idx] */
    slli    t1, s0, 2
    addi    t2, sp, CAND_SCORES
    add     t1, t2, t1                 /* &cand_scores[cursor] */
    lw      t5, 0(t0)
    lw      t6, 0(t1)
    sw      t5, 0(t1)
    sw      t6, 0(t0)
    /* Swap IDs */
    addi    t0, sp, CAND_IDS
    add     t5, t0, t4
    add     t6, t0, s0
    lbu     a5, 0(t5)
    lbu     a6, 0(t6)
    sb      a5, 0(t6)
    sb      a6, 0(t5)

    /* Now cand[cursor] is the best. a5 still = best node ID, but reload: */
    addi    t0, sp, CAND_IDS
    add     t0, t0, s0
    lbu     s9, 0(t0)                  /* s9 = best candidate node ID */

    /* Advance cursor */
    addi    s0, s0, 1
    sw      s0, CAND_CURSOR(sp)

    /* ── Expand neighbors of s9 ── */
    slli    t0, s9, 5
    add     t0, s11, t0               /* &node[s9] */
    lbu     t4, VDB_NCNT_OFF(t0)      /* t4 = neighbor_count */
    beqz    t4, .Lgs_main_loop        /* no neighbors */
    addi    t5, t0, VDB_NEIGH_OFF     /* t5 = &neighbors[0] */

    li      a6, 0                      /* neighbor index */

.Lgs_expand_neigh:
    add     t0, t5, a6
    lbu     a7, 0(t0)                 /* a7 = neighbor ID */

    /* Check visited */
    li      t0, 32
    bge     a7, t0, .Lgs_vis_hi
    lw      t0, VIS_LO(sp)
    li      t1, 1
    sll     t1, t1, a7
    and     t2, t0, t1
    bnez    t2, .Lgs_neigh_next
    or      t0, t0, t1
    sw      t0, VIS_LO(sp)
    j       .Lgs_vis_done
.Lgs_vis_hi:
    lw      t0, VIS_HI(sp)
    addi    t2, a7, -32
    li      t1, 1
    sll     t1, t1, t2
    and     t2, t0, t1
    bnez    t2, .Lgs_neigh_next
    or      t0, t0, t1
    sw      t0, VIS_HI(sp)
.Lgs_vis_done:

    /* Increment visit counter */
    lw      t0, VISCNT(sp)
    addi    t0, t0, 1
    sw      t0, VISCNT(sp)

    /* Save state before DOT_NODE */
    sw      t5, SCRATCH+0(sp)
    sw      t4, SCRATCH+4(sp)
    sw      a6, SCRATCH+8(sp)
    sw      a7, SCRATCH+12(sp)

    DOT_NODE a7
    /* s4 = dot(query, neighbor) */

    /* Restore state */
    lw      t5, SCRATCH+0(sp)
    lw      t4, SCRATCH+4(sp)
    lw      a6, SCRATCH+8(sp)
    lw      a7, SCRATCH+12(sp)

    /* ── Add to candidate list (if room) ── */
    lw      t0, CAND_COUNT(sp)
    li      t1, CAND_MAX
    bge     t0, t1, .Lgs_skip_cand    /* full */
    slli    t1, t0, 2
    addi    t2, sp, CAND_SCORES
    add     t1, t2, t1
    sw      s4, 0(t1)                 /* cand_scores[count] = dot */
    addi    t1, sp, CAND_IDS
    add     t1, t1, t0
    sb      a7, 0(t1)                 /* cand_ids[count] = neighbor */
    addi    t0, t0, 1
    sw      t0, CAND_COUNT(sp)
.Lgs_skip_cand:

    /* ── Insert into result list (sorted descending, insertion sort) ── */
    lw      a5, RES_COUNT(sp)          /* a5 = res_count */

    /* If result list not full, append at end and bubble up */
    li      t0, EF_SEARCH
    bge     a5, t0, .Lgs_res_full

    /* Append at res_count */
    slli    t0, a5, 2
    addi    t1, sp, RES_SCORES
    add     t0, t1, t0
    sw      s4, 0(t0)                 /* res_scores[count] = dot */
    addi    t0, sp, RES_IDS
    add     t0, t0, a5
    sb      a7, 0(t0)                 /* res_ids[count] = neighbor */
    addi    a5, a5, 1
    sw      a5, RES_COUNT(sp)
    /* Bubble up from position a5-1 */
    addi    s10, a5, -1               /* s10 = pos to bubble */
    j       .Lgs_res_bubble

.Lgs_res_full:
    /* Full — check if better than worst (last element) */
    addi    t0, a5, -1
    slli    t1, t0, 2
    addi    t2, sp, RES_SCORES
    add     t1, t2, t1
    lw      t2, 0(t1)                 /* t2 = worst result score */
    ble     s4, t2, .Lgs_neigh_next   /* not better, skip */
    /* Replace worst and bubble up */
    sw      s4, 0(t1)
    addi    t1, sp, RES_IDS
    add     t1, t1, t0
    sb      a7, 0(t1)
    mv      s10, t0                    /* s10 = pos to bubble */

.Lgs_res_bubble:
    /* Bubble position s10 upward while score > predecessor */
    beqz    s10, .Lgs_neigh_next      /* already at top */

    addi    t0, s10, -1               /* predecessor index */
    slli    t1, s10, 2
    addi    t2, sp, RES_SCORES
    add     t1, t2, t1                /* &res_scores[pos] */
    slli    t3, t0, 2
    add     t3, t2, t3                /* &res_scores[pos-1] */
    lw      a5, 0(t1)                 /* score at pos */
    lw      t6, 0(t3)                 /* score at pos-1 */
    ble     a5, t6, .Lgs_neigh_next   /* in order, done */

    /* Swap scores */
    sw      t6, 0(t1)
    sw      a5, 0(t3)
    /* Swap IDs */
    addi    t1, sp, RES_IDS
    add     t2, t1, s10
    add     t3, t1, t0
    lbu     a5, 0(t2)
    lbu     t6, 0(t3)
    sb      t6, 0(t2)
    sb      a5, 0(t3)

    addi    s10, s10, -1
    j       .Lgs_res_bubble

.Lgs_neigh_next:
    addi    a6, a6, 1
    blt     a6, t4, .Lgs_expand_neigh

    j       .Lgs_main_loop

.Lgs_done:
    /* Write visit count */
    la      t0, vdb_visit_count
    lw      t1, VISCNT(sp)
    sw      t1, 0(t0)

    /* Copy top-4 from result list to output */
    la      t0, vdb_result_ids
    lbu     t1, RES_IDS+0(sp)
    sb      t1, 0(t0)
    lbu     t1, RES_IDS+1(sp)
    sb      t1, 1(t0)
    lbu     t1, RES_IDS+2(sp)
    sb      t1, 2(t0)
    lbu     t1, RES_IDS+3(sp)
    sb      t1, 3(t0)

    la      t0, vdb_result_scores
    lw      t1, RES_SCORES+0(sp)
    sw      t1, 0(t0)
    lw      t1, RES_SCORES+4(sp)
    sw      t1, 4(t0)
    lw      t1, RES_SCORES+8(sp)
    sw      t1, 8(t0)
    lw      t1, RES_SCORES+12(sp)
    sw      t1, 12(t0)

    j       .Lvdb_done

    /* ══════════════════════════════════════════
     *  BRUTE-FORCE SEARCH (N < 8)
     *
     *  Same as original M3 code — scan all nodes.
     * ══════════════════════════════════════════ */
.Lvdb_brute_force:
    /* Initialize top-K: all scores = INT32_MIN, ids = 0xFF */
    lui     t0, 0x80000
    sw      t0, RES_SCORES+0(sp)
    sw      t0, RES_SCORES+4(sp)
    sw      t0, RES_SCORES+8(sp)
    sw      t0, RES_SCORES+12(sp)
    li      t0, 0xFF
    sb      t0, RES_IDS+0(sp)
    sb      t0, RES_IDS+1(sp)
    sb      t0, RES_IDS+2(sp)
    sb      t0, RES_IDS+3(sp)

    mv      s2, s11                     /* &nodes[0].pos */
    li      s0, 0                       /* node index */

.Lvdb_bf_loop:
    addi    s3, s2, 12                  /* &node.neg */
    INTERSECT_LOOP

    /* Insert into top-K if better than worst (slot 3) */
    lw      t0, RES_SCORES+12(sp)
    ble     s4, t0, .Lvdb_bf_not_topk
    sw      s4, RES_SCORES+12(sp)
    sb      s0, RES_IDS+3(sp)

    /* Bubble up [3] vs [2] */
    lw      t0, RES_SCORES+8(sp)
    ble     s4, t0, .Lvdb_bf_not_topk
    sw      t0, RES_SCORES+12(sp)
    sw      s4, RES_SCORES+8(sp)
    lbu     t0, RES_IDS+2(sp)
    lbu     t1, RES_IDS+3(sp)
    sb      t1, RES_IDS+2(sp)
    sb      t0, RES_IDS+3(sp)

    /* Bubble up [2] vs [1] */
    lw      t0, RES_SCORES+4(sp)
    ble     s4, t0, .Lvdb_bf_not_topk
    sw      t0, RES_SCORES+8(sp)
    sw      s4, RES_SCORES+4(sp)
    lbu     t0, RES_IDS+1(sp)
    lbu     t1, RES_IDS+2(sp)
    sb      t1, RES_IDS+1(sp)
    sb      t0, RES_IDS+2(sp)

    /* Bubble up [1] vs [0] */
    lw      t0, RES_SCORES+0(sp)
    ble     s4, t0, .Lvdb_bf_not_topk
    sw      t0, RES_SCORES+4(sp)
    sw      s4, RES_SCORES+0(sp)
    lbu     t0, RES_IDS+0(sp)
    lbu     t1, RES_IDS+1(sp)
    sb      t1, RES_IDS+0(sp)
    sb      t0, RES_IDS+1(sp)

.Lvdb_bf_not_topk:
    addi    s2, s2, VDB_NODE_BYTES
    addi    s0, s0, 1
    bne     s0, s1, .Lvdb_bf_loop

    /* Write visit count = N */
    la      t0, vdb_visit_count
    sw      s1, 0(t0)

    /* Copy results */
    la      t0, vdb_result_ids
    lbu     t1, RES_IDS+0(sp)
    sb      t1, 0(t0)
    lbu     t1, RES_IDS+1(sp)
    sb      t1, 1(t0)
    lbu     t1, RES_IDS+2(sp)
    sb      t1, 2(t0)
    lbu     t1, RES_IDS+3(sp)
    sb      t1, 3(t0)

    la      t0, vdb_result_scores
    lw      t1, RES_SCORES+0(sp)
    sw      t1, 0(t0)
    lw      t1, RES_SCORES+4(sp)
    sw      t1, 4(t0)
    lw      t1, RES_SCORES+8(sp)
    sw      t1, 8(t0)
    lw      t1, RES_SCORES+12(sp)
    sw      t1, 12(t0)

    j       .Lvdb_done

.Lvdb_no_results:
    la      t0, vdb_result_ids
    li      t1, 0xFF
    sb      t1, 0(t0)
    sb      t1, 1(t0)
    sb      t1, 2(t0)
    sb      t1, 3(t0)
    la      t0, vdb_result_scores
    sw      zero, 0(t0)
    sw      zero, 4(t0)
    sw      zero, 8(t0)
    sw      zero, 12(t0)
    la      t0, vdb_visit_count
    sw      zero, 0(t0)

.Lvdb_done:
    /* ── Increment search counter ── */
    la      t0, vdb_search_count
    lw      t1, 0(t0)
    addi    t1, t1, 1
    sw      t1, 0(t0)

    /* ── Restore and return ── */
    lw      ra,  0(sp)
    lw      s0,  4(sp)
    lw      s1,  8(sp)
    lw      s2, 12(sp)
    lw      s3, 16(sp)
    lw      s4, 20(sp)
    lw      s5, 24(sp)
    lw      s6, 28(sp)
    lw      s7, 32(sp)
    lw      s8, 36(sp)
    lw      s9, 40(sp)
    lw      s10, 44(sp)
    lw      s11, 48(sp)
    addi    sp, sp, 608

    li      a0, 0
    ret


/* ═══════════════════════════════════════════════════════════════
 *  VDB INSERT — Build NSW Graph Edges for New Node
 *
 *  HP core has already:
 *    1. Packed the vector into vdb_nodes[new_id] (pos + neg)
 *    2. Set vdb_insert_id = new_id
 *    3. Set lp_command = 3
 *
 *  LP core does:
 *    1. Read new_id and current node_count
 *    2. If first node: set count=1, zero neighbors, done
 *    3. Brute-force scan all existing nodes, collect top-8 candidates
 *       (2×M sorted descending by dot score)
 *    4. Select top-M (up to 4) from candidates as forward neighbors.
 *       No diversity heuristic — plain top-M gives better connectivity
 *       for small N and low-dimensional ternary vectors.
 *    5. Write forward edges: new_node.neighbors = selected
 *    6. Reverse edges: for each selected neighbor, add new_node
 *       to their list (evict weakest if full)
 *    7. Increment node_count
 *
 *  Stack layout (224 bytes):
 *    [sp+0..51]     saved callee-saved regs (13 × 4)
 *    [sp+52..55]    padding
 *    [sp+56..111]   cand_scores[14] (14 × int32, sorted descending)
 *    [sp+112..125]  cand_ids[14]    (14 × uint8)
 *    [sp+126..127]  padding
 *    [sp+128..155]  sel_scores[7]   (7 × int32, selected neighbors)
 *    [sp+156..162]  sel_ids[7]      (7 × uint8)
 *    [sp+163]       padding
 *    [sp+164..167]  sel_count       (uint32)
 *    [sp+168..223]  scratch
 *
 *  Register plan:
 *    s0  = new_id (the node being inserted)
 *    s1  = existing node count (before insert)
 *    s2, s3 = node pos/neg pointers (INTERSECT_LOOP)
 *    s4, s5 = INTERSECT_LOOP output
 *    s6  = &query_pos (new node's pos, on stack or BSS)
 *    s7  = &query_neg (new node's neg)
 *    s8  = &popcount_lut
 *    s9  = scratch / pointer
 *    s10 = scratch / loop counter
 *    s11 = &vdb_nodes
 * ═══════════════════════════════════════════════════════════════ */

#define INS_CAND_SCORES  56     /* 14 × int32 = 56B */
#define INS_CAND_IDS     112    /* 14 × uint8 = 14B */
#define INS_SEL_SCORES   128    /* 7 × int32 = 28B */
#define INS_SEL_IDS      156    /* 7 × uint8 = 7B */
#define INS_SEL_COUNT    164    /* uint32 (aligned) */
#define INS_CAND_MAX     14     /* 2 × M */

.Lvdb_insert_entry:
    addi    sp, sp, -224
    sw      ra,  0(sp)
    sw      s0,  4(sp)
    sw      s1,  8(sp)
    sw      s2, 12(sp)
    sw      s3, 16(sp)
    sw      s4, 20(sp)
    sw      s5, 24(sp)
    sw      s6, 28(sp)
    sw      s7, 32(sp)
    sw      s8, 36(sp)
    sw      s9, 40(sp)
    sw      s10, 44(sp)
    sw      s11, 48(sp)

    la      s8, popcount_lut
    la      s11, vdb_nodes

    /* Read new_id and node_count */
    la      t0, vdb_insert_id
    lw      s0, 0(t0)                  /* s0 = new_id */
    la      t0, vdb_node_count
    lw      s1, 0(t0)                  /* s1 = current count */

    /* Set up s6/s7 pointing to new node's vector as the "query" */
    slli    t0, s0, 5                  /* new_id * 32 */
    add     s6, s11, t0               /* s6 = &node[new_id].pos */
    addi    s7, s6, 12                 /* s7 = &node[new_id].neg */

    /* Zero the new node's neighbor fields (offset 24..31 = 8 bytes) */
    slli    t0, s0, 5
    add     t0, s11, t0
    sw      zero, VDB_NEIGH_OFF(t0)   /* neighbors[0..3] = 0 */
    sw      zero, VDB_NEIGH_OFF+4(t0) /* neighbors[4..5] + ncnt + pad = 0 */

    /* If this is the first node, just increment count and done */
    bnez    s1, .Lins_has_nodes
    li      t0, 1
    la      t1, vdb_node_count
    sw      t0, 0(t1)
    j       .Lins_done_exit

.Lins_has_nodes:
    /* ── Phase 1: Brute-force score all existing nodes ──
     * Collect top-12 candidates (2×M) sorted descending. */

    /* Initialize candidate pool: all scores = INT32_MIN, ids = 0xFF */
    lui     t0, 0x80000
    li      t1, 0
    addi    t2, sp, INS_CAND_SCORES
.Lins_init_cand_scores:
    sw      t0, 0(t2)
    addi    t2, t2, 4
    addi    t1, t1, 1
    li      t3, INS_CAND_MAX
    blt     t1, t3, .Lins_init_cand_scores

    li      t0, 0xFF
    li      t1, 0
    addi    t2, sp, INS_CAND_IDS
.Lins_init_cand_ids:
    sb      t0, 0(t2)
    addi    t2, t2, 1
    addi    t1, t1, 1
    li      t3, INS_CAND_MAX
    blt     t1, t3, .Lins_init_cand_ids

    /* Scan all existing nodes */
    mv      s2, s11                    /* &nodes[0].pos */
    li      s10, 0                     /* scan index */

.Lins_scan_loop:
    /* Skip self (new_id) */
    beq     s10, s0, .Lins_scan_next

    addi    s3, s2, 12                 /* &node.neg */
    INTERSECT_LOOP
    /* s4 = dot(new_node, node[s10]) */

    /* Insert into top-12 candidate pool (looped bubble-up) */
    /* Check if better than worst (slot INS_CAND_MAX-1) */
    li      t3, INS_CAND_MAX
    addi    t3, t3, -1                 /* t3 = last slot index */
    slli    t0, t3, 2
    addi    t1, sp, INS_CAND_SCORES
    add     t0, t1, t0
    lw      t1, 0(t0)                 /* worst score */
    ble     s4, t1, .Lins_scan_next

    /* Replace worst slot */
    sw      s4, 0(t0)                 /* cand_scores[last] = s4 */
    addi    t0, sp, INS_CAND_IDS
    add     t0, t0, t3
    sb      s10, 0(t0)                /* cand_ids[last] = s10 */

    /* Bubble up from position t3 (last) toward 0 */
    mv      s9, t3                     /* s9 = current position */
.Lins_bubble_up:
    beqz    s9, .Lins_scan_next        /* at top, done */
    addi    t4, s9, -1                 /* t4 = predecessor index */

    /* Compare scores */
    slli    t0, s9, 2
    addi    t1, sp, INS_CAND_SCORES
    add     t0, t1, t0                 /* &scores[pos] */
    slli    t2, t4, 2
    add     t2, t1, t2                 /* &scores[pos-1] */
    lw      a5, 0(t0)                 /* score at pos */
    lw      a6, 0(t2)                 /* score at pos-1 */
    ble     a5, a6, .Lins_scan_next   /* in order, done */

    /* Swap scores */
    sw      a6, 0(t0)
    sw      a5, 0(t2)

    /* Swap IDs */
    addi    t0, sp, INS_CAND_IDS
    add     t1, t0, s9
    add     t2, t0, t4
    lbu     a5, 0(t1)
    lbu     a6, 0(t2)
    sb      a6, 0(t1)
    sb      a5, 0(t2)

    mv      s9, t4
    j       .Lins_bubble_up

.Lins_scan_next:
    addi    s2, s2, VDB_NODE_BYTES
    addi    s10, s10, 1
    bne     s10, s1, .Lins_scan_loop

    /* ── Phase 2: Select Top-M Neighbors ──
     *
     * For N=64 with M=4 and 48-trit vectors, plain top-M selection
     * produces better connectivity than diversity heuristic. The small
     * vector dimension means many nodes have similar dot products,
     * causing diversity to reject too aggressively and create sparse
     * graphs. Top-M ensures maximum connectivity.
     *
     * Simply copy cand_ids[0..min(M-1, valid_count-1)] → sel_ids,
     * cand_scores[0..] → sel_scores. The candidates are already
     * sorted descending from Phase 1.
     */
    li      t3, 0                      /* sel_count = 0 */
    li      s10, 0                     /* candidate index */

.Lins_sel_loop:
    li      t0, VDB_M
    bge     t3, t0, .Lins_sel_done     /* have M neighbors */
    li      t0, INS_CAND_MAX
    bge     s10, t0, .Lins_sel_done    /* exhausted candidates */

    /* Load candidate ID */
    addi    t0, sp, INS_CAND_IDS
    add     t0, t0, s10
    lbu     s9, 0(t0)                 /* s9 = cand_id */
    li      t0, 0xFF
    beq     s9, t0, .Lins_sel_done    /* empty slot = end of valid candidates */

    /* Copy to sel */
    addi    t0, sp, INS_SEL_IDS
    add     t0, t0, t3
    sb      s9, 0(t0)                 /* sel_ids[sel_count] = cand_id */

    slli    t0, t3, 2
    addi    t1, sp, INS_SEL_SCORES
    add     t0, t1, t0
    slli    t1, s10, 2
    addi    t2, sp, INS_CAND_SCORES
    add     t1, t2, t1
    lw      t2, 0(t1)
    sw      t2, 0(t0)                 /* sel_scores[sel_count] = score */

    addi    t3, t3, 1
    addi    s10, s10, 1
    j       .Lins_sel_loop

.Lins_sel_done:
    sw      t3, INS_SEL_COUNT(sp)
    /* ── Phase 3: Write Forward Edges ──
     * new_node.neighbors = sel_ids, new_node.neighbor_count = sel_count */
    lw      t3, INS_SEL_COUNT(sp)     /* t3 = sel_count */
    slli    t0, s0, 5
    add     t0, s11, t0               /* t0 = &node[new_id] */

    /* Write neighbor_count */
    sb      t3, VDB_NCNT_OFF(t0)

    /* Write neighbor IDs (up to 7 bytes at offset 24..30) */
    addi    t1, t0, VDB_NEIGH_OFF     /* &node.neighbors[0] */
    addi    t2, sp, INS_SEL_IDS
    lw      t4, 0(t2)                 /* load bytes 0..3 as uint32 */
    sw      t4, 0(t1)                 /* store neighbors[0..3] */
    lhu     t4, 4(t2)                 /* load bytes 4..5 as uint16 */
    sh      t4, 4(t1)                 /* store neighbors[4..5] */
    lbu     t4, 6(t2)                 /* load byte 6 */
    sb      t4, 6(t1)                 /* store neighbor[6] */

    /* ── Phase 4: Reverse Edges ──
     * For each selected neighbor nb:
     *   - If nb has room (count < M): append new_id
     *   - If nb is full (count == M): find weakest edge, replace if
     *     new_id is better.
     */
    li      s10, 0                     /* index into sel_ids */

.Lins_rev_loop:
    lw      t3, INS_SEL_COUNT(sp)
    bge     s10, t3, .Lins_rev_done

    /* Load neighbor ID */
    addi    t0, sp, INS_SEL_IDS
    add     t0, t0, s10
    lbu     s9, 0(t0)                 /* s9 = neighbor node ID */

    /* Get pointer to neighbor's node */
    slli    t0, s9, 5
    add     t0, s11, t0               /* t0 = &node[s9] */

    /* Load neighbor's current edge count */
    lbu     t1, VDB_NCNT_OFF(t0)      /* t1 = nb.neighbor_count */

    li      t2, VDB_M
    bge     t1, t2, .Lins_rev_full

    /* ── Room available: append new_id ── */
    addi    t2, t0, VDB_NEIGH_OFF
    add     t2, t2, t1                /* &nb.neighbors[count] */
    sb      s0, 0(t2)                 /* nb.neighbors[count] = new_id */
    addi    t1, t1, 1
    sb      t1, VDB_NCNT_OFF(t0)      /* nb.neighbor_count++ */
    j       .Lins_rev_next

.Lins_rev_full:
    /* ── Neighbor is full (M edges) ──
     * Find the weakest existing edge and replace if new_id is stronger.
     *
     * Compute dot(nb, new_id) and dot(nb, each existing neighbor).
     * Replace the weakest existing edge if new_id beats it.
     */

    /* Save s10, s9 to scratch area (starts at 168) */
    sw      s10, 168(sp)
    sw      s9,  172(sp)

    /* Set s6/s7 to neighbor's vector as "query" for dot computations */
    slli    t0, s9, 5
    add     s6, s11, t0
    addi    s7, s6, 12

    /* dot(nb, new_id) */
    DOT_NODE s0
    mv      a5, s4                     /* a5 = dot(nb, new_id) */
    sw      a5, 176(sp)               /* save new_score */

    /* Find weakest existing edge */
    lw      s9, 172(sp)               /* restore nb ID */
    slli    t0, s9, 5
    add     t0, s11, t0
    addi    t5, t0, VDB_NEIGH_OFF     /* t5 = &nb.neighbors[0] */

    lui     t3, 0x7FFFF                /* best_weak = INT32_MAX-ish */
    addi    t3, t3, 0x7FF              /* (large positive) */
    li      t4, -1                     /* weak_slot = -1 */
    li      s10, 0                     /* edge index */

.Lins_rev_find_weak:
    li      t0, VDB_M
    bge     s10, t0, .Lins_rev_check_replace

    add     t0, t5, s10
    lbu     a7, 0(t0)                 /* a7 = existing neighbor ID */

    /* dot(nb, existing_neighbor) */
    sw      t5, 180(sp)
    sw      t3, 184(sp)
    sw      t4, 188(sp)
    sw      s10, 192(sp)

    /* s6/s7 still = nb's vector */
    DOT_NODE a7
    /* s4 = dot(nb, existing_neighbor) */

    lw      t5, 180(sp)
    lw      t3, 184(sp)
    lw      t4, 188(sp)
    lw      s10, 192(sp)

    /* If s4 < t3, this is the new weakest */
    bge     s4, t3, .Lins_rev_not_weakest
    mv      t3, s4                     /* t3 = weak_score */
    mv      t4, s10                    /* t4 = weak_slot */

.Lins_rev_not_weakest:
    addi    s10, s10, 1
    j       .Lins_rev_find_weak

.Lins_rev_check_replace:
    /* t3 = weakest edge score, t4 = weakest slot index */
    /* a5 = dot(nb, new_id) — reload from stack */
    lw      a5, 176(sp)

    /* If new_id is stronger than weakest edge, replace */
    ble     a5, t3, .Lins_rev_no_replace

    /* Replace: nb.neighbors[weak_slot] = new_id */
    lw      s9, 172(sp)
    slli    t0, s9, 5
    add     t0, s11, t0
    addi    t0, t0, VDB_NEIGH_OFF
    add     t0, t0, t4
    sb      s0, 0(t0)                 /* overwrite weakest */

.Lins_rev_no_replace:
    /* Restore s6/s7 to new node's vector */
    slli    t0, s0, 5
    add     s6, s11, t0
    addi    s7, s6, 12

    lw      s10, 168(sp)              /* restore sel index */
    lw      s9, 172(sp)               /* restore (not strictly needed) */

.Lins_rev_next:
    addi    s10, s10, 1
    j       .Lins_rev_loop

.Lins_rev_done:
    /* ── Phase 5: Increment node count ── */
    la      t0, vdb_node_count
    lw      t1, 0(t0)
    addi    t1, t1, 1
    sw      t1, 0(t0)

.Lins_done_exit:
    /* ── Increment insert counter ── */
    la      t0, vdb_insert_count
    lw      t1, 0(t0)
    addi    t1, t1, 1
    sw      t1, 0(t0)

    /* ── Restore and return ── */
    lw      ra,  0(sp)
    lw      s0,  4(sp)
    lw      s1,  8(sp)
    lw      s2, 12(sp)
    lw      s3, 16(sp)
    lw      s4, 20(sp)
    lw      s5, 24(sp)
    lw      s6, 28(sp)
    lw      s7, 32(sp)
    lw      s8, 36(sp)
    lw      s9, 40(sp)
    lw      s10, 44(sp)
    lw      s11, 48(sp)
    addi    sp, sp, 224

    li      a0, 0
    ret


/* ═══════════════════════════════════════════════════════════════
 *  Common exit paths
 * ═══════════════════════════════════════════════════════════════ */
.Lrestore_and_exit:
    lw      ra,  40(sp)
    lw      s0,  48(sp)
    lw      s1,  52(sp)
    lw      s2,  56(sp)
    lw      s3,  60(sp)
    lw      s4,  64(sp)
    lw      s5,  68(sp)
    lw      s6,  72(sp)
    lw      s7,  76(sp)
    lw      s8,  80(sp)
    lw      s9,  84(sp)
    lw      s10, 88(sp)
    lw      s11, 92(sp)
    addi    sp, sp, 96

    li      a0, 0
    ret

.Learly_exit:
    li      a0, 0
    ret

    .size   main, .-main
