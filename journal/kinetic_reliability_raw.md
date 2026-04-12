# Raw Thoughts: Why Kinetic Attention Is State-Dependent

## Stream of Consciousness

The trit-level data broke the mystery open. Run 2 (+4.2) and Run 3 (-1.0) are not "the same mechanism with different noise." They are the same mechanism operating on different attractor landscapes with opposite outcomes.

Run 2: all four LP patterns converge to the identical sign vector without bias. Every trit position has the same sign for every pattern. The LP is in a universal attractor — a fixed point of the CfC dynamics where the random weights project all four patterns' GIE hidden states into the same 16-trit output. Bias breaks this by changing which GIE neurons fire, which changes the 32-trit GIE hidden state, which gives the LP CfC a different input, which pushes each pattern's LP trajectory to a different fixed point. The bias is a symmetry-breaking perturbation that creates divergence from a degenerate baseline.

Run 3: the LP already has pattern-specific states without bias (Hamming 2 between some pairs). The random weights happen to project these specific patterns into distinguishable directions. Bias changes the GIE hidden state and the LP trajectory shifts to a DIFFERENT attractor that happens to be degenerate — P0/P1/P2 collapse to the same state. The bias destroyed divergence that the random weights had found.

The uncomfortable truth: with 16 LP neurons and 48-trit random ternary weights, the LP CfC is a low-dimensional random projection. It has 16 output trits, each determined by a 48-trit weight vector drawn from {-1, 0, +1} at 40% sparsity. That's ~29 non-zero weights per neuron. The projection quality (whether it separates specific patterns) is RANDOM — some seeds produce good projections for the P0-P3 set, others don't. The bias changes the effective projection by modifying which GIE neurons fire (changing the 32-trit input to the LP), but the new effective projection is ALSO random in quality.

There are 2^16 = 65,536 possible 16-trit sign vectors. With 4 patterns, we need 4 of them to be distinct. The probability that a random projection maps 4 specific inputs to 4 distinct sign vectors depends on how different the inputs are. With the GIE hidden states for P0-P3 being ~10-15 trits apart (based on earlier P0-P3 Hamming measurements), and the LP projection being random with 48 input trits, the chance that all 6 pairwise Hamming distances are ≥1 is... not high. Many trits will be borderline (LP dot product near zero) and their sign is effectively random.

The number of "confident" trits (where the LP dot magnitude is large enough that sign is robust across packets) is probably much less than 16. If only 4-6 trits are confident, the effective dimensionality of the LP representation is 4-6, and the maximum divergence is bounded by ~4-6 out of 16. The rest are noise trits that flip between +1 and -1 depending on per-packet variation.

This explains the 1.2/16 baseline. The VDB-only mechanism gives 1.2 because only ~2-4 trits are reliably pattern-specific across 120s of accumulation. The other 12-14 trits are noise. The sign-of-sum for noise trits converges to +1 or -1 somewhat randomly, contributing ~0-1 to pairwise Hamming by chance.

The kinetic attention mechanism changes which trits are confident (by changing which GIE neurons fire, which changes the LP dot products). But it doesn't guarantee the new confident trits are MORE pattern-discriminative. It's a random re-roll of which trits are confident, not a directed improvement.

This is why Hebbian learning ALSO failed (+0.1 ± 1.6). The Hebbian rule flips LP weights, which changes which trits are confident. But with 16 neurons and ~29 non-zero weights each, the learning has ~464 parameters and 16 outputs. The ratio of outputs to parameters is 1:29. There are many weight configurations that produce the same 16-trit sign vector. The learning can explore the weight space without finding a better projection because the output is heavily quantized (ternary sign) and the search landscape is flat (many equivalent configurations).

The bottleneck is not the mechanism. The bottleneck is the LP capacity:
- 16 neurons → 16 output trits → 16 bits of information max
- 48-trit input → 2^48 possible inputs, projected into 2^16 outputs → massive information loss
- Random ternary weights → the projection is not optimized for the input distribution
- sign() quantization → the projection loses magnitude information (the MTFP finding from earlier)

More LP neurons = more output trits = more potential for pattern-discriminative projections. More neurons also gives the Hebbian rule more parameters per output ratio (still ~29 weights per neuron, but more neurons means more outputs, which means more degrees of freedom for the learning to find useful structure).

But more neurons means more LP SRAM usage. Current: 16 neurons × 2 pathways × 48 trits × 2 masks = 768 bytes for weights + 32 bytes for hidden states. Doubling to 32 neurons: 1,536 bytes for weights + 64 bytes for hidden states. Current free stack: ~4,400 bytes. This fits, but it's getting tight.

And the LP assembly needs to be updated for a different hidden dimension. The CfC step loops over LP_HIDDEN_DIM. The VDB vectors would change dimension (currently 48 = 32 GIE + 16 LP → 64 = 32 GIE + 32 LP). The VDB node size grows from 32 to 40 bytes. VDB capacity drops from 64 to ~51 nodes.

This is a significant change. But it might be the right one.

## Questions Arising

- Is the LP capacity (16 neurons) truly the bottleneck, or would wider LP just shift the noise to a higher dimension without improving divergence?
- How many confident trits does the current 16-neuron LP actually produce? Can I measure this from the existing data (LP dot magnitudes)?
- Would MTFP encoding (5 trits per neuron, 80 output trits) give enough dimensionality without changing the LP neuron count? The MTFP finding from earlier showed P1-P2 separation of 5-10/80 in MTFP space vs 0-2/16 in sign-space.
- Is the problem actually about LP width, or is it about the INPUT to the LP? The GIE hidden state (32 trits) is computed from random CfC weights with a noisy wireless input. Maybe the GIE hidden itself isn't discriminative enough for ANY LP projection to work.
- What would it take to make the kinetic attention effect directionally guaranteed? The bias would need to know which trits of the GIE hidden are pattern-discriminative and selectively amplify those. But that requires knowing the projection structure — which is the thing Hebbian learning was supposed to learn.

## First Instincts

- The 16-neuron LP with random weights is near its information-theoretic ceiling for these 4 patterns under label-free conditions
- Wider LP (32 neurons) is the simplest test of whether capacity is the bottleneck
- MTFP encoding is an intermediate step that extracts more information from the existing 16 neurons without changing the assembly
- The kinetic attention variance is fundamental to the architecture with random LP weights — it's not fixable by tuning
- Hebbian learning might work on a wider LP where there are more degrees of freedom and the projection landscape is less flat
- The data is pointing toward: ship what works (100% classification, VDB temporal context, 1.2/16 baseline) and investigate wider LP as the next step-change
