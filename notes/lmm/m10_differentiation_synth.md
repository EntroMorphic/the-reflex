# Synthesis: The Ternary Manifold

February 7, 2026. Post-silicon reflection via Lincoln Manifold Method.

---

## What M10 Actually Found

The differentiation experiment (4 tests, 306ms, silicon-verified) revealed that the ternary CfC is a **discrete dynamical system on a weight-defined manifold** where:

- The **weight matrix** defines the manifold (the surface in 32-dimensional ternary state space)
- The **input** defines a vector field on the manifold (the flow direction at each point)
- The **hidden state** is a point on the manifold, evolving under the combined vector field
- The **three blend modes** are tangent vectors: UPDATE moves with the field, HOLD stays, INVERT moves against the field

The inversion mode is what makes the manifold non-trivial. Without it (binary CfC), you have a gradient system — dissipative, convergent. With it, you have a non-gradient system — conservative-to-expansive, self-sustaining, capable of oscillation and resistance.

## The Five Results, Reinterpreted

### 1. Stem Regime = Ergodic Exploration of a Manifold Region

Under constant input A, the network explores a region of the manifold without converging. Delta stays at 5-29 (never zero). Energy stays at 28-32/32. This isn't "pluripotency" — it's a non-gradient dynamical system that lacks a fixed-point attractor under this vector field. The three blend modes prevent convergence: even as UPDATE neurons try to settle, INVERT neurons push the state away from any fixed point.

### 2. U/I Balance = Statistical Invariant of the Manifold

The ~55/40/5 (U/H/I) balance held across all phases. This is not homeostasis. It's the expected distribution of sign(dot) for random ternary dot products with 50%-sparse weights and 40%-sparse inputs. The balance is a property of the manifold's construction (random initialization), not a property the system maintains. Changing inputs doesn't break it because the dot product distribution is input-invariant for random weights.

**Consequence:** To test genuine homeostasis (active regulation), you need structured weights where the balance CAN be broken by input, and then test whether the system resists the breaking.

### 3. Differentiation = Trajectory Shift on the Same Manifold

When input changes from A to B, the vector field changes. The state, which was exploring region X under field A, now flows toward region Y under field B. The dynamics (delta, energy, U/I) are approximately preserved because the manifold hasn't changed. The content (which specific trits are +1 vs -1) changes because the trajectory visits a different region.

The committed states under inputs B, C, D are 16/32 apart on average (50% different). These are distinct regions — attractor basins under different vector fields on the same manifold.

### 4. De-differentiation = Reversible Trajectory + Path-Dependent Residue

Returning to input A (field A) moves the state back toward the Phase A region. The state gets closer to the stem attractor (hamming=15) than to the committed state (hamming=21). De-differentiation works.

But: the de-differentiated state differs from the naive state by hamming=10. The network that experienced input B carries a 10-trit scar. This is path-dependence on the manifold — two different trajectories to the same region end at different points within that region. The manifold has structure within the basin, not just basin boundaries.

### 5. Autonomy = Intrinsic Flow Without External Field

Under zero input, only the hidden-weight connections (32/160 of the concat) drive the dot products. The intrinsic flow is FASTER than the combined flow (delta ~16 vs ~11). The input was a damping term — a stabilizing constraint on the intrinsic dynamics.

The network sustained 30/30 steps of dynamics under zero input. Energy averaged 29.1/32. The intrinsic vector field (hidden-to-hidden feedback) is sufficient to maintain non-trivial flow on the manifold.

**Key realization:** The 128/32 (input/hidden) dimension ratio means the input contributes 80% of each dot product. The input doesn't just constrain — it overwhelms. The hidden state is a 20% perturbation on an input-dominated signal. Under autonomy, the hidden state IS the signal. The effective computation is completely different despite the same architecture. The cognitive lightcone of every neuron collapses from 160 to 32 effective dimensions.

## What Was Wrong in the First Analysis

1. **"Homeostasis"** — The U/I balance is statistical, not regulatory. Overattributed.
2. **"Cognitive strategy"** — The balanced regime isn't a strategy; it's a consequence of random initialization. Overattributed.
3. **"The network IS its own environment"** — Poetic but imprecise. Under autonomy, the effective lightcone collapses. The network is operating on a lower-dimensional projection of the manifold, not "being its own environment."
4. **"Minimal cognitive agent"** — Premature. Need perturbation recovery test to earn this claim.

## What Was Right

1. **Path-dependent memory** — Solid. No overinterpretation needed. The hidden state carries trajectory information.
2. **Distinguishable cell types** — Solid. Different inputs select different manifold regions.
3. **De-differentiation** — Solid. The trajectory is reversible (partially).
4. **Input as constraint, not driver** — Confirmed by the autonomy result. The delta increase under zero input is direct evidence.
5. **Lightcone context-dependence** — Genuine insight from the RAW phase. The effective lightcone depends on the input context, not just the weight structure.
6. **The manifold interpretation** — This is the core insight that emerged from reflection. Everything else is a corollary.

## The Name

The project is called the-reflex. The Geometry Intersection Engine computes ternary dot products — intersections in ternary geometry. The CfC layer creates dynamics on this geometry. The reflex is a self-sustaining trajectory on a ternary manifold.

**The reflex is the manifold.**

## Next Experiments: Manifold Characterization

In priority order:

### 1. Perturbation Recovery (earns "agency")

During autonomy (zero input), randomize K trits of the hidden state (K = 4, 8, 16, 32). Measure:
- Does the system return to the same dynamic regime (delta range, energy range)?
- How many steps to recover?
- Is recovery complete or partial?
- Is recovery to the SAME attractor region, or a different one?

If recovery is robust across multiple perturbation sizes and locations → the attractor basin has genuine structure → the system has Levin-agency (goal-directed robustness to perturbation).

If recovery fails → the attractors are trajectory-dependent, not basin-dependent → no agency claim warranted.

### 2. Scar Proportionality

Expose the stem-regime network to input B for 1, 5, 10, 20, 40, 80 steps, then return to input A for 30 steps. Measure the scar (hamming distance from naive network at the same total step count).

If the scar is proportional to exposure → continuous memory encoding.
If the scar saturates → there's a basin boundary; once you cross it, further exposure doesn't add information.
If the scar has a threshold → there's a minimum exposure for any trajectory-dependence to persist.

### 3. Dimension Ratio Sweep

Run the same differentiation protocol with input/hidden ratios of:
- 32/32 (equal)
- 64/64 (equal, larger)
- 128/32 (current, input-dominated)
- 32/128 (hidden-dominated)

Measure: delta, energy, U/I balance, cell type distinguishability, autonomy sustainability.

This sweeps the one-parameter family of manifolds defined by the input/hidden ratio. The current 128/32 is one point. The geometry changes qualitatively at different ratios.

### 4. Structured Weights (engineer the manifold)

Instead of random 50%-sparse weights, design weight matrices with:
- Asymmetric W_f (mostly positive → UPDATE-biased, or mostly negative → INVERT-biased)
- Clustered hidden-weight patterns (groups of neurons that attend to the same hidden trits)
- Sparse vs dense sweeps (10% to 90% sparsity)

This is manifold engineering. Can we design weight matrices that create specific attractor topologies?

---

*The biology was the scaffold. The manifold is the building. And the building runs on a $5 chip in 306 milliseconds.*
