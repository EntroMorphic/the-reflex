# Nodes of Interest: Self-Organizing Representation

## Node 1: The Structural Wall Constraint
The TriX classification guarantee (W_f hidden = 0, 100% accuracy) lives in the same weight matrix that Hebbian learning would modify. Any learning that touches GIE W_f risks breaking classification integrity. This is not a minor concern — it is the central architectural tension. The structural wall that makes the prior safe is the same wall that makes learning constrained.
Why it matters: The ROADMAP's proposed mechanism ("flip the sign of that neuron's weight for the current input trit") directly modifies W_f, which IS the classifier. Implementing it as written would erase the structural guarantee that the project's entire epistemic framing depends on.

## Node 2: Learn in the LP Core, Not the GIE
Resolution to Node 1: the learning layer is the LP core, not the GIE. LP weights (W_f_lp, W_g_lp) are separate from the TriX classifier. Updating LP weights changes what the temporal context layer extracts, which changes the gate bias quality, which changes GIE selectivity — without ever touching the GIE's classification weights. The structural wall stays intact. The prior gets wiser. The classifier stays structurally guaranteed.
Tension with Node 1: This is a weaker form of learning than the ROADMAP proposed. GIE weight updates would directly reshape perception. LP weight updates only reshape the temporal model, which indirectly shapes perception through gate bias. The indirection is safer but slower.

## Node 3: The Error Signal
The VDB search returns the nearest stored snapshot (48 trits) and the Hamming distance. The per-trit disagreement between retrieved node and current state is the candidate error signal. For the LP portion (trits 32-47): each trit where `lp_hidden[i] != retrieved_lp[i]` is an error. The Hebbian rule: for each LP neuron, for each input trit that contributed to the wrong output, flip that weight.
Why it matters: This error signal is already computed during CMD 5 (the feedback blend compares retrieved vs current). Extracting per-trit disagreement is a few AND/XOR operations in assembly — no new data structures, no new peripherals.
Tension with Node 5: The error signal assumes the VDB retrieval is "correct" — that what the system stored in the past is what it should be producing now. If the past state was formed from bad weights, the error signal points toward bad weights.

## Node 4: The Update Mechanism (CMD 6)
A new LP core command: CMD 6 (Hebbian weight update). Runs after CMD 5 (which provides the VDB retrieval and the error signal). For each of 16 LP neurons: load the per-trit error mask, identify weights that contributed to the error, flip the most-contributing one. Write back the updated bitmask. Rate-limited: one flip per neuron per N wake cycles.
Why it matters: This is doable in ~200-400 bytes of assembly. It uses only AND, XOR, popcount, branch — operations the LP core already has. No new hardware. No floating point. No multiplication. The weight update happens at 100 Hz (LP wake rate), rate-limited to maybe once per second per neuron.
Dependency: Needs the per-trit error mask from CMD 5's VDB comparison. Currently CMD 5 computes the aggregate distance but doesn't extract the per-trit mask. Small modification to CMD 5 to store the disagreement mask in LP SRAM.

## Node 5: The Cascade Problem
When LP weights improve, the LP hidden state changes for the same input. But the VDB still contains snapshots computed under the old weights. So VDB retrievals return states from the old representation — and the error signal derived from those retrievals points the new weights toward the old representation. This is a training-on-stale-targets problem.
Why it matters: In the worst case, this creates oscillation: weights update → VDB goes stale → error signal pulls weights back → repeat. In the best case, it's self-correcting: new VDB insertions use the new LP state, gradually replacing old nodes. The convergence rate depends on VDB churn rate vs. weight update rate.
Resolution candidate: Pillar 1 (dynamic scaffolding / VDB pruning) naturally evicts old nodes. If pruning runs in parallel with learning, stale nodes get replaced and the VDB stays current with the learned representation. This creates a dependency: Pillar 1 should run alongside Pillar 3, not after it.

## Node 6: The Gating Problem
When should weight updates happen? Not during noisy periods (wrong error signal). Not during transitions (signal is changing). Only during "consistent retrieval under stable conditions" — when the VDB returns similar results over multiple consecutive steps, suggesting the system is in a stable state with a reliable error signal.
Candidate gate: If the VDB's top-1 result is the same node for K consecutive CMD 5 calls, the retrieval is stable. K=5 (50ms at 100 Hz) would require half a second of consistent retrieval before any update fires. This is conservative enough to filter out transient noise but fast enough to learn within a single pattern exposure (5 seconds per pattern in cycling mode = 100 stable retrievals per pattern per cycle, of which ~20 would trigger updates at K=5).

## Node 7: The CLS Consolidation Analog
This IS the CLS consolidation path the ROADMAP describes. The VDB (hippocampus) trains the LP weights (neocortex) through repeated retrieval under consistent conditions. In biological CLS, the hippocampus replays episodes during sleep to consolidate neocortical representations. Here, the "replay" is the VDB retrieval that happens every 10ms during live operation. Sleep isn't needed — the consolidation is online.
Why it matters: If it works, the LP core's random weights gradually specialize to the patterns actually encountered. The "Seed B headwind" (degenerate random projection) would be fixed: the projection would learn to separate P1 from P2 because the VDB error signal would consistently show that they're different states being collapsed.

## Node 8: What Changes and What Stays
The complete picture of the self-organizing system:

| Component | Fixed | Learns | Why |
|---|---|---|---|
| GIE W_f (input cols) | ✓ | | TriX classification guarantee |
| GIE W_f (hidden cols) | ✓ (=0) | | Structural wall (prior can't corrupt classifier) |
| GIE W_g | ✓ | | CfC candidate pathway (could learn, but risky) |
| Gate threshold | ✓ | | Base sensitivity (Phase 5 bias modulates it) |
| Gate bias | | ✓ (runtime) | Agreement-weighted, decays geometrically |
| LP W_f, W_g | | ✓ (Pillar 3) | Temporal context extraction |
| VDB nodes | | ✓ (insertions) | Episodic memory (already adaptive) |
| Input encoder | ✓ | | Deterministic packet→trit mapping |

The learning is contained entirely within the LP temporal context layer. Everything upstream (GIE classification, peripheral fabric, input encoding) and the structural guarantees (W_f hidden = 0) remain frozen. The system learns a better *model of what it has been perceiving*, not a better *way of perceiving*. The perception is fixed and structurally guaranteed. The interpretation adapts.
