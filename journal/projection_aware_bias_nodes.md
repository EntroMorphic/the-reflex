# Nodes of Interest: Projection-Aware Gate Bias

## Node 1: The Disc Metric Space Problem

Three candidates for computing discriminability:

**(a) Sign-space LP means.** `disc[n] = count of LP neurons k where W_f[k][n] ≠ 0 AND lp_mean_sign[p][k] differs across patterns.` Problem: sign-space collapses P1-P2. The disc score inherits the degeneracy. GIE neurons that separate P1/P2 in magnitude but not sign are scored as non-discriminative.

**(b) MTFP-space LP means.** Same computation but using the 80-trit MTFP means. Problem: the mapping from GIE neuron n to MTFP trit j is indirect (through the LP dot product and the MTFP encoder). A single GIE neuron influences all 5 MTFP trits of every LP neuron it connects to. The disc score would need to be: for each LP neuron k, does the 5-trit MTFP block [k*5..(k+1)*5-1] differ across patterns? This is feasible but more complex.

**(c) LP dot magnitudes directly.** For each GIE neuron n, for each LP neuron k where W_f[k][n] ≠ 0, compare `mean(|dot_f[k]|)` across patterns. Problem: requires accumulating per-LP-neuron, per-pattern dot magnitude histograms. Expensive in memory and computation. Overkill.

Tension: (a) is too coarse, (c) is too expensive, (b) is the right resolution but adds complexity.

## Node 2: The Init-Time vs Online Question

The LP weight matrices W_f are fixed at init. They never change. Any discriminability metric that depends ONLY on W_f can be computed once at init time and cached.

But discriminability that depends only on W_f is just connectivity — which LP neurons does GIE neuron n influence? That's not discriminability. It's fan-out.

True discriminability requires knowing which LP neurons carry pattern-specific information. That's only knowable after accumulating LP means — which happens online. So disc must be computed online, after the cold-start window.

Unless: we can predict from the W_f structure alone which neurons WILL be discriminative. The LP dot for neuron k under pattern p is `sum_j(W_f[k][j] * input_p[j])`. If two patterns p and q produce different inputs at position n (i.e., the GIE hidden state differs at position n for the two patterns), then W_f[k][n] ≠ 0 means LP neuron k's dot will differ. The question is whether the GIE hidden state differs at position n across patterns — and THAT depends on the TriX signatures, which are computed during Test 11's Phase 0.

So: disc[n] could be computed from the TriX signatures and W_f, at Test 11 completion time, before any LP accumulation.

`disc[n] = count of LP neurons k where W_f[k][n] ≠ 0 AND sig[p][...] implies gie_hidden[n] differs across patterns`

But the GIE hidden state is not the input. It's the CfC blend of the input through the GIE weights. The relationship between input signatures and GIE hidden state is complex (it depends on the GIE's W_f, W_g, and the blend history).

This is getting circular. The cleaner path is online computation after the cold-start window.

## Node 3: The Entrainment Risk

Per-group bias has 4 values. If one entrains, it entrains an 8-neuron block. Per-neuron bias has 32 values. If one entrains, it entrains a single neuron — which fires every loop, dominating the LP dot for every LP neuron it connects to.

The MTFP agreement entrainment was caused by high-resolution agreement creating a positive feedback loop. Per-neuron bias has the same risk: a neuron with high disc gets high bias → fires more → changes LP state → might increase agreement (if the fired neuron reinforces the mean) → higher bias → locks in.

Mitigation: the disc score is recomputed periodically, not locked in. If a neuron's firing changes the LP means so that it's no longer discriminative, disc drops, bias drops. The system self-corrects.

But: the disc recomputation uses the LP means, which are accumulated sums. They have inertia. A locked-in neuron's contribution accumulates into the mean for hundreds of steps before the mean shifts enough to change disc. The feedback loop has high latency but low damping.

## Node 4: The "Is This the Right Problem?" Question

The seed sweep shows 2/3 benefit, 1/3 regression. The mean effect is +0.6 Hamming points. The paper reports this honestly.

Per-neuron bias is an engineering optimization that might turn 2/3 into 3/3. But it adds:
- 32-byte volatile array (minor)
- disc computation logic (~20 lines)
- A new metric space decision (sign vs MTFP vs magnitude)
- A new potential failure mode (single-neuron entrainment)
- Complexity in the paper (Section 2.4 gets harder to explain)

The cost-benefit: if it works, the paper says "3/3 seeds benefit, per-neuron bias is the mechanism." If it fails (entrainment, wrong disc space, instability), we've added complexity with no gain and need to revert.

The alternative: report 2/3 honestly, note "per-neuron discriminability-weighted bias" as the predicted improvement, and leave it to the follow-up. The paper is already honest about the limitation. Adding a speculative fix that might not work makes the paper harder to review, not stronger.

## Node 5: What the Simplest Correct Version Looks Like

If we do build it, the simplest version that could work:

1. Use MTFP-space LP means for disc (they separate P1-P2 where sign-space doesn't)
2. disc[n] = count of LP neurons k where W_f[k][n] ≠ 0 AND `hamming(lp_mean_mtfp[p][k*5..(k+1)*5], lp_mean_mtfp[q][k*5..(k+1)*5]) > 0` for any pair (p,q)
3. Compute disc once after all patterns have ≥30 samples
4. Normalize to [0, 1]
5. bias_pn[n] = (int8_t)(BASE_GATE_BIAS * agreement * disc_norm[n])
6. Decay: bias_pn[n] *= 0.9 each confirmation (same as current per-group)
7. ISR reads bias_pn[n] instead of bias[n/8]

That's maybe 40 lines of HP code and 1 line of ISR change. The risk is manageable if we test with all 3 seeds.

But the MTFP LP means are only available on the HP core. The disc computation is HP-side only. The LP core doesn't know disc exists. Clean separation maintained.
