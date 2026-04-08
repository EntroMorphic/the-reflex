# Lincoln Manifold: TriX-Dispatched LP Feedback — RAW

*Phase 1. Unfiltered. April 8, 2026.*
*Observer: Claude Opus 4.6*

---

## The Problem as I Understand It

The LP feedback path dispatches CMD 5 (CfC + VDB search + feedback blend) based on CPU core_pred classification — an HP-side argmax over signature dot products. This classifier runs at ~80% accuracy with systematic P0-P1 confusion (73% cross-dot ratio). The agreement signal that drives gate bias is computed from the LP accumulator, which is built from these contaminated dispatches.

The gate bias hurts transitions in all 3 seeds of the multi-seed TEST 14C we just ran. Full condition (CMD5+bias) crosses later than no-bias (CMD5) in every case: steps 18/0/7 vs 0/0/0. The bias consistently creates a headwind during P1→P2 switches.

The hypothesis: the headwind exists because the LP accumulator for P1 is contaminated with P2 samples (and vice versa) due to CPU core_pred misclassification. The agreement signal reads a noisy accumulator, computes a noisy bias, and that noise becomes a headwind during transitions when the prior should be releasing cleanly.

---

## Why TriX Fixes This

TriX runs in the ISR at 430 Hz. It's 100% accurate. It's structurally decoupled from the prior (W_f hidden = 0). When the sender switches from P1 to P2, TriX correctly classifies P2 on the very first packet. There is no 20% contamination. There is no systematic P0-P1 confusion.

If LP feedback is dispatched based on TriX prediction instead of CPU core_pred, the LP accumulator is clean. The agreement signal is clean. The bias releases on the first post-switch confirmation because the accumulator isn't polluted with P1 residue.

The 0.9 decay rate becomes appropriate again. It was designed for clean input. We've been feeding it dirty input and blaming the decay rate.

---

## What Scares Me

Two things.

**Timing mismatch.** TriX runs in the ISR at 430 Hz. LP feedback (CMD 5) runs at ~4 Hz (per-packet). The ISR produces a new trix_pred every ~2.3ms. The HP core dispatches LP feedback every ~250ms. If I dispatch based on trix_pred, which of the ~100 TriX predictions between packets do I use? The most recent one? The majority vote? The one that was active when the packet arrived?

The existing code reads trix_pred as a volatile int32_t. It's the most recent ISR classification at the moment the HP core reads it. Between packets, TriX is re-classifying the same input (the GIE is free-running on the same DMA descriptors). So trix_pred should be stable — it's the same input, same signatures, same scores. But what about the transition moment? When a new packet arrives and update_gie_input() changes the DMA buffers, there's a brief period where the ISR might classify a partially-updated input. Could produce a transient wrong prediction.

Actually, looking at the code more carefully: gie_input_pending is set, the ISR re-encodes the hidden portion, and trix_pred is updated after the full loop completes. The ISR processes the full chain atomically (one GDMA loop = one classification). So trix_pred is always based on a complete input. The transient concern doesn't apply.

**Loss of the novelty gate.** CPU core_pred uses a novelty threshold (NOVELTY_THRESHOLD=60) — if the best signature dot product is below 60, the packet is rejected. This filters out garbage packets (noise, partial packets, wrong-sender). TriX doesn't have an explicit novelty gate — it always produces a prediction. If I dispatch based on TriX alone, I lose the novelty filter.

But wait — TriX has an implicit quality signal: trix_confidence (the margin between the best and second-best pattern scores). A high-confidence TriX prediction with a wide margin is a strong signal. A low-confidence prediction with a narrow margin is weak. I could gate LP dispatch on trix_confidence instead of core_pred novelty. This is arguably a better gate because it measures discrimination quality, not just absolute match strength.

---

## The Actual Change Is Small

Looking at the code paths:

1. In Tests 12-14, after each confirmed packet, the HP core does:
   - CPU core_pred = argmax(dot(input, sig[p]))
   - If core_pred score < NOVELTY_THRESHOLD: skip
   - Else: dispatch CMD 5 with pred = core_pred

2. The change:
   - Read trix_pred from ISR (volatile, already available)
   - Read trix_confidence from ISR (volatile, already available)
   - If trix_confidence < some threshold: skip
   - Else: dispatch CMD 5 with pred = trix_pred

The LP accumulator, gate bias computation, VDB insert — all downstream code just uses `pred`. Swapping what feeds `pred` is a 5-line change.

---

## What About the Signatures?

TriX signatures are enrolled during Test 11, Phase 0a-0c. They're installed as W_f weights. The ISR uses those weights to classify. CPU core_pred uses the same sig[4] arrays. The signatures are identical. The difference is *how* the dot product is computed and thresholded:

- TriX: hardware dot product via GDMA→PARLIO→PCNT, gated by gate_threshold (default 90). Accumulates per-group, takes argmax.
- CPU core_pred: software scalar dot product, gated by NOVELTY_THRESHOLD (60). Takes argmax over all 128 input dimensions.

The TriX dot products are per-neuron, then summed per group. The CPU dot products are per-pattern (all 128 trits at once). They should agree on well-separated patterns and may disagree on marginal cases.

The current data says they DO agree: TriX ISR vs CPU reference agreement is 100% across all confirmed classifications (commit 0b09f69). But that was measured on confirmed packets only (core_pred score >= 60). For packets that core_pred rejects (score < 60), we don't know what TriX would say. Those might be the noisy packets that TriX would also classify with low confidence.

---

## The Deeper Question

Is the 80% core_pred accuracy actually causing the transition headwind? Or is it the bias decay rate? Or the agreement computation itself?

The multi-seed 14C data shows: no-bias (CMD 5, same 80% core_pred) transitions perfectly — P2 > P1 from step 0 in every seed. So core_pred contamination alone doesn't prevent transition. The contamination interacts with the bias mechanism to create the headwind.

Here's my model: The LP accumulator for P1 has some P2 contamination (from CPU core_pred misclassification during the P1 phase). This makes the P1 accumulator look slightly more like P2 than it should. When the switch happens, the agreement between lp_now (moving toward P2) and sign(P1_accumulator) (slightly P2-contaminated) stays positive for longer than it should — because they're both P2-ish. The bias doesn't release because the agreement signal is artificially elevated by cross-contamination.

With clean TriX dispatch, the P1 accumulator contains only true P1 samples. The agreement between lp_now (moving toward P2) and sign(P1_accumulator) (clean P1) drops sharply — they're genuinely different. The bias releases faster.

This is testable. If the theory is right, TriX-dispatched runs should show:
1. Cleaner LP accumulators (sharper sign-of-sum vectors)
2. Faster agreement decay at transition
3. Faster bias release
4. Faster or equal crossover vs no-bias condition

---

## What I Don't Know

1. Whether trix_confidence is a sufficient novelty gate, or if some garbage packets will slip through
2. Whether the enrollment window (30s with transition sender = only P1/P2 seen) produces TriX signatures good enough for all patterns, or if P0/P3 classification degrades
3. Whether the improvement is large enough to matter — maybe the headwind is 5 steps, not 50, and the fix makes it 2 steps instead of 5. Interesting but not publication-worthy.

The third one is the real question. The 14C data shows Full condition crosses at step 18 (Seed A), 0 (Seed B), 7 (Seed C) vs no-bias at 0, 0, 0. The headwind is real but variable. If TriX dispatch eliminates the headwind completely, all three seeds should show crossover at step 0 under the Full condition — matching no-bias.
