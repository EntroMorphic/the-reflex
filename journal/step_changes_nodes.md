# Nodes of Interest: Step-Changes for The Reflex

## Node 1: The Priority Ordering Problem

Nine potential step-changes. Finite time. Three papers in different states. The ordering matters because some changes strengthen papers and some open new tracks.

**Paper-strengthening moves:** multi-seed validation, UART falsification. These don't change the system — they add evidence for the existing claims.

**Architecture-extending moves:** dynamic scaffolding (Pillar 1), GIE MTFP encoding. These add capability while preserving the fixed-weight property.

**Architecture-breaking moves:** Hebbian GIE (Pillar 3). Changes the fundamental character of the system.

**New-domain moves:** SAMA (Pillar 2), Nucleo APU. Different papers, different venues, different physics.

Tension: paper-strengthening moves are boring but high-leverage. Architecture-extending moves are interesting but don't help the current papers. The temptation is to build new features. The need is to ship what exists.

## Node 2: Multi-Seed as Minimum Viable Robustness

All silicon data uses seed `0xCAFE1234`. Every projection degeneracy (P1-P2 sign collapse, LP neuron firing patterns, MTFP scale occupancy) is a property of this specific random matrix, not of the architecture.

Three seeds would show:
- Does the gate bias improvement (14C > 14A) hold across different projections?
- Does the VDB causal necessity (CMD 4 collapses, CMD 5 separates) hold?
- Does the MTFP encoding resolve P1-P2 for all seeds, or only when the sign-space degeneracy happens to align with the magnitude scales?

The cost is trivial: change one constant, reflash, run 12 minutes. Three seeds = 36 minutes. The benefit is transformative: the paper claims become architecture claims, not single-matrix claims.

Tension with time: multi-seed needs normal sender mode. TEST 14C needs transition sender. The sender reflash overhead is ~2 minutes per switch. Total workflow for 3 seeds: flash normal sender → run seed A → flash seed B → run → flash seed C → run → flash transition sender → run 14C seed A → etc. Doable in a few hours but tedious.

## Node 3: Dynamic Scaffolding — The 64-Node Wall

The VDB fills in ~3 minutes. After that, the system operates on stale episodic memory. In a stationary environment this is fine — the 64 nodes represent the experience distribution. In a changing environment, the system can't encode new states.

