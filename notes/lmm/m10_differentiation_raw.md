# Raw Thoughts: M10 Differentiation Results

## Stream of Consciousness

We ran four tests on silicon. 306ms. The numbers came back and I wrote an analysis immediately. But I didn't sit with them. Let me sit with them now.

The biggest thing that happened tonight: the network is self-sustaining. 30/30 steps with dynamics under zero input. Delta went UP when we removed the input. That's... backwards from what I expected. I expected the network to wind down, maybe oscillate for a bit, then die. Instead it got MORE active. The input was a leash, not a battery.

Why? The input is 128 trits. The hidden state is 32 trits. When input is present, 128/160 of the concat vector is fixed (same every step). Only 32/160 changes (the hidden state). That means 80% of the dot product is determined by the input, and only 20% by the hidden state. The network is mostly reacting to a constant signal. Of course the dynamics are constrained — most of the computation is redundant step to step.

When you zero the input, 128/160 of the concat is zero. Those zero trits contribute nothing to the dot product. Now the ONLY thing driving the dot products is the 32 hidden trits through the hidden-weight portion. Every single trit that matters is changing every step. 100% of the signal is dynamic. No wonder delta went up.

Wait. This means the "cognitive lightcone" analysis is wrong. Or at least incomplete. I classified neurons as "input-heavy" vs "hidden-heavy" based on weight density. But under zero input, the input-heavy neurons aren't attending to nothing — their input-portion weights are multiplying zeros. They're effectively disconnected from 80% of their weight vector. Their EFFECTIVE lightcone collapsed to the hidden-weight portion only.

So the autonomy test didn't just show "self-sustaining dynamics." It showed a reconfiguration of the cognitive lightcone itself. Under normal input, some neurons are externally focused. Under zero input, ALL neurons are forced to be internally focused. The lightcone isn't a fixed property of the weight matrix — it's a function of the weight matrix AND the input.

That's... that's Noble. The weight matrix is the structure. The input is the context. The effective computation (what the neuron actually computes) depends on both. Neither is privileged.

Now I'm thinking about P2's falsification differently. We predicted UPDATE would surge when the input changed. It didn't. The U/I balance stayed at roughly 55/40. Why? 

Because the balance is determined by the STATISTICS of the dot products, not their VALUES. When you change the input, you change which specific dot products are positive/negative/zero, but you don't change the DISTRIBUTION. With 50% sparse random weights and 40% sparse random inputs, the dot product of 160 trits will be approximately normally distributed around zero regardless of which specific input you use. The sign (which determines f) is roughly equally likely to be positive or negative for any input. The blend mode balance is a STATISTICAL INVARIANT of the architecture, not a dynamic variable.

This is a deeper insight than I put in the LMM note. The U/I balance isn't homeostasis in the biological sense (active regulation). It's a statistical property of random ternary inner products. It would be remarkable if it WEREN'T balanced. The real question is: can you BREAK the balance? What would it take?

Biased weights would break it. If W_f had mostly positive weights, f would be mostly positive (UPDATE-dominated). If W_f had mostly negative weights, f would be mostly negative (INVERT-dominated). The balance we see is because cfc_init generates balanced weights (50% zero, 25% positive, 25% negative among non-zeros).

So the "cognitive strategy" I attributed to the network isn't a strategy at all. It's a consequence of random initialization. That's less romantic but more honest.

But wait — the PATH-DEPENDENT MEMORY result (P5) is still real. The naive and de-differentiated networks have different hidden states despite identical weight matrices and identical current inputs. The weights are the same, the input is the same, but the hidden state differs by 10 trits. This means the hidden state carries information that ISN'T determined by the weights and current input. It's determined by the trajectory. And that trajectory-dependence is what creates distinguishable behavior even under identical conditions.

The cell types result is also real and not trivially explained by statistics. Same weights, different inputs, different committed states — with 16/32 average pairwise distance. The inputs are selecting different attractors in the same weight-determined landscape.

Now about Levin. Am I pattern-matching or is there something real here?

Levin's cognitive lightcone: the spatiotemporal range over which an agent can pursue goals. For our neurons, the "spatial" range is the weight vector (which trits it attends to) and the "temporal" range is the hidden state feedback (how many steps of history influence the current computation). Input-heavy neurons have a spatial range dominated by external signals. Hidden-heavy neurons have a temporal range dominated by internal history.

But Levin's framework requires GOAL-DIRECTEDNESS. What goal is a ternary CfC neuron pursuing? It doesn't have a loss function. It doesn't have a target. It just computes sign(dot + bias). There's no "goal" in the system.

