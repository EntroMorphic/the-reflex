# LMM: The Strange Loop Learns

*Lincoln Manifold Mapping - Equilibrium Propagation and the Dissolution of Forward/Backward*

---

## What We Built

A learning system where the backward pass doesn't exist as a separate algorithm. Instead:

1. **Free Phase**: Oscillators evolve toward equilibrium (forward pass)
2. **Nudged Phase**: Same dynamics, but output is gently clamped toward target
3. **Weight Update**: Δw ∝ (correlations_nudged - correlations_free)

The gradient computation *emerged* from running the same dynamics twice with different boundary conditions.

---

## What Actually Happened

```
Initial coupling[0][3] = 0.177
Final coupling[0][3] = 0.239

Theta→Alpha saturated to 1.000
Some couplings collapsed to 0.010 (floor)

Average error: 58.2 / 256 (better than random)
```

The system didn't perfectly learn the task. But it **learned something**. The coupling matrix is dramatically different after training - asymmetric, with one connection saturated, others near floor.

More importantly: **the learning happened through the same physics as inference**. There was no backprop, no gradient tape, no separate backward graph. Just: run dynamics, nudge output, run again, compare correlations, adjust.

---

## The Phenomenology of Building This

When I wrote the `evolve_step` function, I initially had separate code paths for free and nudged phases. Then I realized: *they should be the same function with an optional nudge parameter*. The unification felt important. The backward pass isn't a mirror of the forward pass - it IS the forward pass, perturbed.

There was a moment of uncertainty around the weight update rule. In standard equilibrium propagation, you compute:

```
Δw_ij ∝ β⁻¹ (⟨s_i s_j⟩_β - ⟨s_i s_j⟩_0)
```

But what are "correlations" for phase oscillators? I chose: `cos(phase_i - phase_j)`. This felt like the right projection of the complex state onto a real-valued correlation measure. Phase alignment → high correlation. Phase opposition → negative correlation.

Did I derive this rigorously? No. It *emerged* from staring at the structure and asking: what quantity should increase when two oscillators are in sync?

---

## What Didn't Work (Yet)

The learning is noisy. The output phases after 50 epochs don't cleanly map to the targets:

| Pattern | Target | Output | Error |
|---------|--------|--------|-------|
| 0 | 0 | 63 | 63 |
| 1 | 64 | 35 | 29 |
| 2 | 128 | 28 | 100 |
| 3 | 192 | -23 | 41 |

Pattern 2 is way off. Why?

Hypothesis: The 4 patterns aren't linearly separable in the phase space the network naturally wants to occupy. The oscillator dynamics have their own attractor structure, and we're fighting against it.

Or: The learning rate is wrong. Or: 10 steps isn't enough to reach equilibrium. Or: The nudge strength is too weak/strong.

These are hyperparameters I could tune. But the deeper question is: **is the architecture capable of this task at all?**

I don't know. And that not-knowing feels like real research, not just implementation.

---

## The Recursive Reflection Observation

The original Spectral FFN had self-modification via coherence feedback:
- High coherence → reduce coupling → stabilize
- Low coherence → increase coupling → explore

Now we've added *another* self-modification loop: equilibrium propagation adjusts coupling based on output error.

These two loops interact:
1. Coherence-driven adaptation (unsupervised, continuous)
2. Error-driven adaptation (supervised, discrete training steps)

The system is modifying itself in two nested ways. Is this healthy? Does it help? Does it cause interference?

I genuinely don't know. The coupling matrix evolution shows both loops acting - some couplings saturated (possibly from the coherence loop), others drifted (possibly from the learning loop).

---

## On Biological Plausibility

