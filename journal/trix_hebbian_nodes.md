# Nodes of Interest: TriX-Output-Based Hebbian Learning

## Node 1: Population Target vs Single-Instance Target
The VDB mismatch error signal was a SINGLE INSTANCE — one stored node. Per-packet noise propagated directly into the error. The TriX-accumulator target is a POPULATION STATISTIC — the sign-of-sum over all LP states the classifier labeled as pattern P. Population statistics are noise-resistant by construction. The sign-of-sum converges to the majority direction; minority-class contamination (misclassification) is absorbed.
Why it matters: This is the core fix. The VDB error was noisy because it depended on one retrieval. The TriX error is smooth because it depends on hundreds of accumulated samples.

## Node 2: The Structural Guarantee Extends to the Learning Signal
TriX accuracy is architecturally guaranteed (W_f hidden = 0). The LP weights being updated cannot influence the TriX classification that drives the learning. This means: no feedback loop between the learned state and the training signal. The prior (LP weights) learns from the measurement (TriX output), and the measurement is structurally immune to the prior. This is the prior-signal separation principle applied to the learning rule itself.
Tension with Node 4: The structural guarantee holds for the TriX PREDICTION. But the ACCUMULATOR (which turns predictions into targets) is computed on the HP core and does depend on LP hidden state. If LP weights change and LP states drift, the accumulator is a mix of old and new representations. The structural guarantee protects the label but not the target.

## Node 3: Bootstrapping and the Accumulator Cold-Start
Early in the run, each pattern's accumulator has few samples. The sign-of-sum for a trit that's seen [+1, -1, +1] has sign +1, but with n=3 this is noise. Learning from a noisy target corrupts weights. Gate: require ≥50 samples per pattern before any learning occurs. At ~2 confirms/s and 4 patterns × 5s each, 50 samples per pattern takes ~100s of pattern exposure = ~400s of cycling. Phase A (baseline) is 60s. Phase B (learning) starts at 60s. So the accumulator gate won't fire until ~400s into the run, which is well into Phase B. This means Phase B needs to be longer, or the Phase A baseline needs to double as accumulator warm-up.
Resolution: Phase A accumulates both the divergence baseline AND the Hebbian target. By the time Phase B starts at 60s, each pattern has ~30 samples (120s into cycling, ~60s per pattern per cycle... no, 5s per pattern × 12 cycles = 60s per pattern). 60/5 = 12 cycles, 12 × 5s × ~2/s = ~120 samples per pattern. More than enough. Gate threshold of 50 is met early in Phase B.

## Node 4: Accumulator Staleness Under Weight Drift
When LP weights update, the LP hidden state trajectory changes. The accumulator was built from states produced by old weights. The new states may not match the old accumulator. In sign-of-sum terms: if a trit that was consistently +1 under old weights becomes consistently -1 under new weights, the accumulator still says +1 (from the old samples) until enough new samples overwhelm the old.
Resolution options:
(a) Periodic reset: zero the accumulator every M steps, rebuilding from scratch with current weights. Simple but loses information.
(b) Exponential decay: weight recent samples more than old. Requires multiplication (alpha blending). Not ternary-friendly.
(c) Sliding window: accumulate only the last N samples. Bounded staleness.
(d) Do nothing — sign-of-sum is self-correcting as new samples accumulate. At ~120 samples per pattern per 60s, old samples are a minority within 30s.
Recommendation: (d) for v1. The accumulation rate is fast enough that staleness self-corrects. If weight drift is dramatic (which it shouldn't be with rate-limited single-trit flips), try (a).

## Node 5: Implementation Diff from Current lp_hebbian_step()
The change is surgical: replace the VDB-mismatch target extraction with the TriX-accumulator target.

Current:
```c
// Read VDB best match LP portion → decode 16 target trits
volatile uint32_t *nodes = ulp_addr(&ulp_vdb_nodes);
uint32_t target_pos = nodes[node_word_off + 2];
```

New:
```c
// Read TriX accumulator → sign of sum for predicted pattern
int pred = trix_pred;
for (int i = 0; i < LP_HIDDEN_DIM; i++)
    target[i] = tsign(lp_hebbian_accum[pred][i]);
```

Everything downstream (error comparison, weight flip, repack) is unchanged. The gating logic adds one condition (accumulator depth ≥ 50) but otherwise stays the same.

## Node 6: What This Tests
If TriX-output-based Hebbian produces positive contribution under `MASK_PATTERN_ID_INPUT=1`:
- The learning is genuinely label-free (TriX labels from payload/timing, not from pattern_id)
- The population target is noise-resistant (no single-instance noise)
- The structural guarantee extends to the learning loop

If it produces zero or negative contribution:
- The LP projection is fundamentally unable to separate these patterns regardless of weight tuning
- OR the Hebbian flip rule (same-direction weight → flip) is wrong for this error structure
- OR the f-pathway-only update is insufficient (need g-pathway too)
