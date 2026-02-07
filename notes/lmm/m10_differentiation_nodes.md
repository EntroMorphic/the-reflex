# Nodes of Interest: M10 Differentiation Results

Extracted from RAW phase. These are observations and tensions, not solutions.

---

## Node 1: The Input Is a Constraint, Not a Driver

Under zero input, delta went from ~11 to ~16. The input was stabilizing the network, not powering it. 128/160 of the concat is constant when input is present — 80% of each dot product is redundant step to step. Removing it frees the hidden state dynamics from that anchor.

Why it matters: This inverts the conventional model of neural network computation. Inputs aren't "driving" the network — they're constraining a self-sustaining dynamical system. The network's natural state is wild exploration; the input focuses it.

## Node 2: The U/I Balance Is Statistical, Not Dynamic

The blend mode balance (~55% UPDATE, ~40% INVERT, ~5% HOLD) held across all phases: stem, differentiation, commitment, de-differentiation. P2 was falsified because this balance is a statistical property of random ternary dot products, not something the network regulates.

Why it matters: This challenges the "homeostasis" interpretation. If the balance can't be broken by changing inputs, it's not being maintained — it's just the expected value of the distribution. The network ISN'T choosing to be balanced. It IS balanced by construction.

Tension with Node 1: If the balance is fixed and the input is just a constraint, what IS the network actually doing that's non-trivial?

## Node 3: Path-Dependent Memory Is Real

Hamming distance of 10 between naive (100 steps of A) and de-differentiated (30A + 20B + 20B + 30A). Same weights, same current input, different histories, different states. This is NOT explained by statistics — it's a consequence of the nonlinear trajectory through state space.

Why it matters: This is the cleanest result. No interpretive framework needed. The hidden state carries information about its history. The present state depends on the path, not just the current condition.

Question: Is the 10-trit scar proportional to exposure duration? Does 5 steps of B leave a 2-trit scar? Does 100 steps of B leave a 15-trit scar? Or is there a threshold?

## Node 4: Cognitive Lightcones Are Context-Dependent

I classified neurons as input-heavy or hidden-heavy based on weight density. But under zero input, all neurons become effectively hidden-only (the input-portion weights multiply zeros). The lightcone isn't a fixed property — it's weight matrix TIMES input context.

Why it matters: This is a genuine correction to the Levin mapping. A neuron's cognitive lightcone isn't its weight vector. It's its weight vector projected onto the current signal. The same neuron has a different effective lightcone under different inputs. This is more nuanced than what I wrote in the LMM note.

Tension with Node 2: If lightcones are context-dependent, and the input changes the effective lightcone, then changing the input DOES change something meaningful — just not the U/I balance.

## Node 5: What Changed During Differentiation Was Content, Not Dynamics

Phase A and Phase B have nearly identical macro-statistics (avg_delta ~11, energy ~30, U ~55%). But the HIDDEN STATE moved 17-24 trits from the stem attractor. The dynamics (how the system behaves) were preserved. The content (what specific state the system is in) changed.

Why it matters: This is the distinction between the state space and the trajectory. The attractor landscape (determined by weights) defines the dynamics. The input selects which region of that landscape the network visits. The dynamics are invariant because the landscape is invariant. The content changes because the input shifts the trajectory.

This is exactly Noble's point: the macro-level (dynamics) is conserved while the micro-level (specific trits) varies. The levels are coupled but the macro doesn't reduce to the micro.

## Node 6: Agency Requires Perturbation Recovery

Levin's operational definition of goal-directedness: robust return to a preferred state after perturbation. We showed de-differentiation (return to stem-like state after input perturbation). But we didn't show recovery from STATE perturbation — randomizing the hidden state and watching if it returns to the attractor.

Why it matters: Without the perturbation recovery test, we can't distinguish "goal-directed behavior" from "trajectory determined by initial conditions." A ball rolling downhill returns to the valley after a push, but we don't call it goal-directed. We'd need the network to return to its attractor from MULTIPLE different perturbations — showing the attractor is a basin, not just a trajectory.

This is the experiment that earns the word "cognition."

