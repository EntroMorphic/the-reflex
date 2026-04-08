# Raw Thoughts: Projection-Aware Gate Bias

*April 7, 2026. Motivated by seed sweep: gate bias helps 2/3 seeds, hurts 1/3.*

## Stream of Consciousness

The idea is clean on paper: weight each GIE neuron's bias by how much its LP projection contributes to pattern separation. Neurons that don't discriminate get zero bias. The noise disappears. Seed B's regression goes away.

But I need to be honest about what I don't know.

First concern: the discriminability score `disc[n]` depends on the LP running means. The LP means are the sign-of-sum accumulators — they converge over time. Early in the run (first 15 samples per pattern), the means are noisy. The cold-start guard prevents bias activation before 15 samples, but 15 samples might not be enough for the disc scores to stabilize. If disc is noisy, the per-neuron bias is random noise weighted by random noise.

Second concern: I'm computing disc from the sign-space LP means (`lp_mean[p][k]`), not from the MTFP means. The sign-space means are what the LP core actually operates on. But we just showed that sign-space collapses P1-P2. If the LP mean for P1 and P2 are identical in sign-space (Hamming 0), then disc[n] won't distinguish neurons that separate P1 from P2 — they'll all look non-discriminative because the means don't differ. The disc score inherits the sign-space degeneracy.

Should disc use MTFP means instead? The MTFP means DO separate P1-P2 (Hamming 7-9/80). Using MTFP disc would correctly identify neurons that carry P1-P2 magnitude information. But the MTFP means are 80-dimensional (5 trits per LP neuron), and the LP weight matrix W_f connects GIE neurons to LP neurons, not to MTFP trits. The mapping from "GIE neuron n" to "MTFP trit j" is indirect: GIE neuron n influences LP neuron k's dot, which gets encoded as 5 MTFP trits. The discriminability should be computed at the LP neuron level (does LP neuron k discriminate?), not at the MTFP trit level.

But at the LP neuron level in sign-space, P1 and P2 often don't discriminate (Hamming 0-1). So disc[n] for P1-P2 pairs would be near zero for all GIE neurons. The per-neuron bias wouldn't help with P1-P2 because the disc score can't see the separation.

Third option: compute disc from LP dot MAGNITUDES directly, not from the sign-of-sum means. For each pattern p, the mean LP dot magnitude for neuron k is `mean(|dot_f[k]|) over P-classified samples`. If P1 gives neuron k a mean magnitude of 5 and P2 gives it 13 (like the diagnostic showed), that's discriminative — even though the signs are the same.

This is more complex but more correct. The disc score becomes: for each GIE neuron n, how much does the LP dot magnitude distribution change across patterns in the LP neurons that n influences?

Fourth concern: this is getting complicated. The original gate bias was 4 values, dead simple, and it worked 2/3 of the time. The per-neuron bias is 32 values, requires disc computation from LP weight matrices and running means, and might not work if the disc score is computed in the wrong space. Am I over-engineering a solution to a problem that's really just "1 of 3 random projections is unlucky"?

Fifth concern: the MTFP agreement entrainment bug happened because we gave the agreement mechanism too much resolution. The higher-resolution signal created a positive feedback loop. Per-neuron bias is also higher resolution — 32 channels instead of 4. Could per-neuron bias create a more targeted entrainment? If one neuron has disc >> others, all the bias concentrates on it, making it fire constantly, which locks the LP state to whatever that neuron represents. A single-neuron lock-in.

Sixth concern: the ISR change is truly minimal (one array index). But I need to add `gie_gate_bias_pn[32]` as a volatile array in BSS. The ISR reads it. The HP core writes it. This is the same pattern as the current `gie_gate_bias[4]`. No race — the ISR reads atomically per-neuron, the HP core writes between classification events. But 32 bytes instead of 4. And the ISR now does a per-neuron lookup instead of a per-group lookup, which is actually faster (no division by 8).

What scares me: I'm solving a problem that might not be the right problem. The seed sweep showed gate bias helps 2/3 and hurts 1/3. Maybe the right response is not "make the bias smarter" but "accept that fixed random projections have a 1/3 failure rate, report it honestly, and let Pillar 3 (learning) fix it by adapting the projection." The per-neuron bias is a patch. Weight learning is the cure.

But Pillar 3 is a different research track. And we just said "ship then build." The per-neuron bias is a ship-time improvement that could strengthen the paper. Weight learning is a build-time feature that's a separate paper.

## Questions Arising

1. Should disc be computed from sign-space means, MTFP means, or LP dot magnitudes?
2. Does disc stabilize fast enough to be useful within the cold-start window (15 samples)?
3. Could per-neuron bias create single-neuron entrainment (a new failure mode)?
4. Is the complexity justified for a 1-in-3 failure rate?
5. Should the paper just report the honest 2/3 result and note per-neuron bias as future work?
6. Would it be cleaner to compute disc once at init (from the LP weight matrices alone, no running means) rather than online?

## First Instincts

- Compute disc from LP weight matrices alone (no running means needed)
- Use a simpler metric: disc[n] = number of LP neurons where W_f[k][n] ≠ 0 AND W_f[k][n] differs from at least one other pattern's typical input
- Actually, even simpler: disc[n] = count(W_f[k][n] ≠ 0 for k=0..15). Just connectivity. Neurons with more LP connections get more bias. Neurons with zero connections get zero.
- But that's not discriminability, that's just fan-out. A neuron connected to 10 LP neurons that all agree across patterns is not discriminative.
- I think the honest answer is: the current data doesn't support building this. We don't know which disc metric is right. We should report the 2/3 result and specify per-neuron bias as the predicted fix, to be validated in a follow-up.
