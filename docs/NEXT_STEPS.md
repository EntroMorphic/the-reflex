# Next Steps: MTFP Dot Encoding for LP CfC Neurons

*April 7, 2026. Based on LP dot magnitude diagnostic (commit `7391876`).*

---

## The Problem

The LP CfC has 16 neurons. Each computes a ternary dot product against the 48-trit input (`[gie_hidden[32] | lp_hidden[16]]`), producing an integer in approximately [-48, +48]. The `sign()` function then quantizes this to {-1, 0, +1} — one trit per neuron. The LP hidden state is 16 trits.

For patterns P1 and P2, the sign pattern is nearly identical: 12 of 16 neurons produce the same sign. The LP Hamming between P1 and P2 is 0-4 across runs. This is the P1-P2 degeneracy.

**But the dot magnitudes differ.** The diagnostic (commit `7391876`) proved this on silicon:

```
LP f-pathway dots (per neuron):
       n00 n01 n02 n03 n04 n05 n06 n07 n08 n09 n10 n11 n12 n13 n14 n15
P1:    +5  -7  -1  +7  +4  +5  +8  +0 -12  +5  +0  -3  +3 -13  +6  +1
P2:    +5  -7  -5  +7  +6 +13  +4  -2  -6  -5  -6  -1  +3  -3  -2  +1

P1 vs P2 comparison:
  n05: P1=+5  P2=+13  sign=SAME  |mag_diff|=8
  n08: P1=-12 P2=-6   sign=SAME  |mag_diff|=6
  n13: P1=-13 P2=-3   sign=SAME  |mag_diff|=10
  n02: P1=-1  P2=-5   sign=SAME  |mag_diff|=4
  n06: P1=+8  P2=+4   sign=SAME  |mag_diff|=4

Signs: 12 same, 4 different
Mean |magnitude difference|: 3.0
```

The projection IS separating P1 and P2. The `sign()` function is collapsing the separation. Three neurons (n05, n08, n13) have magnitude differences of 8, 6, and 10 — substantial — with identical signs.

This is the same bottleneck pattern as the timing encoding: the thermometer collapsed P1-pause and P2-steady into identical trits. MTFP21 gap history resolved it by preserving temporal structure. Here, `sign()` collapses P1 and P2 dots into identical trits. MTFP dot encoding resolves it by preserving magnitude structure.

---

## The Solution: 5-Trit MTFP Per Neuron

Replace `sign(dot)` → 1 trit per neuron with MTFP encoding → 5 trits per neuron.

### Encoding Format (per neuron)

```
┌──────┬──────┬──────────┬──────────┬──────────┐
│sign  │ exp0 │  exp1    │ mant0    │ mant1    │
│{-1,+1}│ magnitude  │ position within  │
│      │ scale      │ magnitude range  │
└──────┴──────┴──────────┴──────────┴──────────┘
```

5 trits = 3^5 = 243 possible states. Dot range is approximately [-48, +48] = 97 values. 243 > 97, so the encoding can represent every distinct dot value.

**Trit 0: Sign.** +1 if dot > 0, -1 if dot < 0, 0 if dot == 0. (Same as current `sign()`)

**Trits 1-2: Magnitude exponent.** Order of magnitude of |dot|.

