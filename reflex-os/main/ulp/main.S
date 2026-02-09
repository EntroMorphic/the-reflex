/*
 * LP Core Geometric Processor + Ternary Vector Database
 * Hand-Written RISC-V Assembly
 *
 * Every instruction on this core is specified here. No compiler
 * decisions, no hidden register allocation, no spills.
 *
 * Two modes, selected by lp_command:
 *   CMD 1: CfC neural network step (ternary liquid neural network)
 *   CMD 2: VDB nearest-neighbor search (brute-force ternary dot)
 *
 * Both modes use the same INTERSECT primitive:
 *   dot = popcount(a_pos & b_pos) + popcount(a_neg & b_neg)
 *       - popcount(a_pos & b_neg) - popcount(a_neg & b_pos)
 *
 * The CfC computes 32 intersections against weight matrices.
 * The VDB computes N intersections against stored vectors.
 * Same AND+popcount core. Same assembly. Different data.
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
lp_command:     .space 4        /* 0=noop, 1=CfC step, 2=VDB search */

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
 *  VDB STATE — Ternary Vector Database
 *
 *  48-trit vectors packed as (pos_mask[3], neg_mask[3])
 *  = 6 words = 24 bytes per vector.
 *
 *  Node layout (24 bytes):
 *    [0..11]   pos_mask[3]  (3 × uint32_t)
 *    [12..23]  neg_mask[3]  (3 × uint32_t)
 *
 *  64 nodes × 24 bytes = 1,536 bytes
 *
 *  Brute-force search: dot query against all nodes, return best.
 *  HNSW graph can be layered on later.
 * ═══════════════════════════════════════════════════════════════ */

#define VDB_MAX_NODES   64
#define VDB_NODE_BYTES  24      /* 6 words: 3 pos + 3 neg */
#define VDB_K_MAX       4       /* max results returned */

    .global vdb_node_count
vdb_node_count: .space 4        /* uint32_t: number of stored vectors */

    .global vdb_nodes
vdb_nodes:      .space (VDB_MAX_NODES * VDB_NODE_BYTES)  /* 1536 bytes */

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
 *  INTERSECT_LOOP — looped version for VDB search
 *
 *  Same math as INTERSECT but uses a loop over 3 words instead
 *  of full unrolling. Saves code space (~200 bytes per use vs
 *  ~600 for the unrolled version). Slightly slower but called
 *  once per node, and VDB search is not latency-critical.
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
.Lintersect_word:
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
    bne     t5, t3, .Lintersect_word

    sub     s4, s4, s5                  /* dot = agree - disagree */
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
 *    2 = VDB search (read query, scan nodes, write results)
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
 *  VDB MODE — Ternary Vector Database Search
 *
 *  Brute-force nearest neighbor: dot the query against every
 *  stored node, return the top-K matches.
 *
 *  The query is pre-packed by the HP core into vdb_query_pos[3]
 *  and vdb_query_neg[3]. Nodes are stored pre-packed.
 *
 *  Uses the same INTERSECT as CfC — same AND+popcount core.
 *  The only difference: instead of fixed weight matrices, we
 *  iterate over a dynamic array of stored vectors.
 *
 *  Register plan:
 *    s0  = node index (0..N-1)
 *    s1  = node count
 *    s2  = &current_node_pos  (incremented by 24 per node)
 *    s3  = &current_node_neg  (= s2 + 12)
 *    s4  = dot result (from INTERSECT_LOOP)
 *    s5  = (used by INTERSECT_LOOP)
 *    s6  = &query_pos
 *    s7  = &query_neg
 *    s8  = &popcount_lut
 *    s9  = best_score
 *    s10 = best_id
 *    s11 = (free)
 * ═══════════════════════════════════════════════════════════════ */
.Lvdb_entry:
    /* ── Save callee-saved registers ── */
    addi    sp, sp, -56
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

    /* ── Load node count, bail if empty ── */
    la      t0, vdb_node_count
    lw      s1, 0(t0)
    beqz    s1, .Lvdb_no_results

    /* ── Initialize best = -infinity (0x80000000) ── */
    lui     s9, 0x80000                 /* best_score = INT32_MIN */
    li      s10, 0xFF                   /* best_id = 0xFF (none) */

    /* ── Scan all nodes ── */
    la      s2, vdb_nodes               /* &nodes[0].pos */
    li      s0, 0                       /* node index */

.Lvdb_scan_loop:
    /* s2 = &node[s0].pos[0], s3 = &node[s0].neg[0] = s2+12 */
    addi    s3, s2, 12

    /* ── INTERSECT: dot(query, node) ── */
    /* Uses looped version to save code space.
     * Result in s4. Clobbers a0-a7, t0-t5, s4, s5. */
    INTERSECT_LOOP

    /* s4 = dot product of query vs node[s0] */

    /* ── Update best if this dot > best_score ── */
    ble     s4, s9, .Lvdb_not_better
    mv      s9, s4                      /* best_score = dot */
    mv      s10, s0                     /* best_id = node index */
.Lvdb_not_better:

    /* ── Advance to next node ── */
    addi    s2, s2, VDB_NODE_BYTES      /* next node (+24 bytes) */
    addi    s0, s0, 1
    bne     s0, s1, .Lvdb_scan_loop

    /* ── Write results ── */
    la      t0, vdb_result_ids
    sb      s10, 0(t0)                  /* result_ids[0] = best_id */

    la      t0, vdb_result_scores
    sw      s9, 0(t0)                   /* result_scores[0] = best_score */

    j       .Lvdb_done

.Lvdb_no_results:
    /* No nodes — write 0xFF / 0 */
    la      t0, vdb_result_ids
    li      t1, 0xFF
    sb      t1, 0(t0)
    la      t0, vdb_result_scores
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
    addi    sp, sp, 56

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
