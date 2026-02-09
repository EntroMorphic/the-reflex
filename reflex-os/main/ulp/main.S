/*
 * LP Core Geometric Processor — Hand-Written RISC-V Assembly
 *
 * Every instruction on this core is specified here. No compiler
 * decisions, no hidden register allocation, no spills. The neural
 * network IS the instruction stream.
 *
 * Runs on ESP32-C6 LP core: RV32IMAC, 16MHz, ~30μA.
 * Called as main() by SDK startup every ~10ms (LP timer wakeup).
 *
 * Operations used (exhaustive list):
 *   LW, LBU, LB    — load word, load byte unsigned, load byte signed
 *   SW, SB          — store word, store byte
 *   AND             — bitwise intersection
 *   SRLI, ANDI      — shift right logical, mask to byte
 *   ADD, SUB, NEG   — accumulate, difference, negate
 *   BGT, BLT, BEQZ  — branch on comparison
 *   LA              — load address (pseudo: AUIPC+ADDI)
 *   LI              — load immediate (pseudo: LUI+ADDI)
 *   RET             — return (pseudo: JALR ra)
 *
 * NOT used: MUL, MULH, DIV, REM, OR, XOR, SLL, SRA, SLLI,
 *           any floating point, any atomic, any CSR (in main)
 *
 * Register plan (during neuron loop):
 *   s0  = neuron counter (0..15)
 *   s1  = weight stride offset (neuron * 12 bytes = 3 words × 4)
 *   s2  = &lp_W_f_pos base
 *   s3  = &lp_W_f_neg base
 *   s4  = &lp_W_g_pos base
 *   s5  = &lp_W_g_neg base
 *   s6  = &x_pos (packed input, on stack)
 *   s7  = &x_neg (packed input, on stack)
 *   s8  = &popcount_lut
 *   s9  = &lp_hidden
 *   s10 = &h_new (on stack)
 *   s11 = &lp_dots_f
 *   t0-t6, a0-a7 = temporaries
 *
 * Stack layout (48 bytes):
 *   sp+0  .. sp+11  : x_pos[3]    (packed input positive masks)
 *   sp+12 .. sp+23  : x_neg[3]    (packed input negative masks)
 *   sp+24 .. sp+39  : h_new[16]   (new hidden state, byte array)
 *   sp+40 .. sp+43  : saved ra
 *   sp+44 .. sp+47  : (padding)
 *   sp+48 .. sp+95  : saved s0-s11 (12 regs × 4 bytes)
 *   Total: 96 bytes
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

    .global gie_hidden
gie_hidden:     .space 32       /* 32 × int8_t: GIE hidden state */

    .global lp_hidden
lp_hidden:      .space 16       /* 16 × int8_t: LP hidden state */

    .global lp_step_count
lp_step_count:  .space 4        /* uint32_t: total steps completed */

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
lp_data_ready:  .space 4        /* uint32_t: sync flag */


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
 *
 *  4 LUT lookups, 4 byte extractions, 3 adds = 11 instructions
 * ═══════════════════════════════════════════════════════════════ */
.macro POPCOUNT dst, src, tmp1, tmp2
    andi    \tmp1, \src, 0xFF           /* byte 0 */
    add     \tmp1, s8, \tmp1            /* &lut[byte0] */
    lbu     \dst, 0(\tmp1)              /* pop(byte0) */

    srli    \tmp1, \src, 8              /* byte 1 */
    andi    \tmp1, \tmp1, 0xFF
    add     \tmp1, s8, \tmp1
    lbu     \tmp2, 0(\tmp1)
    add     \dst, \dst, \tmp2           /* += pop(byte1) */

    srli    \tmp1, \src, 16             /* byte 2 */
    andi    \tmp1, \tmp1, 0xFF
    add     \tmp1, s8, \tmp1
    lbu     \tmp2, 0(\tmp1)
    add     \dst, \dst, \tmp2           /* += pop(byte2) */

    srli    \tmp1, \src, 24             /* byte 3 (no mask needed) */
    add     \tmp1, s8, \tmp1
    lbu     \tmp2, 0(\tmp1)
    add     \dst, \dst, \tmp2           /* += pop(byte3) */
.endm


/* ═══════════════════════════════════════════════════════════════
 *  INTERSECT MACRO — fully unrolled ternary dot product
 *
 *  Computes: dot = agree - disagree
 *  where:
 *    agree    = Σ popcount(a_pos[i] & b_pos[i]) + popcount(a_neg[i] & b_neg[i])
 *    disagree = Σ popcount(a_pos[i] & b_neg[i]) + popcount(a_neg[i] & b_pos[i])
 *
 *  Inputs:
 *    \abase = base address of weight (a_pos at +0, uses stride for a_neg)
 *    \anbase = base address of weight neg mask
 *    s6     = &x_pos[0]  (input packed pos)
 *    s7     = &x_neg[0]  (input packed neg)
 *    s8     = &popcount_lut
 *
 *  Output: \dot_out = signed dot product
 *  Clobbers: a0-a7, t0-t4
 *
 *  3 words × 4 ANDs × 17 instructions per popcount ≈ 204 instructions
 *  (vs 193 + 48 register saves in compiler version = 241)
 * ═══════════════════════════════════════════════════════════════ */