The pruning algorithm:
1. For each node, compute Hamming from the mean of its classified pattern
2. Mark redundant: Hamming ≤ 1 (mean already represents this state)
3. Retain distinctive: Hamming ≥ 3 (the mean can't represent this state)
4. Retain load-bearing: node is on short paths in the NSW graph (high betweenness)
5. For each pruned node: reconnect its neighbors to maintain M≤7 degree, zero the slot

This runs as CMD 6 on the LP core. The LP core already has the infrastructure: it can iterate nodes (VDB insert does this), compute dot products (INTERSECT_LOOP), and modify graph edges (insert already does bidirectional edge management).

Tension: the pattern mean is computed on the HP core (sign-of-sum accumulator). The LP core doesn't know which pattern a node belongs to. Either (a) the HP core tags each node with its pattern ID during insert, or (b) the LP core classifies each node against the VDB query and infers the pattern. Option (a) requires adding a pattern_id byte to each node (node size 32 → 33 bytes, alignment breaks). Option (b) is compute-heavy (64 classifications per pruning pass).

Simpler: the HP core identifies which nodes to prune and writes a deletion list to LP SRAM. The LP core executes the deletions (zero slots, reconnect edges). The decision is HP-side, the execution is LP-side. Clean separation.

## Node 4: GIE MTFP — Redundancy Question

The LP dots are a linear projection of GIE hidden through random weights: `dot_f[n] = INTERSECT(W_f[n], [gie_hidden | lp_hidden])`. The GIE hidden state contributes 32 of the 48 input trits. If we encode GIE dots as MTFP, we're encoding the function of the GIE hidden, not the GIE hidden itself. The LP dots already contain the GIE information, filtered through the LP projection.

The question: does GIE MTFP add information that LP dot MTFP doesn't capture?

Yes, if the LP projection loses GIE structure. The LP projection is 48→16 (or 48→80 with MTFP). It's a dimensionality reduction. Structure that exists in 32-dimensional GIE space may not survive the projection to 16-dimensional LP space. GIE MTFP would preserve that structure before the projection compresses it.

No, if the LP dot magnitudes already capture the pattern-specific GIE variation. The diagnostic showed that LP dots do vary by pattern — neurons n05, n08, n13 have substantial magnitude differences. These differences reflect GIE differences that survived the projection.

Verdict: GIE MTFP is probably marginal. The LP dot MTFP already captures the surviving variation. GIE MTFP would add the variation that didn't survive — but that variation is exactly what the LP projection discards, meaning the LP core can't use it anyway (it doesn't see the GIE MTFP, only the LP dots).

Unless GIE MTFP is used for the agreement computation. Then it's an 160-trit representation of the GIE's full-resolution state, compared against the accumulated mean. This would give the agreement mechanism access to GIE structure that the LP projection collapses. Worth testing — but the MTFP agreement entrainment bug showed that higher-resolution agreement can be destructive. Proceed with caution.

## Node 5: Pillar 3 as a Fork, Not a Step

Hebbian learning changes what the system IS. With fixed weights, the system is: a ternary projector with episodic compensation, where the hippocampus is permanently necessary. With learning, the system becomes: a ternary projector that improves with experience, where the hippocampus gradually becomes less necessary as the projection sharpens.

These are different systems with different theoretical contributions. The fixed-weight system tests "what can episodic memory do for a dumb projector?" The learning system tests "can episodic memory guide projection improvement?"

The Stratum 2 paper argues that the hippocampus is permanently necessary. Pillar 3 would produce a system where the hippocampus becomes redundant — directly contradicting the Stratum 2 finding. This isn't a problem (different experimental conditions produce different findings), but it means Pillar 3 is a new paper, not an extension of the current one.

## Node 6: The Deployment Horizon

At what point does The Reflex stop being a research project and become a product? The answer is probably: when dynamic scaffolding works and SAMA is demonstrated. A single chip that classifies, remembers, adapts, and forgets-when-redundant is a sensor. A cluster of chips that share state at the GIE level is a swarm. The sensor is an engineering contribution. The swarm is a product.

The research value peaks at the current set of papers. After that, it's engineering. The research questions (Is the hippocampus permanently necessary? Does prior-signal separation scale? What does ternary CLS look like with learning?) are best answered by the fixed-weight system, not by a product-ready system with scaffolding and multi-agent support.

Tension: research impact and practical impact diverge here. The papers say "here's what we found." The product says "here's what you can do with it." The project needs to decide which it's optimizing for at each stage.

## Node 7: What the Silicon Hasn't Been Asked

Questions the hardware could answer but hasn't been asked:
- **What does the LP hidden state trajectory look like over 30 minutes?** Current runs are 2 minutes. The VDB fills at 3 minutes. What happens at minute 10 when the VDB has been full for 7 minutes and the stale episodes are increasingly irrelevant?
- **What happens with 8 patterns instead of 4?** The sender could transmit 8 patterns (doubling the pattern ID range). The GIE has 32 f-pathway neurons — 4 groups of 8 with TriX routing, but with 8 patterns it would be 8 groups of 4. Sensitivity drops. Does the architecture degrade gracefully?
- **What's the classification latency?** GIE runs at 430 Hz (2.3 ms per loop). TriX classifies every loop. But the end-to-end latency from packet arrival to LP state update includes ESP-NOW processing, input encoding, ISR re-encode (1-2 loops), and CMD 5 dispatch (10 ms LP wake). What's the actual time from "Board B transmits" to "LP state reflects it"?
- **What does the VDB graph look like?** The BFS connectivity test verifies reachability but doesn't visualize the graph structure. Are P1 episodes clustered? Do P1 nodes link to P2 nodes (cross-pattern edges)?
