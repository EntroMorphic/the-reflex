# Raw Thoughts: Hebbian Learning from TriX Classifier Output

## Stream of Consciousness

The VDB mismatch error signal failed because it was indirectly label-informed. The fix is to use TriX — the structurally guaranteed, 100% accurate, genuinely label-free classifier — as the training signal instead. Let me think about what this actually means mechanically.

The current (broken) Hebbian flow:
1. CMD 5 runs: CfC → VDB search → retrieve best match → blend
2. Error = per-trit disagreement between retrieved LP portion and current LP hidden
3. For each LP neuron with error: flip a contributing W_f weight

The problem: the VDB node's LP portion was computed when the GIE hidden state contained pattern_id. So the "target" LP state is label-informed. When we remove the label, the VDB stores noisier states, and the error signal pushes weights in bad directions.

The proposed TriX-output flow:
1. CMD 5 runs: CfC → VDB search → retrieve → blend (unchanged)
2. TriX says "this is pattern P" (100% accurate, structural guarantee)
3. The LP should be producing the CANONICAL state for pattern P
4. Error = per-trit disagreement between current LP hidden and the canonical LP state for pattern P
5. For each LP neuron with error: flip a contributing W_f weight

Where does the "canonical LP state for pattern P" come from? It has to be accumulated from past LP states that TriX labeled as pattern P. This is a RUNNING MEAN — exactly what the test harness already computes for the divergence matrix. For each pattern P, maintain a sign-of-sum accumulator: `lp_target[P][16]`. Each time TriX says P, add the current LP hidden to the accumulator for P.

The target for Hebbian learning is then `tsign(lp_target[P][j])` — the accumulated sign vector for the predicted pattern. The error is: current LP hidden differs from the accumulated target.

This is different from VDB mismatch in a crucial way: the target is a POPULATION STATISTIC (the mean LP state for this pattern), not a SINGLE INSTANCE (one VDB node). Population statistics are noise-resistant. Single instances are noisy.

But wait — there's a bootstrapping problem. Early in the run, the accumulator has few samples, so the target is noisy. The learning could corrupt weights before the target stabilizes. Solution: gate on accumulator sample count — don't learn until the target has ≥N samples per pattern. N=50 would require ~50 confirmations per pattern = ~25 seconds of pattern exposure at ~2 confirms/s. Conservative but safe.

Another question: do I use TriX predictions or ground-truth labels for the accumulator? This matters because:
- TriX = 100% accurate (structural guarantee). Using TriX means the accumulator is label-free and structurally guaranteed to be correct. The system learns from its own perception.
- Ground truth = pattern_id from the packet. Using GT means the accumulator is label-informed. Same as the VDB leak.

For genuine label-free learning: USE TRIX PREDICTIONS ONLY. The accumulator for pattern P gets updated when `trix_pred == P`, not when `gt == P`. This way the entire learning pipeline is label-free: TriX classifies → accumulator updates → target computed → error derived → weights flipped. No ground truth touches the loop.

Now: what happens if TriX misclassifies? With the new distinct P2 payload + `MASK_PATTERN_ID_INPUT=1`, TriX is still 100% on the cycling sender. If it ever misclassifies, the wrong accumulator gets a wrong sample. At 100% accuracy, this never happens. At <100% (degraded conditions), the accumulator is contaminated proportionally to the error rate. But sign-of-sum is robust to minority contamination — the majority direction dominates.

The implementation:
1. Add `lp_hebbian_target[4][16]` (int16_t accumulators) — HP-side BSS. 64 bytes.
2. Each CMD 5 step: read TriX prediction, add current LP hidden to `lp_hebbian_target[trix_pred]`.
3. When gating conditions are met (retrieval stability + TriX agreement + rate limit + accumulator count ≥ N):
   a. Compute target = `tsign(lp_hebbian_target[trix_pred][j])` for each trit
   b. Compute error = `target[j] != lp_hidden[j]` for each trit
   c. For each LP neuron with error: flip a contributing W_f weight (same flip mechanism as before)
