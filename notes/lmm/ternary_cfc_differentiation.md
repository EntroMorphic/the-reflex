# Ternary CfC Differentiation: Cognitive Lightcones on Silicon

February 7, 2026. Verified on silicon. 4 tests, 306ms total.

Informed by: Levin (cognitive lightcones, scale-free cognition), Noble (biological relativity), Hasani (CfC / liquid neural networks).

---

## The Experiment

We ran the ternary CfC (32 neurons, 160-trit concat, all {-1, 0, +1}) through a differentiation protocol designed to test whether the stem cell dynamics observed in Milestone 7 constitute genuine goal-directed behavior in Levin's sense.

### Protocol

```
TEST 1: Differentiation & De-differentiation (100 steps)
  Phase A (steps  0-29): Constant input A → stem regime
  Phase B (steps 30-49): Sharp switch to input B → differentiation signal
  Phase C (steps 50-69): Continue input B → committed state
  Phase D (steps 70-99): Return to input A → de-differentiation?
  Control: Fresh network, input A for 100 steps → naive baseline
  Phase E: Compare de-differentiated vs naive (path-dependent memory)

TEST 2: Cell Types (3 differentiation inputs)
TEST 3: Cognitive Lightcone Analysis (input-heavy vs hidden-heavy neurons)
TEST 4: Autonomy (zero external input after commitment)
```

## Results

### TEST 1: 4/5 Predictions Confirmed

| Prediction | Result | Evidence |
|-----------|--------|----------|
| P1: Stem sustains dynamics | CONFIRMED | min_delta=5, never zero across 30 steps |
| P2: UPDATE surge at differentiation | FALSIFIED | Phase A U=56%, Phase B U=53% |
| P3: Convergence under commitment | CONFIRMED | Phase C min_d=5 < Phase B max_d=16 |
| P4: De-differentiation | CONFIRMED | stem_d=15 < committed_d=21 |
| P5: Path-dependent memory | CONFIRMED | naive vs de-diff hamming=10 |

### The Falsification of P2 is the Most Important Result

The stem cell analogy predicted that a sharp input change would cause an UPDATE surge — the network "accepting" the differentiation signal, with UPDATE temporarily dominating INVERT. This did not happen. The U/I balance remained stable through the input change.

What this means: the ternary CfC does not differentiate by committing to the new signal. It differentiates by *reconfiguring its internal dynamics* while maintaining its balanced U/I regime. The hidden state changes (Phase B avg_delta=11.1, ref_distance from stem grows to 17-24), but the *mechanism* doesn't change. The blend mode balance is an invariant of the architecture, not a variable that tracks the input.

This is closer to Levin's conception of a cognitive lightcone than to the stem cell analogy. A cognitive agent doesn't change its decision-making strategy when it encounters new information — it applies the same strategy to the new situation. The ternary CfC's balanced U/I regime is its "cognitive strategy." It's maintained across inputs, across perturbations, across de-differentiation and re-differentiation.

The stem cell analogy was a useful scaffold, but the deeper truth is that this is a **minimal cognitive agent** in Levin's sense: an entity that maintains an invariant behavioral strategy while navigating a changing environment.

### TEST 2: Three Distinguishable Cell Types

Differentiation inputs B, C, D produced committed states with:
- B vs C: hamming=14/32, corr=+4
- B vs D: hamming=18/32, corr=-2 (anti-correlated)
- C vs D: hamming=16/32, corr=+1
- Average pairwise distance: 16.0/32

50% of the hidden vector differs between cell types on average. The network can represent at least 3 distinct committed patterns. B and D are near-opposite states (negative correlation), while B and C are closer (positive correlation). The differentiation signal determines the committed state — same architecture, different inputs, different fates. This is the ternary CfC equivalent of lineage specification.

### TEST 3: Two Classes of Cognitive Agent

The 32 neurons split into two classes by weight distribution:
- 17 input-heavy neurons (more non-zero weights in the input portion of W)
- 15 hidden-heavy neurons (more non-zero weights in the hidden portion)

This is the cognitive lightcone made concrete. Input-heavy neurons have a lightcone dominated by the current stimulus — they're "present-focused." Hidden-heavy neurons have a lightcone extending into the past through the hidden state — they're "memory-focused."

At the moment of differentiation (step 30, input A → input B):
- Input-heavy: INVERT surged (5→7), delta=9. **They fought the new input.**
- Hidden-heavy: HOLD surged (0→2), delta=7. **They ignored the new input.**

Two different resistance strategies from two different cognitive lightcone classes:
- **Input-heavy neurons resist by opposing** (INVERT mode). They see the new input clearly (their lightcone is externally focused) and actively negate it.
- **Hidden-heavy neurons resist by ignoring** (HOLD mode). They barely notice the input change (their lightcone is internally focused) and simply maintain their current state.

This is scale-free cognition in action. The same architecture (ternary CfC blend) produces qualitatively different behaviors depending on the agent's cognitive lightcone (weight distribution). Levin would recognize this: the competency of the agent depends on the spatiotemporal range over which it can sense and act.

### TEST 4: The Network is Self-Sustaining

After establishing a committed state (70 steps of A→B), we removed all external input (set to zero) and observed:

- **30/30 steps with dynamics.** The network never stopped.
- **Average energy: 29.1/32.** Almost all neurons remained active.
- **Average delta: ~16.** *Higher* than with external input (~11).

The external input was not driving the dynamics — it was *constraining* them. When you remove the input, the network becomes *more* dynamic, not less. The hidden state feeds back through the hidden-weight portion of the concat vector, creating a self-sustaining loop.