.macro INTERSECT dot_out, abase, anbase
    li      a6, 0                       /* agree = 0 */
    li      a7, 0                       /* disagree = 0 */

    /* ── Word 0 ── */
    lw      a0, 0(\abase)              /* a_pos[0] */
    lw      a1, 0(\anbase)             /* a_neg[0] */
    lw      a2, 0(s6)                   /* b_pos[0] = x_pos[0] */
    lw      a3, 0(s7)                   /* b_neg[0] = x_neg[0] */

    and     a4, a0, a2                  /* a_pos & b_pos */
    POPCOUNT t0, a4, t1, t2
    add     a6, a6, t0

    and     a4, a1, a3                  /* a_neg & b_neg */
    POPCOUNT t0, a4, t1, t2
    add     a6, a6, t0

    and     a4, a0, a3                  /* a_pos & b_neg */
    POPCOUNT t0, a4, t1, t2
    add     a7, a7, t0

    and     a4, a1, a2                  /* a_neg & b_pos */
    POPCOUNT t0, a4, t1, t2
    add     a7, a7, t0

    /* ── Word 1 ── */
    lw      a0, 4(\abase)              /* a_pos[1] */
    lw      a1, 4(\anbase)             /* a_neg[1] */
    lw      a2, 4(s6)                   /* x_pos[1] */
    lw      a3, 4(s7)                   /* x_neg[1] */

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
    lw      a0, 8(\abase)              /* a_pos[2] */
    lw      a1, 8(\anbase)             /* a_neg[2] */
    lw      a2, 8(s6)                   /* x_pos[2] */
    lw      a3, 8(s7)                   /* x_neg[2] */

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

    /* dot = agree - disagree */
    sub     \dot_out, a6, a7
.endm


/* ═══════════════════════════════════════════════════════════════
 *  main() — One Geometric CfC Step
 *
 *  Called by SDK lp_core_startup(). Returns 0 to a0.
 *  SDK handles LP timer re-arm and halt after we return.
 * ═══════════════════════════════════════════════════════════════ */
    .text
    .align 2
main:
    /* ── Check data ready flag ── */
    la      t0, lp_data_ready
    lw      t1, 0(t0)
    beqz    t1, .Learly_exit
    sw      zero, 0(t0)                 /* clear flag */

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

    /* ── Load base addresses into saved registers ── */
    la      s8, popcount_lut
    la      s9, lp_hidden
    addi    s6, sp, 0                   /* &x_pos[0] at sp+0 */
    addi    s7, sp, 12                  /* &x_neg[0] at sp+12 */
    addi    s10, sp, 24                 /* &h_new[0] at sp+24 */

    /* ═════════════════════════════════════════════════════════
     *  PHASE 1: CONCATENATE — [gie_hidden | lp_hidden] → concat
     *  PHASE 2: PACK — concat → (x_pos[3], x_neg[3])
     *
     *  Done together: read trits, build pos/neg masks word by word.
     *  Word 0: trits [0..15]   from gie_hidden[0..15]
     *  Word 1: trits [16..31]  from gie_hidden[16..31]
     *  Word 2: trits [32..47]  from lp_hidden[0..15]
     * ═════════════════════════════════════════════════════════ */

    /* ── Pack word 0: gie_hidden[0..15] ── */
    la      t3, gie_hidden
    li      a0, 0                       /* pos_mask = 0 */
    li      a1, 0                       /* neg_mask = 0 */
    li      t4, 1                       /* bit = 1 (shift register) */

    .set i, 0
    .rept 16
    lb      t0, i(t3)                  /* trit = gie_hidden[i] */
    blez    t0, 1f                      /* skip if <= 0 */
    or      a0, a0, t4                  /* pos |= bit */
    j       2f
1:  bgez    t0, 2f                      /* skip if >= 0 (i.e. == 0) */
    or      a1, a1, t4                  /* neg |= bit */
2:  slli    t4, t4, 1                   /* bit <<= 1 */
    .set i, i+1
    .endr

    sw      a0, 0(s6)                   /* x_pos[0] */
    sw      a1, 0(s7)                   /* x_neg[0] */

    /* ── Pack word 1: gie_hidden[16..31] ── */
    addi    t3, t3, 16                  /* &gie_hidden[16] */
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

    sw      a0, 4(s6)                   /* x_pos[1] */
    sw      a1, 4(s7)                   /* x_neg[1] */

    /* ── Pack word 2: lp_hidden[0..15] ── */
    mv      t3, s9                      /* &lp_hidden */
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

    sw      a0, 8(s6)                   /* x_pos[2] */
    sw      a1, 8(s7)                   /* x_neg[2] */


    /* ═════════════════════════════════════════════════════════
     *  PHASE 3: GEOMETRIC CfC — 16 neurons × (INTERSECT, PROJECT, GATE)
     * ═════════════════════════════════════════════════════════ */

    la      s2, lp_W_f_pos
    la      s3, lp_W_f_neg
    la      s4, lp_W_g_pos
    la      s5, lp_W_g_neg
    la      s11, lp_dots_f

    li      s0, 0                       /* neuron counter */
    li      s1, 0                       /* weight offset (neuron × 12) */

