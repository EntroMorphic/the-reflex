# Reflections: Projection-Aware Gate Bias

## Core Insight

The idea is correct in principle but premature in practice. The disc metric depends on the LP means. The LP means in sign-space inherit the P1-P2 degeneracy. The LP means in MTFP-space resolve it but introduce a more complex mapping. Neither has been validated as a discriminability predictor. Building and testing the mechanism is at least a half-day of work with uncertain payoff.

The paper is already honest about the 2/3 result. Making it 3/3 would be stronger, but only if the mechanism is simple and well-understood. A per-neuron bias with an unvalidated disc metric adds more complexity than confidence.

## Resolved Tensions

### Node 1 vs Node 2: Which disc metric?

**Resolution: MTFP-space, LP-neuron-level.** For each LP neuron k, check whether its 5-trit MTFP block differs across any pattern pair. This correctly identifies P1-P2 discrimination that sign-space misses. The mapping from GIE neuron n to LP neuron k is through W_f[k][n] — this is direct and fixed.

But this doesn't resolve the deeper issue: we don't know if this metric actually predicts which GIE neurons are useful to bias. It's a theoretical construction. It needs empirical validation.

### Node 3: Entrainment risk

**Resolution: bounded but real.** The same decay (0.9 per confirmation) and hard floor (MIN_GATE_THRESHOLD=30) limit the damage. A single entrained neuron can lower its threshold to 30 but not below. The GIE's 32 neurons still vote by majority. One always-firing neuron contributes 1/32 of the hidden state — noticeable but not catastrophic.

The MTFP agreement entrainment was worse because it affected the agreement score globally (80-trit dot), which fed back into ALL bias values. Per-neuron entrainment is local — it affects one neuron's firing, which changes the LP state, which changes the agreement score, which changes all biases. The feedback path is still global through the agreement score. So the entrainment risk is similar in kind, just more localized in expression.

### Node 4: Is this the right problem?

**Resolution: no, not right now.** The right problem right now is submitting the papers. Per-neuron bias is an improvement to the mechanism. The mechanism is already demonstrated (it works 2/3 of the time, honestly reported). Improving it to 3/3 is an engineering contribution, not a research contribution. It belongs in the follow-up, not in the first paper.

The paper should:
1. Report the 2/3 result honestly (already done)
2. Diagnose WHY seed B fails (the projection's discriminative direction doesn't align with the group structure)
3. Describe per-neuron discriminability-weighted bias as the predicted fix
4. Leave implementation to future work

This is a stronger paper than "we tried per-neuron bias and it fixed it" — because the diagnosis explains the failure mechanism, and the predicted fix follows logically from the diagnosis. A reviewer can evaluate the reasoning without needing to see the implementation.

## What I Now Understand

**Don't build it. Describe it.**

The per-neuron bias idea is sound. The disc metric has a correct formulation (MTFP-space, LP-neuron-level). The ISR change is trivial. But the implementation introduces unvalidated complexity at a point where the project needs to ship, not iterate.

The right move: add a paragraph to the Stratum 1 paper's Analysis section explaining why seed B fails and what per-neuron bias would change. Specify the mechanism precisely enough that a reader could implement it. Don't implement it ourselves. Ship the honest result.

Build it in the next session, after the papers are submitted, as Tier 2 work (architecture-extending, not paper-strengthening).
