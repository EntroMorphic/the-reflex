# Reflections: Kinetic Attention Reliability

## Core Insight

The 16-neuron LP with random ternary weights is a low-dimensional random projection that is at or near its information-theoretic ceiling for discriminating 4 wireless patterns under label-free conditions. The measured baseline (1.2/16 from VDB-only) represents the amount of pattern-discriminative information that survives the random projection AND the sign() quantization. The remaining 14.8/16 trits are noise or non-discriminative.

Both kinetic attention and Hebbian learning fail to reliably improve on this baseline because they operate on a system with insufficient degrees of freedom: kinetic attention re-rolls which trits are confident (random perturbation, not directed improvement), and Hebbian learning explores a flat weight landscape (16 quantized outputs, ~29 weights each, too many equivalent configurations).

The fix is more dimensions. Two paths exist:

1. **MTFP encoding** (HP-side, no assembly changes): extract 80 trits from 16 neurons instead of 16 trits. Preserves magnitude information. Requires VDB and feedback path changes but not LP core changes.

2. **Wider LP** (assembly rewrite): 32 neurons, 32 trits, richer input (96-trit), more confident projections. Higher engineering cost but structurally solves the dimensionality problem.

## Resolved Tensions

### Node 1 vs Node 3: Is the bias effect real?
Yes — the gate bias reliably changes per-group fire rates by >10% every run. The DYNAMICS change is real and measurable. What's unreliable is whether the changed dynamics produce more or less LP divergence. The bias is a real mechanism with a random effect on the metric we care about.

This isn't a failure of the bias mechanism — it's a failure of the metric to align with the mechanism. The bias changes GIE hidden state. The LP projects GIE hidden into 16 sign trits. Whether the changed GIE hidden maps to more-separated sign trits depends on the random LP projection. The bias is doing its job. The LP projection is the bottleneck.

### Node 2 vs Node 5: Sign-space ceiling vs MTFP resolution
The 1.2/16 baseline in sign-space is misleading. Earlier MTFP measurements showed 5-10/80 P1-P2 separation (from the Apr 6-7 sessions). If the LP MTFP divergence is 5-10/80 while sign-space is 0-2/16, then there IS more information in the 16 neurons than the sign-space metric captures. The ceiling is in the quantization, not in the projection.

MTFP encoding doesn't change the LP neurons — it changes how we READ them. The 16 dot products are already computed by the LP core (stored in `lp_dots_f`). The MTFP encoding is a post-hoc HP-side operation. If the VDB used MTFP representations instead of sign representations, the retrieval would be more discriminative, the blend would be finer, and the entire temporal context layer would operate at higher resolution.

This is the path of least resistance: more information from the same hardware, no assembly changes, no SRAM budget increase for the LP core.

### Node 6 vs Node 7: Wider LP vs better VDB
Both address dimensionality, but at different levels:
- Wider LP adds more projections (more neurons, more output dimensions)
- Better VDB (MTFP vectors, weighted search) extracts more from existing projections

MTFP first, wider LP second. MTFP is cheaper to implement and test. If MTFP-resolution VDB already lifts the baseline above 1.2/16 (measured in MTFP-space), the wider LP may not be needed yet.

## Hidden Assumptions Challenged

### "Kinetic attention should always improve divergence"
No. Kinetic attention improves divergence when the LP is stuck in a universal attractor (symmetry trap). It degrades divergence when the LP has already found pattern-specific states and the perturbation pushes it into a degenerate basin. The correct claim is: "kinetic attention breaks symmetry in degenerate LP states but may disrupt existing separation."

### "More neurons automatically means more divergence"
Not necessarily. With 32 neurons and random weights, there will be more confident trits (statistically), but also more noise trits. The RATIO of confident to noise trits depends on the input distribution and the weight statistics, not just the count. However, the absolute number of confident trits should increase, which gives more room for divergence and learning.

### "The Hebbian rule is fundamentally broken"
Not broken — underpowered. The rule (flip a weight that contributed to the error direction) is correct in principle. But with 16 outputs and ~29 weights per output, flipping one weight is a tiny perturbation in a flat landscape. With 32 or 80 output dimensions, the landscape has more structure and the same flip rule may have more targeted effects.

## What I Now Understand

The session's findings form a coherent picture:

1. **Classification is solved.** 100% label-free, 4 patterns, 430 Hz. The GIE with enrolled signatures discriminates perfectly from payload+timing features. This is real, verified, and durable.

2. **The temporal context layer works.** VDB-only (CMD 5) produces reliable 1.2/16 LP divergence. This is the system's baseline capability with 16 LP neurons and random weights.

3. **Kinetic attention is a symmetry-breaker, not a universal improver.** It helps when the LP is stuck (+4.2 when baseline is 0) and may hurt when the LP is already separated (-1.0 when baseline is 2). Mean +1.3, range -1.0 to +4.2. State-dependent.

4. **Hebbian learning is noise at this dimensionality.** +0.1 ± 1.6. The 16-trit output space with ~29 weights per neuron creates a flat optimization landscape where single-trit flips are random walks.

5. **The bottleneck is LP dimensionality.** 16 sign trits is not enough for reliable pattern-discriminative representation of 4 patterns. The existing 16 neurons contain more information (visible in MTFP space at 5-10/80) that the sign() quantization discards.

6. **The next step-change is MTFP-resolution VDB and feedback**, not more mechanisms on top of 16 sign trits. Extract the information that's already being computed. Then re-evaluate kinetic attention and Hebbian learning at the higher resolution.