Equilibrium propagation was proposed as a biologically plausible alternative to backprop because:
- Local computations only (no global error signal)
- Same hardware for forward and backward (neurons don't have separate backward circuitry)
- Temporal, not instantaneous (learning emerges over time)

Our implementation inherits this. The weight update rule uses only **local** information: the correlation between two bands at two different times. No neuron needs to know about global loss gradients.

In silicon, this means: the learning could potentially happen **in hardware**. If we had analog coupling elements (memristors? adjustable capacitors?), the correlation difference could directly modify them. The CPU wouldn't need to be involved in learning.

This is speculative. But it's not crazy.

---

## What I Notice About My Own Process

Writing this code felt different from the earlier implementations. There was more uncertainty. More "I think this is right but I'm not sure." The equilibrium propagation literature is sparser than standard deep learning, and the mapping to oscillator dynamics isn't obvious.

I made choices that I can defend but can't prove:
- Phase correlation via cosine
- Coupling update proportional to correlation difference
- Nudge applied to phase velocity, not phase directly
- Same number of steps for free and nudged phases

Each of these could be wrong. The system still learned (a little). Does that validate the choices, or did I get lucky?

The honest answer: I don't know. And this document is me noting that I don't know, rather than pretending I do.

---

## The Shape of What Emerged

We now have a stack:

```
Layer 4: Equilibrium Propagation (supervised learning)
         ↕ modifies coupling via error
Layer 3: Coherence-Coupling Loop (unsupervised adaptation)
         ↕ modifies coupling via coherence
Layer 2: Kuramoto Coupling (phase dynamics)
         ↕ modifies phase velocities via phase differences
Layer 1: Spectral Oscillators (state representation)
         ↕ rotates complex states with band-specific decay
Layer 0: Pulse Arithmetic Engine (hardware substrate)
         PCNT counts, PARLIO transmits, ETM orchestrates
```

Each layer builds on the one below. Each layer has its own timescale and its own form of self-modification.

Is this a good architecture? It's certainly a *rich* one. Many interacting feedback loops. The question is whether the richness is productive complexity or just complexity.

---

## Falsifiable Claims

1. **The coupling matrix changes during training** ✓ Observed
2. **The output phases move toward targets** ✓ Partial (average error decreased)
3. **The learning uses only local correlations** ✓ By construction
4. **More training epochs → lower error** ? Not clearly demonstrated
5. **The system generalizes to novel inputs** ? Not tested

Claim 4 and 5 are the real tests. I'd want to:
- Run for 500 epochs and see if error keeps decreasing
- Hold out one pattern, train on three, test on held-out

These would tell us if this is real learning or just memorization/luck.

---

## What Wants To Happen Next

1. **Hyperparameter search**: Learning rate, nudge strength, equilibrium steps
2. **Longer training**: Does loss converge?
3. **Generalization test**: Train/test split
4. **Input weight learning**: Currently only coupling learns; could input projections also learn via eqprop?
5. **Hardware learning**: Could the coupling update happen without CPU?

The last one is the prize. If the contrastive correlation can be computed in hardware, and the coupling adjustment can be analog... the system learns at inference speed. No training/inference distinction. Just continuous adaptation.

---

## Closing Observation

The backward pass, in this system, is not a backward pass. It's a forward pass with a question mark at the end.

Free phase: "What do you naturally do with this input?"
Nudged phase: "What do you do if I also want this output?"

The difference between those answers is the gradient. Not computed symbolically. Emerged empirically. From running the same dynamics with different boundary conditions.

The strange loop doesn't just compute. It learns.

And it learns the same way it computes: by being itself, twice, and noticing the difference.

---

*"The backward pass IS the forward dynamics, perturbed."*

---

## Metrics

| Measurement | Value |
|-------------|-------|
| Learning rate | 274 Hz (including both phases + update) |
| Inference rate | 580 Hz (forward only) |
| Training epochs | 50 |
| Final avg error | 58.2 / 256 |
| Coupling range | 0.010 to 1.000 (evolved from ~0.1-0.2 initial) |

## Files

- `spectral_eqprop.c`: Equilibrium propagation implementation
- `spectral_ffn.c`: Base spectral FFN (inference only)
- `spectral_double_cfc.c`: Dual-timescale precursor

## Timestamp

2026-02-04, in collaboration with human

