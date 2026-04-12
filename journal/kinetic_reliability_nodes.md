# Nodes of Interest: Kinetic Attention Reliability

## Node 1: Two Attractor Regimes
The LP CfC with random weights has (at least) two types of starting conditions:
- **Universal attractor** (Run 2): all patterns converge to the same sign vector. LP divergence = 0. The random projection collapses all four GIE hidden states to the same direction.
- **Pattern-specific attractors** (Run 3): different patterns reach different LP fixed points. LP divergence > 0. The random projection happened to separate them.

The bias perturbation has opposite effects in these two regimes. In the universal-attractor regime, ANY perturbation breaks symmetry and creates divergence (you can only go up from 0). In the pattern-specific regime, a perturbation may push the system into a different (degenerate) basin.

Why it matters: The kinetic attention effect is not +1.3 on average. It's +4.2 when the baseline is 0 (symmetry trap) and -1.0 when the baseline is 2 (already separated). The benefit is inversely correlated with the baseline quality. This means the mechanism is a floor-raiser, not a ceiling-raiser.

## Node 2: The 16-Neuron Ceiling
With 16 LP neurons, each producing a sign(dot) trit, the LP has 16 bits of output. But many of those bits are near-zero dots whose signs are noise. The number of "confident" trits — where |dot| is large enough that the sign is consistent across packets — might be only 4-6. This bounds the effective dimensionality and the achievable divergence.

Evidence: the VDB-only baseline (1.2/16 across 3 runs in the stable measurement) suggests ~1-2 reliably pattern-discriminative trit positions per pair, with the rest being noise. The maximum achievable divergence might be bounded by the number of confident trits, not by the mechanism.

Tension with Node 5: MTFP encoding preserves magnitude information and showed 5-10/80 P1-P2 separation (from earlier measurements) vs 0-2/16 in sign-space. This suggests there IS more information in the 16 neurons — it's just lost in the sign() quantization.

## Node 3: The Bias Is a Random Re-Roll, Not a Directed Improvement
The gate bias lowers the threshold for one neuron group, changing which GIE neurons fire, changing the 32-trit GIE hidden state, changing the LP input. The LP then projects this different input through the same random weights into a different 16-trit output.

Whether the new output has more divergence is RANDOM — it depends on how the random LP weights interact with the specific change in GIE hidden. The bias doesn't have access to the LP projection structure, so it can't know whether lowering group N's threshold will produce a more or less discriminative LP state.

This is the atomic cause of the variance: the bias reliably changes the dynamics but randomly changes the divergence.

## Node 4: Hebbian Learning Failed for the Same Reason
The Hebbian rule flips LP weights to reduce the error between current LP state and the TriX-accumulated target. But with 16 output trits and ~29 non-zero weights per neuron, the landscape is flat — many weight configurations produce the same sign vector. Flipping one weight (changing the dot by ±2) may cross zero for a near-threshold trit (changing its sign) or have no visible effect on a confident trit. The flips are random walks in a flat landscape.

With more neurons (more outputs), the landscape would have more structure — each output bit responds to fewer weight changes, so flips are more targeted.

## Node 5: MTFP Encoding Is the Intermediate Fix
The MTFP21 dot encoding (5 trits per neuron: sign + 2 exp + 2 mantissa) preserves the magnitude information that sign() discards. With 16 neurons × 5 trits = 80 output trits:
- More dimensions for divergence to emerge
- Near-zero dots are encoded as low-magnitude (exp=-1,-1) rather than random sign
- The VDB stores 80-trit LP states instead of 16-trit, giving the search more resolution
- The Hebbian target (TriX accumulator) has 80 dimensions instead of 16

This doesn't add neurons or change the LP assembly. It's an HP-side change: the MTFP encoding already exists (`encode_lp_dot_mtfp()` in `test_harness.h`). The VDB snapshot dimension would grow from 48 to 112 trits (32 GIE + 80 LP MTFP). Node size grows from 32 to ~56 bytes. VDB capacity drops from 64 to ~36 nodes.

BUT: the MTFP encoding was spec'd but never integrated into the VDB or the feedback mechanism. Currently the LP hidden (16 sign trits) is what goes into the VDB and what the Hebbian rule targets. Making the LP MTFP the representation requires changes to: VDB node format, VDB search, LP feedback blend, Hebbian target, and the divergence measurement.

## Node 6: Wider LP Is the Structural Fix
Doubling from 16 to 32 LP neurons:
- 32 sign trits → 32 bits of output → more dimensions for pattern separation
- 96-trit input (32 GIE + 32 LP hidden) → richer recurrence
- VDB vectors grow from 48 to 64 trits → more discriminative retrieval
- More confident trits (statistical: with 2× more projections, more will land far from zero)
- Hebbian rule has more targets (32 neurons) with the same weights-per-neuron ratio

LP SRAM cost: 16→32 neurons × 2 pathways × 96 trits × 2 masks = ~3,072 bytes for weights (up from 768). Hidden state: 32 bytes (up from 16). Dots: 128 bytes (up from 64). Total CfC state: ~3,968 bytes (up from 968). Free stack drops from ~4,400 to ~1,400. TIGHT but feasible if VDB scales down proportionally.

But this requires rewriting the LP assembly — the CfC loop count, VDB vector dimensions, weight packing, all change. Significant engineering effort.

## Node 7: The Real Question
Is the 16-neuron LP at its ceiling, or is the VDB mechanism undersupported?

Arguments for ceiling:
- 1.2/16 baseline is stable (±0.1 std in the 3-rep measurement)
- Kinetic attention can't reliably improve it (random re-roll)
- Hebbian learning can't reliably improve it (flat landscape)
- Both mechanisms fail for fundamentally different reasons that both reduce to "not enough dimensions"

Arguments against (VDB undersupported):
- VDB capacity (64 nodes, 48-trit vectors) may be insufficient for 4 patterns at 16-trit LP resolution
- With 16 LP trits and 4 patterns, the VDB is doing 4-way nearest-neighbor in a 48-dimensional ternary space — but 32 of those dimensions (GIE) are high-resolution and 16 (LP) are low-resolution. The search may be dominated by GIE similarity, not LP similarity
- Better VDB search (weighting LP trits more heavily than GIE trits in the distance) might improve LP divergence without changing the LP

This is testable: weight the LP portion of the VDB distance higher than the GIE portion and see if baseline divergence improves.