## Node 7: The 80/20 Dimension Asymmetry

Input is 128 trits. Hidden is 32 trits. This is a 4:1 ratio. Under normal operation, the input dominates the dot product. Under autonomy, only the hidden portion matters.

Why it matters: This isn't just a parameter choice — it determines the entire character of the system. A 50/50 ratio (64 input, 64 hidden) would give the hidden state equal footing during normal operation. The current 80/20 split means the input overwhelms the hidden state in every dot product. The hidden feedback is a PERTURBATION on an input-dominated computation.

Question: What happens at 64/64? Does the system become more autonomous (hidden state has more influence) or more chaotic (less external constraint)?

## Node 8: Cell Types Are Attractor Basins

Three inputs produced three distinct committed states with 16/32 average pairwise distance. These are three different regions of the state space that the network visits under three different inputs. They're distinguishable because the weight landscape creates different attractor basins that different inputs steer toward.

Why it matters: This is the most concrete evidence that the weight matrix defines a structured landscape, not just random dynamics. If it were random, different inputs would produce similar hidden states (because the dot product statistics would be similar). The fact that they're 50% different means the landscape has genuine structure — different basins, different attractors.

Tension with Node 2: The U/I balance is statistical (Node 2), but the committed states are structured (Node 8). Both are true. The DYNAMICS are statistical; the CONTENT is structured. This is the same point as Node 5 but from a different angle.

## Node 9: The Stem Cell Analogy Was Scaffolding

The analogy motivated the experiment, but the results show something different from what the analogy predicted. P2 falsification means the differentiation mechanism isn't what biology does (cascade of commitment signals). It's what dynamical systems do (trajectory shift in a fixed landscape).

Why it matters: Analogies are tools, not truths. The stem cell analogy got us to build the experiment. The experiment revealed a dynamical-systems truth that's more precise than the biological analogy. We should update the framework, not defend the analogy.

But: the de-differentiation result IS analogous to biological iPSCs (induced pluripotent stem cells). And the path-dependent memory IS analogous to epigenetic memory. The analogy isn't wrong — it's partial. It works at the level of state transitions but not at the level of mechanism.

## Node 10: The Self-Sequencing Fabric Implication

The autonomy result means a self-sustaining ternary CfC could run as a peripheral loop: GDMA evaluates dot products, CPU does blend logic, repeat. No external input needed. The CPU doesn't schedule steps — the fabric just runs.

But here's the thing: with the input zeroed, you only need the hidden-weight portion of each weight vector (32 trits, not 160). That's 5x fewer trits per dot product. At 1 MHz PARLIO, that's 5x faster — effectively 33 Hz instead of 6.7 Hz. At 10 MHz, it's 335 Hz. An autonomous ternary CfC running at 335 Hz on a $5 chip with no CPU intervention.

Why it matters: The architecture and the computation adapt to the context. When the input is present, you need the full 160-trit dot product. When the system is autonomous, you only need 32 trits. The DMA descriptor chain could be reconfigured at the moment the input is removed — or the input portion could be a separate DMA buffer that's simply not loaded.

## Node 11: 32 Neurons Might Be Too Few

With 32 hidden trits and 3 possible values per trit, the state space is 3^32 ≈ 1.85 × 10^15. That's large. But the EFFECTIVE state space under the dynamics might be much smaller. If the network only visits a small subset of states, 32 dimensions might not be enough to see genuine attractor structure vs. noise.

Question: Is there a way to estimate the effective dimensionality of the dynamics? Track how many principal components of the hidden state trajectory explain most of the variance.

## Node 12: The Falsification Pattern

P2 was falsified cleanly and immediately. This is good science — we made a specific prediction and the silicon said no. The question is: did we learn more from the falsification than we would have from confirmation?

Yes. Confirmation would have said "the stem cell analogy works at the mechanism level." Falsification said "the dynamics are statistical invariants" — which is a deeper structural insight about the architecture class. Falsification was more informative.

This validates the LMM approach. If we'd only run the experiment to confirm our hypothesis, we'd have missed the most important finding.
