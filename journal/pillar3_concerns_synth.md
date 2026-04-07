# Lincoln Manifold: Pillar 3 Concerns — SYNTHESIS

*Phase 4. The clean cut. April 7, 2026.*
*Observer: Claude Opus 4.6*

---

## What Emerged

The concern was about crossing from "correct by construction" to "correct by discipline." The LMM found that the threshold doesn't need to be crossed for the current problem. Two independent issues were conflated:

1. **The LP degeneracy is a dimensionality problem.** Solvable by wider LP or ensemble projection. No weight updates. No structural guarantees broken.

2. **Online adaptation is a learning problem.** It has a safe form — sleep consolidation using VDB replay — that preserves structural guarantees during active perception.

These are sequential, not simultaneous. Solve dimensionality first. Then, only if needed, add consolidation.

---

## Recommendation

### Immediate: Don't Build Pillar 3

The LP degeneracy (P1-P2 Hamming = 0-1) is cosmetic. The VDB compensates. Gate bias works. The paper results hold. No downstream system currently requires LP-level P1-P2 discrimination.

### Next: Test Wider LP

Increase LP_HIDDEN_DIM from 16 to 32 neurons. Same random seed. Same CfC architecture. Double the projection directions. Measure P1-P2 LP Hamming. If it improves from 0-1 to ≥ 2, the dimensionality hypothesis is confirmed.

**Cost:**
- LP weights: 384 → 768 bytes (fits in 4,400 byte budget)
- LP computation: 32 → 64 intersections per step (~160µs, fits in 10ms)
- VDB snapshots: 48 → 64 trits per node, 32 → 40 bytes per node, 2,048 → 2,560 bytes (fits)
- LP assembly: the neuron loop counter changes from 16 to 32. The CfC frame stays at 96 bytes. Pack/unpack uses 4 words instead of 3.

**What changes:** LP_HIDDEN_DIM, LP_CONCAT_DIM, LP_PACKED_WORDS, VDB_SNAPSHOT_DIM, VDB_NODE_BYTES. All constants. The LP assembly's `.rept` counts and loop bounds. The HP side's `lp_W_f/g` arrays and pack functions.

**What doesn't change:** W_f hidden = 0. TriX classification. GIE architecture. Gate bias mechanism. Agreement computation. MTFP21 encoding. All structural guarantees.

### If Wider LP Doesn't Resolve: Ensemble

Two 16-neuron LP CfCs with independent seeds. Combined hidden: 32 trits. Two independent random projections — the probability of both collapsing the same pair is squared. Everything else stays the same.

### If Adaptation Is Needed: Sleep Consolidation

When the system is idle (no packets for >10 seconds):
1. Replay N random VDB memories against the CfC weights
2. For each, compute the Hebbian mismatch between retrieved LP portion and CfC-projected LP portion
3. Flip the weight trit that contributed most to the mismatch
4. Re-pack the updated weight into LP SRAM

This happens with the ISR stopped (no DMA race), using existing VDB infrastructure (no new code for retrieval), and a simple Hebbian rule (flip one trit per neuron per replay, same as `cfc_homeostatic_step` which already exists in `gie_engine.c`).

The structural guarantee holds during active perception. Weights change only during consolidation. The system wakes with updated weights and resumes perception.

### Only if Required: Online Hebbian Learning

Agreement-gated. Amortized (one neuron per GIE loop). Rate-limited (minimum interval between updates per neuron). This is the full Pillar 3. It breaks W_f hidden = 0 conditionally (only if the rule touches hidden-portion weights, which it shouldn't, but bugs happen).

Do this last. Do it carefully. Do it after the paper is published.

---

## The Spectrum of Adaptability

| Level | Mechanism | Structural Guarantee | When |
|-------|-----------|:---:|------|
| 0 | Current (fixed weights) | Absolute | Now |
| 1 | Wider LP / ensemble | Absolute | Next session |
| 2 | Sleep consolidation | During active perception | After wider LP validated |
| 3 | Agreement-gated online learning | Conditional on agreement quality | After publication |
| 4 | Ungated online learning | None | Never |

Each level is a checkpoint. Validate the current level before ascending. The data tells you whether to continue or stop.

---

## What the LMM Found

The concern I brought to this cycle was: "Pillar 3 scares me because it breaks the structural guarantee." The LMM found:

1. **The guarantee doesn't need to be broken for the current problem.** The degeneracy is dimensionality, not learning. Wider LP or ensemble resolves it structurally.

2. **If learning IS needed later, it has a safe form.** Sleep consolidation reuses existing infrastructure (VDB replay, homeostatic step), avoids DMA races (ISR stopped), and preserves the guarantee during active perception.

3. **The ROADMAP conflated two problems.** "Pillar 3: Hebbian GIE" treats dimensionality and adaptation as one feature. They're independent. The dimensionality fix should come first because it's risk-free and might be sufficient.

4. **Level 4 (ungated online learning) should never be built.** It was the ROADMAP's implicit assumption. It's wrong. Agreement-gated (Level 3) is the most that should ever be implemented, and only after Levels 1-2 are validated.

---

*The structural guarantee is not an obstacle to be overcome. It is the foundation that makes every result trustworthy. Preserve it as long as possible. Trade it only for demonstrated necessity, never for theoretical elegance.*