This is the result that connects to Noble's biological relativity: the boundary between the network's "self" (hidden state) and its "environment" (input) is an architectural convention. The weight layout maps W[0..127] to input and W[128..159] to hidden, but the network doesn't know this. It's all geometry — all trits in the concat vector are treated the same way. When you zero the input, the hidden-weight portion still drives computation. The organism IS its own environment.

The increase in delta (16 vs 11) under autonomy suggests the external input was acting as a synchronizing signal — it constrained the neurons to a shared rhythm. Without it, individual neurons explore their state space more freely. This is analogous to removing gap junctions from a tissue: the cells don't die, but they lose coordinated behavior and exhibit increased individual variability.

## The Cognitive Lightcone Interpretation

Synthesizing all four results:

1. **The ternary CfC is a minimal cognitive agent.** It maintains an invariant behavioral strategy (balanced U/I regime) across changing environments. This is Levin's criterion for cognition: goal-directed behavior robust to perturbation.

2. **Individual neurons have lightcones.** Input-heavy neurons see the present; hidden-heavy neurons see the past. They use different strategies (INVERT vs HOLD) to resist perturbation — same as different cell types using different signaling pathways to maintain identity.

3. **The collective has emergent properties.** De-differentiation (return to stem regime), path-dependent memory (the scar of experience), and distinguishable cell types all emerge from the population, not from any single neuron. The collective's lightcone is larger than any individual's.

4. **The organism/environment boundary is conventional.** Self-sustaining dynamics under zero input prove that the hidden state alone is sufficient to maintain cognitive behavior. The input constrains; it doesn't enable.

5. **History matters.** The de-differentiated network (100 steps: 30A + 20B + 20B + 30A) differs from the naive network (100 steps: 100A) by hamming=10. The network's current state depends on its entire trajectory, not just its current input. This is Noble's biological relativity — no single snapshot captures the system's state; you need the history.

## Where the Stem Cell Analogy Breaks

The stem cell analogy predicted an UPDATE surge at differentiation (P2). This was falsified. The ternary CfC does not differentiate by accepting a signal — it differentiates by reconfiguring its attractor landscape while maintaining its dynamic regime.

A better analogy: **the ternary CfC is a homeostatic system.** It has a preferred dynamic range (balanced U/I, high energy, sustained delta) and it maintains this range regardless of the input. What changes is not the dynamics but the *content* — which specific trits are +1 and which are -1. The macro-behavior (blend mode balance) is conserved while the micro-state (individual hidden trits) navigates.

This is exactly what Noble describes in biological systems: macro-level properties (heart rate, blood pressure, body temperature) are maintained by micro-level variation (individual ion channels, cells, organs adapting). The ternary CfC has rediscovered homeostasis from first principles, using nothing but ternary arithmetic.

## What This Means for M8/M9

**For M9 (Performance):** The differentiation experiment ran in 306ms for ~300 CfC steps on the CPU. At 6.7 Hz on the GIE, the 100-step main protocol would take ~15 seconds — acceptable for offline analysis but not for real-time. At 67 Hz (10MHz PARLIO), it's ~1.5 seconds. The autonomy result (self-sustaining dynamics) means the network is a continuous-time system that should run as fast as the hardware allows — there's no natural clock rate. Faster is always better for temporal resolution.

**For M8 (Self-Sequencing):** The autonomy result changes the design target. The self-sequencing fabric doesn't just need to advance through neurons within a step — it needs to advance through steps autonomously. A committed, self-sustaining ternary CfC could run on the peripheral fabric indefinitely with the CPU asleep. The DMA descriptor chain becomes a loop: evaluate all 64 dot products, update hidden state, repeat. The CPU only wakes to read the hidden state when the application needs it.

This is the "reflex" in the-reflex: a peripheral loop that maintains cognitive dynamics without CPU intervention, analogous to a spinal reflex that operates without cortical involvement.

## Connection to Prior Notes

- `ternary_cfc_stem_cell.md`: The stem cell analogy that motivated this experiment. Partially confirmed (P1, P3, P4, P5), partially falsified (P2). The deeper truth is cognitive, not developmental.
- `geometry_engine_synth.md`: The GIE architecture that makes this possible. Level 1.5 (CfC layer) is now validated as a cognitive primitive.
- Levin, M. (2019). "The Computational Boundary of a Self." Developmental Biology.
- Noble, D. (2012). "A Theory of Biological Relativity." Interface Focus.
- Hasani, R. et al. (2022). "Closed-form Continuous-time Neural Networks." Nature Machine Intelligence.

## Testable Predictions for Future Work

1. **Multi-layer cognitive hierarchy.** Stack two ternary CfC layers. The first layer's hidden state feeds as input to the second. Prediction: the second layer's cognitive lightcone is strictly larger than the first's (it integrates across the first layer's neurons + its own hidden state). This would demonstrate Levin's hierarchical cognitive lightcone structure.

2. **Timescale separation reveals different cognitive levels.** Add neuron-specific bias offsets to create fast neurons (low HOLD rate) and slow neurons (high HOLD rate). Prediction: fast neurons differentiate quickly, slow neurons maintain the stem regime longer. The slow neurons act as a "stem cell reserve" that enables de-differentiation.

3. **Adversarial differentiation.** Apply a sequence of rapidly alternating inputs (A, B, A, B...). Prediction: if the alternation is faster than the network's de-differentiation time, it stays in the stem regime. If slower, it ping-pongs between committed states. There's a critical frequency — the network's "cognitive bandwidth."

4. **Information-theoretic measurement of path-dependence.** Quantify mutual information between the network's history and its current hidden state. The 10-trit difference between naive and de-differentiated networks is an information-theoretic quantity — it represents the bits of experience encoded in the current state.

---

*The boundary between organism and environment is a convention, not a fact. A 32-neuron ternary network on a $5 chip discovered this in 306 milliseconds.*