| exp0 | exp1 | |dot| range | Meaning |
|:---:|:---:|---:|---|
| -1 | -1 | 0 | Zero (HOLD — gate didn't fire) |
| -1 | 0 | 1-3 | Weak (barely fired) |
| -1 | +1 | 4-8 | Moderate |
| 0 | -1 | 9-15 | Strong |
| 0 | 0 | 16-24 | Very strong |
| 0 | +1 | 25-35 | Dominant |
| +1 | -1 | 36-48 | Saturated |
| +1 | 0 | 49+ | Overflow |

Boundaries tuned to the observed dot distribution. P1 n05 (dot=+5) and P2 n05 (dot=+13) have the same sign (+1) but different exponents: scale 2 (4-8) vs scale 3 (9-15). The exponent alone discriminates them.

**Trits 3-4: Mantissa.** Position within the magnitude range (lower/middle/upper third × lower/upper half = 9 positions from 2 trits). Provides intra-scale resolution.

### LP Hidden State: 16 × 5 = 80 Trits

The LP hidden state expands from 16 trits to 80 trits. Each neuron contributes 5 trits instead of 1.

```
lp_hidden[0..4]:    neuron 0  [sign, exp0, exp1, mant0, mant1]
lp_hidden[5..9]:    neuron 1  [sign, exp0, exp1, mant0, mant1]
...
lp_hidden[75..79]:  neuron 15 [sign, exp0, exp1, mant0, mant1]
```

---

## What Changes

### LP Core Assembly (`main.S`)

The LP CfC currently computes `h_new[n] = gate(sign(dot_f), lp_hidden[n], sign(dot_g))`. With 5-trit encoding:

**Option A: Encode on LP core.** After computing `dot_f`, encode it as 5 MTFP trits and store in `lp_hidden[n*5..(n+1)*5-1]`. The LP assembly needs an MTFP encoder macro. The gate function changes: instead of `gate(sign, keep, candidate)` operating on single trits, it operates on 5-trit MTFP values. The gate decision (HOLD/UPDATE/INVERT) still uses the sign trit (trit 0), but the UPDATE/INVERT operations replace all 5 trits.

**Option B: Encode on HP core.** The LP core writes raw `dot_f[16]` and `dot_g[16]` to LP SRAM (it already does this for diagnostics). The HP core reads them, encodes as MTFP, and writes back to a separate `lp_hidden_mtfp[80]` in LP SRAM. The LP core's CfC step is unchanged. The MTFP encoding is HP-side only.

**Recommendation: Option B.** The LP core's hand-written assembly is the most fragile code in the system. Adding an MTFP encoder macro to RISC-V assembly is high-risk, low-reward. The HP core runs at 160 MHz and has plenty of time between CMD 5 dispatches (~250ms at 4 Hz) to encode 16 × 5 = 80 trits. The LP core continues to compute in sign-space (its native representation). The HP core lifts the sign-space output into MTFP-space for downstream use.

This means the LP CfC's *internal* dynamics remain ternary (sign-based gates). The MTFP encoding is an *external representation* of the LP state — used by the VDB, the agreement computation, and the gate bias mechanism. The LP core doesn't need to know MTFP exists.

### VDB Snapshot Dimension

Currently: `VDB_SNAPSHOT_DIM = 48` (32 GIE + 16 LP).

**Option A: Full MTFP in VDB.** Snapshot = 32 GIE + 80 LP = 112 trits. Node size = ceil(112/16) × 4 bytes × 2 masks = ... this is too large. 112 trits packed as (pos_mask, neg_mask) = 14 bytes per mask × 2 = 28 bytes for vector + 8 bytes graph = 36 bytes per node. 64 × 36 = 2,304 bytes. Fits in LP SRAM (4,400 free) but the INTERSECT_LOOP in the assembly needs to handle 7 words instead of 3. The assembly's dot product macro is unrolled for 3 words. Changing to 7 requires either re-unrolling or switching to a loop. This is a significant assembly change.

**Option B: Sign-only in VDB, MTFP for LP state.** VDB snapshots remain 48 trits (32 GIE + 16 LP signs). VDB search and retrieval are unchanged. The MTFP encoding is used only for agreement computation, gate bias, and LP Hamming measurement — all on the HP core. The VDB doesn't see MTFP at all.

**Recommendation: Option B.** The VDB's purpose is episodic retrieval for LP blend. The blend rule (agreement/gap-fill/conflict→HOLD) operates on single trits. MTFP magnitudes are not meaningful for the blend rule — you can't "fill a gap" in a magnitude exponent. The VDB should continue to store and match in sign-space. The MTFP representation is a parallel state used for downstream HP-core computations.

### Agreement Computation

Currently: `trit_dot(lp_now[16], tsign(lp_running_sum[p_hat][16]))`.

With MTFP: `trit_dot(lp_now_mtfp[80], tsign(lp_running_sum_mtfp[p_hat][80]))`.

The dot product is 80-dimensional instead of 16-dimensional. The agreement score has more resolution. The computation is 5× more trits but still trivial on the HP core (~1µs).

The running sum accumulators expand from `int16_t[4][16]` to `int16_t[4][80]`. Memory: 4 × 80 × 2 = 640 bytes. On the HP core stack — no LP SRAM cost.

### Gate Bias

Unchanged in mechanism. Agreement score is computed from the 80-trit MTFP state. Bias values are per-pattern-group `int8_t[4]`. The ISR reads them from BSS. No ISR changes.

### LP Hamming Metric

Currently: `trit_hamming(lp_mean_p1[16], lp_mean_p2[16])`.

With MTFP: `trit_hamming(lp_mean_p1_mtfp[80], lp_mean_p2_mtfp[80])`.

The maximum Hamming goes from 16 to 80. The P1-P2 separation that was 0-4/16 should become measurable at 10-30/80 (based on the magnitude differences observed in the diagnostic).

---

## What Doesn't Change

- **LP core assembly:** The CfC step, VDB search, VDB insert, CMD 1-5 dispatch, feedback blend — all unchanged. The LP core continues to compute in sign-space.
- **GIE / ISR:** Gate bias read, CfC blend, TriX classification — all unchanged.
- **VDB:** Node format, NSW graph, search, insert — all unchanged. Snapshots remain 48 trits.
- **MTFP21 timing encoding:** Already in place, operates independently.
- **W_f hidden = 0:** Structural guarantee preserved. No weight updates.

---

## Implementation Plan

### Phase 1: HP-Side MTFP Encoder (~30 lines)

```c
/* In gie_engine.c or geometry_cfc_freerun.c */
static void encode_lp_dot_mtfp(int dot, int8_t *out) {
    /* Trit 0: sign */
    out[0] = (dot > 0) ? 1 : (dot < 0) ? -1 : 0;
    
    int mag = (dot > 0) ? dot : -dot;
    
    /* Trits 1-2: magnitude exponent */
    /* Trits 3-4: mantissa */
    /* Scale table tuned to observed dot distribution */
    ...
}

/* After each CMD 5 step, read dots and encode: */
int32_t *dots_f = (int32_t *)ulp_addr(&ulp_lp_dots_f);
int8_t lp_mtfp[80];
for (int n = 0; n < 16; n++)
    encode_lp_dot_mtfp(dots_f[n], &lp_mtfp[n * 5]);
```

### Phase 2: Update Agreement Computation (~10 lines)

Replace 16-trit dot with 80-trit dot in the agreement calculation. Update running sum accumulators from `[4][16]` to `[4][80]`.

### Phase 3: Update LP Hamming Measurement (~5 lines)

Compute LP means and Hamming from 80-trit MTFP vectors. Report as n/80.

### Phase 4: Validate on Silicon

1. Check LP Hamming P1-P2: should increase from 0-4/16 to measurably higher in /80 space
2. Check classification accuracy: should remain ~96% (MTFP encoding is downstream of classification)
3. Check gate bias: agreement signal should have more resolution
4. 14/14 PASS under hardened criteria

---

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| LP assembly changes | None — Option B keeps LP core unchanged | HP-side only |
| VDB format change | None — Option B keeps VDB at 48 trits | Sign-space for VDB |
| Accumulator overflow | Low — int16_t holds ±32K, 5 trits/neuron | Monitor sum magnitudes |
| MTFP scale boundaries wrong | Low — tunable post-silicon | Start conservative, adjust |
| No improvement | Possible — if P1-P2 degenerate in magnitude too | Diagnostic already showed separation exists |

---

## The Principle

This is the third application of the same insight in one session:

1. **Timing encoding:** Thermometer → MTFP21 gap history. Classification accuracy 80% → 96%.
2. **LP dot encoding:** sign() → MTFP dot encoding. LP Hamming P1-P2 from 0-4/16 to measurable separation.
3. **General:** When a scalar quantization (thermometer, sign) collapses information that a downstream computation needs, replace it with a multi-trit encoding that preserves the structure.

The encoding is the computation. The neurons don't need to change. The weights don't need to change. The assembly doesn't need to change. The structural guarantees don't need to break. The trits just need to carry the information that's already there.

---

## Open Questions

1. **What are the right MTFP scale boundaries for dot magnitudes?** The observed range is [-13, +13] in the diagnostic. The theoretical range is [-48, +48]. Hardware may use a narrower effective range. Capture dot histograms across a full TEST 14 run to determine the empirical distribution before finalizing boundaries.

2. **Does the g-pathway (candidate dots) also need MTFP encoding?** Currently `sign(dot_g)` produces the candidate trit that UPDATE/INVERT selects. If the candidate magnitude carries pattern-specific information, encoding it as MTFP would further increase the LP state's discriminative power. But the g-pathway's role is to provide diverse candidate values, not to discriminate patterns. Start with f-pathway only.

3. **Should the LP blend rule change?** Currently the VDB blend operates on single trits (agreement/gap-fill/conflict→HOLD). If the LP state is 80 MTFP trits, the blend should still operate on the sign trit (trit 0 of each 5-trit group). The magnitude trits (1-4) are re-computed from the dot product on the next CfC step — they don't need blending. The blend is sign-space; the representation is MTFP-space.

4. **How does this interact with the wider-LP path from the Pillar 3 LMM?** If MTFP dot encoding resolves the P1-P2 degeneracy, wider LP (32 neurons) is unnecessary. If it doesn't (because the sign-space degeneracy is mirrored in magnitude-space for some neuron subsets), wider LP is the fallback. Test MTFP first — it's cheaper (no assembly changes, no VDB changes) and attacks the demonstrated bottleneck (sign quantization) directly.

---

*Thirteen commits. Twelve silicon runs. Four LMM cycles. One principle: the encoding is the computation.*