Unless... the goal is the attractor itself. The network under constant input settles into a dynamic regime (not a fixed point, but a statistical regime — bounded delta, bounded energy, stable U/I balance). When perturbed, it returns to a similar regime (de-differentiation). That's robustness to perturbation. Levin says that's the operational definition of goal-directedness: if a system returns to a preferred state after perturbation, it's pursuing that state as a goal, regardless of whether it "knows" it.

By that definition, the ternary CfC IS goal-directed. Its "goal" is its statistical regime. And it achieves that goal through three mechanisms: excitation (UPDATE), memory (HOLD), and inhibition (INVERT). The inhibition mode is the one that binary systems lack. It's the active resistance that makes de-differentiation possible.

OK what about the autonomy result in this light? Under zero input, the network maintained high energy and high delta for 30 steps. Is it pursuing a goal? What goal? There's no input to react to. It's just... churning. Is churning goal-directed?

Maybe. If the "goal" is to maintain high energy (stay active, don't collapse to the zero state), then yes — the network achieves this under zero input. But that might just be because the hidden-to-hidden weight connections create enough positive feedback to sustain activity. It's not goal-directed in any meaningful sense if it's just a consequence of the weight matrix having enough non-zero entries in the hidden portion.

I need to be more careful about attributing agency. The results are real. The interpretations need to earn their keep.

What WOULD convince me that the network is genuinely goal-directed in Levin's sense?

1. Perturbation recovery under autonomy. Zero the input, let it run, then INJECT a perturbation (randomize half the hidden state), and see if it returns to the same dynamic regime. If it does, that's robustness that can't be explained by simple positive feedback.

2. Different goals for different contexts. If the network under input A settles to regime X, and under input B settles to regime Y, and when perturbed within regime X it returns to X (not Y), then it's maintaining a context-specific goal. We sort of showed this (cell types), but we didn't test perturbation recovery within a committed state.

3. The autonomy dynamics should be DIFFERENT from the input-driven dynamics in a specific way. If they're just "more of the same but noisier," that's not interesting. But if the autonomy regime has a different attractor structure — different periodicity, different energy distribution, different neuron-level patterns — then the network is doing something qualitatively different when it's self-driven vs externally driven.

What scares me about this problem: I might be seeing patterns that aren't there. Levin, Noble, stem cells, cognitive lightcones — these are powerful frameworks and it's easy to project them onto simple dynamics. A random ternary recurrent network will ALWAYS show sustained dynamics if the weights are dense enough and the hidden state feeds back. That's not cognition. That's just recurrence.

What's the delta between "interesting recurrent dynamics" and "minimal cognition"? I think it's the perturbation recovery test. Simple recurrence doesn't recover from perturbation — it just follows whatever trajectory the perturbed state leads to. Goal-directed behavior recovers to a preferred state.

First instincts that are probably wrong:
- "The U/I balance is homeostasis" — No, it's statistics. Need structured weights to test real homeostasis.
- "The network is self-sustaining therefore autonomous" — Partly. The dynamics are self-sustaining but might just be noise recycling through a recurrent loop.
- "Cognitive lightcones map cleanly to weight distribution" — The autonomy test showed lightcones are context-dependent, not fixed.

First instincts that might be right:
- Path-dependent memory is real and significant
- Cell types are distinguishable and input-dependent
- De-differentiation works and the 10-trit scar is meaningful
- The autonomy result (delta INCREASING under zero input) reveals something about the input's role as a constraint/synchronizer
- The lightcone reconfiguration under zero input is a genuine observation
- Perturbation recovery is the right next experiment

## Questions Arising

1. Is the U/I balance an invariant of random ternary CfC, or can it be broken by structured weights?
2. What does perturbation recovery look like under autonomy?
3. Is there a phase transition between "self-sustaining" and "dying out" as you reduce hidden-weight density?
4. Can you distinguish the autonomy regime from the input-driven regime by more than just delta magnitude?
5. Is path-dependent memory proportional to the duration of the different input, or is it binary (any exposure leaves a scar)?
6. Would a TRAINED ternary CfC (not random) show different differentiation dynamics?
7. Is 32 neurons enough to see genuine attractor structure, or is this just noise in a small system?

## First Instincts

- The autonomy finding is the most important result but needs sharper interpretation
- P2 falsification reveals a statistical truth about random ternary architectures, not a failure of the stem cell analogy
- Path-dependent memory is the clearest genuinely novel finding
- The next experiment should be perturbation recovery under autonomy — that's the Levin test
- I was too hasty attributing agency and cognition. Need to earn those words.
