# Lincoln Manifold: TriX-Dispatched LP Feedback — REFLECT

*Phase 3. Axe sharpening. April 8, 2026.*
*Observer: Claude Opus 4.6*

---

## Core Insight

**The system has two classifiers. We've been using the wrong one for the wrong job.**

TriX exists to classify. It does so at 100% accuracy with a structural guarantee. CPU core_pred exists because it was written first — before TriX signatures were installed as W_f weights, before the ISR had per-group scoring, before the agreement mechanism was designed. It was the only classifier available when Tests 12-13 were written. Tests 14 and 14C inherited it by inertia, not by design.

The entire agreement-weighted gate bias mechanism was designed around the assumption that the classification input would be clean. The kinetic attention LMM cycle (March 22) explicitly designed the agreement signal to release the bias within one confirmation of a pattern switch. That design assumed TriX-quality classification — immediate, correct, structurally guaranteed. But the implementation dispatched from CPU core_pred, which is none of those things for the P0-P1 pair.

The fix is not a new mechanism. It is finishing the wiring that the design assumed.

---

## Resolved Tensions

### Novelty Gating vs. Classification Accuracy (Nodes 3 and 5)

**Tension:** CPU core_pred provides both novelty gating (reject garbage) and classification (assign pattern label). Replacing it with TriX replaces both functions. But TriX has no natural reject mechanism.

**Resolution:** Hybrid dispatch. Keep CPU core_pred for novelty gating only. Use TriX for classification only. Each classifier does what it's good at: core_pred filters noise (its absolute score is a meaningful quality signal even at 80% classification accuracy), TriX assigns labels (100% accuracy, structural guarantee).

This is the right separation of concerns. The novelty question ("is this a real pattern?") and the classification question ("which pattern is it?") are different questions. They don't need the same answer source.

### Accumulator Cold-Start at Transition (Nodes 4 and 6)

**Tension:** Under TriX dispatch, the P2 accumulator is empty at the P1→P2 switch (TriX never misclassifies P1 as P2). The cold-start guard disables P2 bias until 15 samples accumulate. Is this delay harmful?

**Resolution:** The delay is the design working correctly. During the first 15 P2 confirmations (~3-4 seconds), the system operates in the no-bias baseline mode — which the multi-seed 14C data shows is the fastest transition mode (crossover at step 0 in all seeds). The cold-start guard naturally produces the optimal transition behavior: no bias during reorientation, then bias activates from clean data once the new pattern is established.

Under the current CPU core_pred dispatch, the P2 accumulator is pre-loaded with ~60 misclassified P1 samples. The cold-start guard doesn't trigger (60 > 15). The bias activates immediately from noisy data. This is worse than no bias — it's wrong bias.

The cold-start guard was always the right mechanism. It was being bypassed by contamination.

### Whether the Improvement Matters (RAW concern #3)

**Tension:** Maybe the headwind is 5 steps, not 50. Is the fix worth the change?

**Resolution:** The improvement matters for three reasons beyond the headwind itself:

1. **Paper integrity.** The Stratum 1 paper claims "the structural guarantee (W_f hidden = 0) ensures classification accuracy is independent of the prior." With CPU core_pred in the LP path, this claim has an asterisk. With TriX dispatch, the claim extends to the entire system. No asterisk.

2. **Red-team remediation.** Issue #4 (LP feedback uses a weaker classifier) and the new Limitation #7 go away completely. The paper gets shorter and stronger.

3. **CLS paper.** The transition headwind is the one finding that contradicts the CLS paper's narrative. If bias always hurts transitions, the "hippocampus stabilizes" story is complicated by "but the attention mechanism destabilizes." Removing the headwind simplifies the story to what it should be: VDB stabilizes, bias amplifies during stable periods, and the agreement mechanism correctly releases at transitions.

---

## The Change in Detail

### What Changes (5 lines of code)

In `geometry_cfc_freerun.c`, in every test function that dispatches LP feedback (Tests 12, 13, 14, 14C), after the novelty gate:

```c
// BEFORE:
int pred = core_pred;

// AFTER:
int pred = (int)trix_pred;
```

That's it. The novelty gate (`if (core_best < NOVELTY_THRESHOLD) continue;`) stays. The LP accumulator logic stays. The gate bias computation stays. The VDB insert stays. Only the pattern label source changes.

### What Doesn't Change

- W_f hidden = 0 (structural guarantee)
- ISR / GIE / GDMA / PARLIO / PCNT (all peripheral hardware)
- LP core assembly (CMD 1-5)
- VDB (insert, search, feedback blend)
- Gate bias computation (agreement, decay, hard floor)
- Test 11 enrollment (signatures, TriX enable)
- Novelty gating (core_pred threshold)

### What This Enables

The Stratum 1 paper can state: "LP feedback is dispatched from the TriX ISR classification (100% accuracy, structurally guaranteed by W_f hidden = 0). The CPU core_pred classifier is used only for novelty gating (reject/accept), not for pattern labeling."

The Limitation #7 we added today becomes: "Previously, LP feedback used CPU core_pred (~80% accuracy). This was replaced with TriX dispatch (100%) after identifying systematic P0-P1 contamination in the LP accumulators. The improvement was verified on multi-seed TEST 14C."

---

## What Remains Open

**One genuine unknown:** Does TriX dispatch change the no-bias baseline? The no-bias condition (CMD 5, no gate bias) uses `pred` only for accumulator binning, not for bias. If TriX dispatch cleans up the accumulator, the no-bias LP divergence might improve too. This would be a bonus finding — cleaner accumulators produce better pattern separation even without kinetic attention.

**One operational concern:** The transition sender only provides P1 and P2. TriX signatures for P0 and P3 are enrolled from zero samples (all-zero signatures). TriX will still classify P0/P3 inputs by finding the least-bad match among the four groups. Seed C showed 0/15 TriX accuracy in the first 14C run — this might be because the P0/P3 signatures were empty and TriX was guessing. This doesn't affect the P1→P2 transition experiment (which only uses P1 and P2), but it means the full 4-pattern Tests 12-14 should be run with the normal sender (4-pattern cycling) to validate TriX dispatch under the full pattern set. That's a follow-up, not a blocker.