4. Repack weights to LP SRAM.

This is almost identical to the current `lp_hebbian_step()` except:
- The target comes from the TriX-labeled accumulator instead of the VDB best match
- The accumulator is a population mean, not a single instance
- No label leaks through the GIE hidden state because the TriX prediction is input-independent (W_f hidden = 0)

Wait — IS the TriX prediction input-independent? Let me check. TriX computes `argmax(sum(sig[p][i] * cfc.input[i]))` for each pattern p. The dot product uses `cfc.input[]` which, with `MASK_PATTERN_ID_INPUT=1`, has pattern_id trits zeroed. So the TriX score for each pattern depends on payload + timing + RSSI features only. The structural guarantee (W_f hidden = 0) means the classification is independent of the hidden state. So YES — TriX is genuinely label-free under `MASK_PATTERN_ID_INPUT=1`.

And the TriX prediction accuracy is 100% on the cycling sender with distinct P2 payload. So the accumulator will be clean.

The beauty of this design: the learning signal comes from the SAME classifier that provides the structural guarantee. The prior (LP weights) learns from the measurement (TriX classification), and the measurement is architecturally guaranteed to be independent of the prior. That's the prior-signal separation principle applied to the learning rule itself.

One concern: the TriX prediction is computed in the ISR at 430 Hz. The HP core reads `trix_pred` asynchronously. What if `trix_pred` changes between when the HP reads it and when the Hebbian update runs? This is the same race condition that exists for the current gate bias mechanism — the HP reads `trix_pred` during the feedback loop and it's already handled by the atomic read of the volatile. Not a new issue.

Another concern: the LP CfC step (CMD 5) updates `lp_hidden` AFTER reading `gie_hidden`. The `lp_hidden` we read on the HP side is post-CfC, post-VDB-blend. The TriX prediction was made from the PREVIOUS input (the one that was in `cfc.input[]` when the ISR ran). There's a one-step lag between the TriX prediction and the LP hidden state. Is this a problem?

Probably not. The TriX prediction changes at most once per pattern switch (every 5 seconds). The LP hidden state evolves every CMD 5 step (~10ms). So the TriX label is stable for ~500 LP steps. A one-step lag is negligible.

## Questions Arising

- Should the accumulator decay? If the LP weights update and the LP hidden states change, the old accumulator entries are from the old weights. A sign-of-sum accumulator doesn't decay — it just keeps growing. Maybe switch to an exponential moving average (EMA) instead? But EMA requires multiplication (alpha * old + (1-alpha) * new). Ternary alternative: count-and-threshold — for each trit, increment a counter when +1, decrement when -1. The sign of the counter is the target. Reset the counter periodically (every M steps) to prevent staleness.
- How many weight flips per update cycle? The current mechanism flips one per neuron. Is that enough? Too many? The ablation showed 111 flips across 42 updates for the (broken) mechanism. That's ~2.6 flips per update. Maybe allow up to 3 flips per neuron per update.
- Should the g-pathway weights also learn? The LMM synthesis said "v2" for g-pathway. For this iteration, keep it f-only and see if the TriX-based target fixes the label-dependency issue.
- What TEST 15 modifications are needed? Minimal — the test already measures Control vs Hebbian with clean phases. Just swap the error signal source from VDB mismatch to TriX accumulator.

## First Instincts

- The TriX-output-based target is cleaner than VDB mismatch and should work label-free
- The sign-of-sum accumulator is the right structure — robust to noise, ternary-native, already used throughout the project
- Gate on accumulator depth (≥50 samples per pattern) to prevent learning from noisy early targets
- Keep everything else from the existing implementation: f-only, one flip per neuron, stability gate, rate limit
- Test with MASK_PATTERN_ID=1 + MASK_PATTERN_ID_INPUT=1 from the start — no point validating with labels first