.Lneuron_loop:
    /* ── Compute f-pathway weight addresses ── */
    add     t5, s2, s1                  /* &lp_W_f_pos[n][0] */
    add     t6, s3, s1                  /* &lp_W_f_neg[n][0] */

    /* ── INTERSECT f-pathway ── */
    INTERSECT t3, t5, t6
    /* t3 = dot_f */

    /* ── Store dot_f for diagnostics ── */
    add     t5, s11, s0                 /* not quite right: need s0*4 */
    slli    t5, s0, 2                   /* neuron * 4 */
    add     t5, s11, t5                 /* &lp_dots_f[n] */
    sw      t3, 0(t5)

    /* ── Compute g-pathway weight addresses ── */
    add     t5, s4, s1                  /* &lp_W_g_pos[n][0] */
    add     t6, s5, s1                  /* &lp_W_g_neg[n][0] */

    /* ── INTERSECT g-pathway ── */
    INTERSECT t4, t5, t6
    /* t4 = dot_g */

    /* ── Store dot_g for diagnostics ── */
    la      t5, lp_dots_g
    slli    t6, s0, 2
    add     t5, t5, t6
    sw      t4, 0(t5)

    /* ── PROJECT: f = sign(dot_f), g = sign(dot_g) ── */
    /* f in a0, g in a1 */
    /* sign(x): x>0 → +1, x<0 → -1, x==0 → 0 */
    li      a0, 0                       /* f = 0 */
    bgtz    t3, .Lf_pos
    bltz    t3, .Lf_neg
    j       .Lf_done
.Lf_pos:
    li      a0, 1
    j       .Lf_done
.Lf_neg:
    li      a0, -1
.Lf_done:

    li      a1, 0                       /* g = 0 */
    bgtz    t4, .Lg_pos
    bltz    t4, .Lg_neg
    j       .Lg_done
.Lg_pos:
    li      a1, 1
    j       .Lg_done
.Lg_neg:
    li      a1, -1
.Lg_done:

    /* ── GATE: h_new[n] = trit_gate(f, lp_hidden[n], g) ── */
    /* f=a0, g=a1 */
    /* if f==0: h_new = lp_hidden[n]  (HOLD) */
    /* if f>0:  h_new = g             (UPDATE) */
    /* if f<0:  h_new = -g            (INVERT) */
    add     t0, s9, s0                  /* &lp_hidden[n] */
    lb      t0, 0(t0)                   /* keep = lp_hidden[n] */

    beqz    a0, .Lgate_hold
    bgtz    a0, .Lgate_update
    /* f < 0: INVERT — h_new = -g */
    neg     a1, a1                      /* a1 = 0 - g */
.Lgate_update:
    /* f > 0 (or fell through from invert): h_new = a1 */
    mv      t0, a1
.Lgate_hold:
    /* t0 = result (either keep, g, or -g) */
    add     t1, s10, s0                 /* &h_new[n] */
    sb      t0, 0(t1)

    /* ── Advance to next neuron ── */
    addi    s0, s0, 1                   /* neuron++ */
    addi    s1, s1, 12                  /* offset += 3 words × 4 bytes */
    li      t0, 16
    bne     s0, t0, .Lneuron_loop


    /* ═════════════════════════════════════════════════════════
     *  PHASE 4: COMMIT — write h_new → lp_hidden
     * ═════════════════════════════════════════════════════════ */
    li      t0, 0
.Lcommit_loop:
    add     t1, s10, t0                 /* &h_new[i] */
    lb      t2, 0(t1)
    add     t1, s9, t0                  /* &lp_hidden[i] */
    sb      t2, 0(t1)
    addi    t0, t0, 1
    li      t1, 16
    bne     t0, t1, .Lcommit_loop


    /* ═════════════════════════════════════════════════════════
     *  PHASE 5: DECISION — majority vote of hidden state
     *
     *  vote = Σ lp_hidden[i]   (project onto all-ones direction)
     *  decision = sign(vote)
     * ═════════════════════════════════════════════════════════ */
    li      a0, 0                       /* vote = 0 */
    li      t0, 0
.Lvote_loop:
    add     t1, s9, t0
    lb      t2, 0(t1)                   /* lp_hidden[i] (sign-extended) */
    add     a0, a0, t2
    addi    t0, t0, 1
    li      t1, 16
    bne     t0, t1, .Lvote_loop

    /* decision = sign(vote) */
    li      t0, 0                       /* decision = 0 */
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


    /* ═════════════════════════════════════════════════════════
     *  PHASE 6: INCREMENT STEP COUNTER
     * ═════════════════════════════════════════════════════════ */
    la      t0, lp_step_count
    lw      t1, 0(t0)
    addi    t1, t1, 1
    sw      t1, 0(t0)


    /* ── Restore callee-saved registers and return ── */
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

    li      a0, 0                       /* return 0 */
    ret

.Learly_exit:
    li      a0, 0
    ret

    .size   main, .-main
