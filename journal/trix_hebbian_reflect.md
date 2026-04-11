# Reflections: TriX-Output-Based Hebbian Learning

## Core Insight

The fix is not a different learning rule — it's a different target. The Hebbian flip mechanism (find contributing weight, flip it) was fine. The VDB mismatch target was the problem: single-instance, label-infected, noisy. The TriX-accumulator target is population-level, structurally guaranteed, noise-resistant. Same flips, different signal.

This is the prior-signal separation principle applied to itself. The previous Hebbian implementation let the prior (LP state stored in VDB) drive the learning. The corrected implementation lets the measurement (TriX classification, structurally independent of LP state) drive the learning. The prior informs; the measurement decides. The same architecture, the same principle, one level deeper.

## Resolved Tensions

### Node 2 vs Node 4: Structural guarantee extends to label but not to target
Resolved by recognizing the two are different guarantees and both are sufficient:
- The LABEL is guaranteed correct (W_f hidden = 0 → TriX accuracy is LP-independent)
- The TARGET (accumulator) is NOT guaranteed correct (it depends on LP state which changes under learning)
- But the target doesn't need to be guaranteed correct — it needs to be directionally useful. The sign-of-sum converges to the majority direction. As long as the majority of LP states for a pattern are consistent (which they are — LP is deterministic given input), the target points in the right direction. Weight drift adds noise to the accumulator, but the noise is bounded by the flip rate (one per neuron per 100ms) and the accumulation rate overwhelms it (~120 samples per pattern per 60s).

### Node 3: Bootstrapping solved by Phase A warm-up
Phase A (60s baseline) doubles as accumulator warm-up. By t=60s, each pattern has ~120 samples (enough for stable sign-of-sum). The gate threshold (50 samples) is met well before Phase B starts. No structural change needed.

## Hidden Assumptions Challenged

### "The learning rule needs to be different"
No. The same single-trit flip rule works. The issue was the target, not the rule. The flip mechanism was correctly pushing weights toward producing outputs that match the target — it was the target that was wrong (label-infected VDB node). With a clean target (TriX accumulator), the same flip rule should push weights in the right direction.

### "Population targets require more memory"
Barely. `lp_hebbian_accum[4][16]` is 4 patterns × 16 trits × 2 bytes (int16_t) = 128 bytes. Plus a count per pattern: 4 × 4 = 16 bytes. Total: 144 bytes of HP BSS. Negligible.

### "The g-pathway must also learn for this to work"
Not necessarily. The f-pathway controls WHETHER a neuron fires. If the f-dot for a neuron is in the wrong direction (gate opens when it should hold, or vice versa), flipping a contributing weight corrects the gate. The g-pathway controls WHAT the neuron produces when it fires — that's a second-order effect. Fix the gate first (f-pathway only). If gate corrections alone produce positive contribution, that's sufficient for v1.

## What I Now Understand

The implementation is three changes:
1. Add `lp_hebbian_accum[4][16]` (int16_t) and `lp_hebbian_accum_n[4]` (int) to `gie_engine.c` or `test_hebbian.c`
2. In the Hebbian update path: replace `target[i] = decoded_vdb_node[i]` with `target[i] = tsign(lp_hebbian_accum[pred][i])`, gated on `lp_hebbian_accum_n[pred] >= 50`
3. In the CMD 5 feedback loop: after each TriX-confirmed step, add `lp_hidden[i]` to `lp_hebbian_accum[trix_pred][i]`

The test (TEST 15) needs no structural changes — it already runs Control vs Hebbian with clean 3-phase measurement. The comparison: Control post divergence vs Hebbian post divergence, both under `MASK_PATTERN_ID_INPUT=1`.

If the contribution is positive: the learning is genuinely label-free and the mechanism works.
If zero or negative: the LP projection fundamentally can't separate these patterns, or the f-only flip rule is insufficient, or 90s of learning isn't enough. Each failure mode has a distinct diagnostic signature.
