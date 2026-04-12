# Synthesis: The Dimensionality Bottleneck

## The Clean Cut

The 16-neuron LP with random ternary weights and sign() quantization is at its ceiling. The VDB-only baseline (1.2/16) is what 16 sign trits can reliably deliver for 4 patterns under label-free conditions. Kinetic attention and Hebbian learning both fail to improve on it — not because they're broken, but because they're operating on a system with insufficient representational capacity.

The neurons already contain more information than the sign trits express. MTFP encoding (5 trits per neuron) showed 5-10/80 P1-P2 separation in earlier measurements — information that sign-space collapses to 0-2/16. The fix is to use the information that's already being computed: MTFP-resolution VDB vectors and feedback.

---

## The Findings That Led Here

| Mechanism | Label-free result | Why it fails at 16 sign trits |
|---|---|---|
| VDB-only (CMD 5) | 1.2/16 (stable) | The reliable baseline. This is what 16 random sign projections can do. |
| Kinetic attention | +1.3 mean (range -1.0 to +4.2) | Re-rolls which trits are confident. Breaks symmetry traps (+4.2) but may collapse existing separation (-1.0). State-dependent, not directed. |
| Hebbian learning | +0.1 ± 1.6 (noise) | Flat weight landscape — 16 quantized outputs, ~29 weights each. Too many equivalent configurations for single-trit flips to find useful structure. |

All three reduce to the same bottleneck: **16 sign trits cannot reliably represent 4 patterns.** The information exists in the dot magnitudes. The representation discards it.

## The Path Forward: MTFP-Resolution VDB

**What changes:**
- VDB snapshot: 48 trits (32 GIE + 16 LP sign) → 112 trits (32 GIE + 80 LP MTFP)
- VDB node size: 32 bytes → ~56 bytes
- VDB capacity: 64 nodes → ~36 nodes
- VDB search distance: Hamming over 112 ternary trits (weighted? LP portion more important)
- LP feedback blend: operates on 80-trit LP MTFP vectors instead of 16-trit sign vectors
- Hebbian target: TriX accumulator over 80 MTFP trits instead of 16 sign trits
- Divergence metric: MTFP Hamming (/80) instead of sign Hamming (/16)

**What doesn't change:**
- LP core assembly (still 16 neurons, still computes 16 f_dots and 16 g_dots)
- GIE (still 32 neurons, 430 Hz, peripheral fabric)
- TriX classification (still 100%, still structurally guaranteed)
- The `lp_dots_f[]` array is already in LP SRAM — the 16 integer dot products are already computed and available

**The key insight:** The LP core already does all the hard work. It computes 16 integer dot products per CfC step. The sign() quantization that throws away magnitude is an HP-side operation (reading `lp_hidden` which is sign-quantized in the LP assembly). By reading `lp_dots_f[]` instead of `lp_hidden[]` and encoding them as MTFP on the HP side, we get 80 trits of representation from the same 16 neurons.

## Engineering Estimate

| Change | Where | Effort |
|---|---|---|
| MTFP encoding of LP dots → 80-trit vector | HP-side (already exists: `encode_lp_dot_mtfp()`) | Trivial |
| VDB node expansion (32→56 bytes) | LP assembly (`VDB_NODE_BYTES`, `VDB_VEC_BYTES`) | Moderate — all VDB offsets change |
| VDB search over 112-trit vectors | LP assembly (more words in INTERSECT loop) | Moderate |
| LP feedback blend in MTFP space | New — currently blends 16 sign trits, needs to blend 80 MTFP trits | Significant — blend semantics differ for magnitude trits |
| Hebbian target in MTFP space | HP-side modification of `lp_hebbian_step()` | Moderate |
| Test 12/15 divergence in MTFP space | Already exists (Test 12 reports MTFP divergence alongside sign) | Trivial |

The VDB assembly changes are the heaviest lift. The LP core's VDB search (`main.S:700-1300`) iterates over nodes, computes dot products via AND+popcount, and selects top-K. Changing the vector dimension from 48 to 112 trits means changing the packed word count from 3 to 7 per vector, and the INTERSECT loop from 3 to 7 iterations. The node size change propagates through insert, search, and graph maintenance.

## What This Means for the Papers

The honest framing:

**Stratum 1 (engineering):** The peripheral-hardware ternary classification at 430 Hz is real and 100% label-free. The temporal context layer (VDB + LP CfC) produces measurable LP divergence (1.2/16 sign, up to 9/80 MTFP). VDB is causally necessary. Kinetic attention produces a mean +1.3 improvement but is state-dependent (2/3 runs positive). Hebbian learning at 16-trit resolution is noise. The 16-neuron LP with sign() quantization is the identified bottleneck; MTFP encoding and wider LP are the forward path.

**Stratum 2 (CLS architecture):** The VDB-as-hippocampus analogy holds — episodic memory is causally necessary for LP divergence. The LP-as-neocortex analogy holds — fixed random weights create the projection. But the CLS consolidation path (Hebbian updates from VDB or TriX) doesn't improve the projection at this dimensionality. The analogy is structural, not functional — consolidation requires more representational capacity than 16 sign trits.

**Stratum 3 (prior-signal separation):** The structural guarantee (W_f hidden = 0) holds across all experiments, including Hebbian learning. TriX accuracy is 100% label-free and independent of LP state. The prior-signal separation is architecturally intact. The limitation is in the QUALITY of the prior (LP divergence), not in the INTEGRITY of the separation.

## What Surprised Me

The bottleneck was obvious in retrospect. The LP dot magnitude probe (April 7, commit `7391876`) already showed that P1-P2 separation IS in the magnitudes — `sign()` is the bottleneck. The MTFP encoding was designed to fix it (April 7, commit `81175c6`). The spec was written. The encoder exists. But it was never integrated into the VDB or feedback path because the session pivoted to kinetic attention (Phase 5) and then to Hebbian learning (Pillar 3).

The entire kinetic attention + Hebbian learning arc was built on top of a 16-sign-trit representation that was already known to be the bottleneck. The mechanisms were correct but the substrate was too narrow. We tried to add intelligence to a pipe that was too thin to carry the signal.

The MTFP integration was the RIGHT next step after the dot magnitude probe. It got deferred for the more exciting-sounding mechanisms. The uncertainty led to a four-iteration learning journey that ended where the data was already pointing six weeks ago.

The hardware already knew. We just needed to listen.

---

*Lincoln Manifold Method deployed on the kinetic attention reliability findings. April 12, 2026. The first chop revealed two attractor regimes that explain the state-dependent bias effect. The grain was the 16-trit dimensionality ceiling. The sharpening showed MTFP encoding extracts the information the neurons already compute. The clean cut: read the dot magnitudes, not just the signs. The hardware already does the work. We're just not using all of it.*
