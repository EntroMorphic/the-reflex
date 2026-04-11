# Reflections: Self-Organizing Representation

## Core Insight

**The ROADMAP's proposal to update GIE W_f weights is wrong. The learning belongs in the LP core, where it improves the temporal model without breaching the structural wall.**

The GIE is the retina — fast, frozen, structurally guaranteed. The LP core is the cortex — slower, adaptive, accumulative. The VDB is the hippocampus — episodic, associative, transient. CLS consolidation flows from hippocampus to cortex (VDB → LP weights), not from hippocampus to retina (VDB → GIE weights). The ROADMAP proposed the second path. The architecture demands the first.

This isn't just a safety concern. It's the right level of abstraction. The GIE classifies raw signals. The LP core extracts temporal context from those classifications. The thing that should adapt is the temporal extraction, not the classification. A better LP projection doesn't change what the system sees — it changes what the system *makes of* what it sees. That's the difference between perception and interpretation. Perception should be fixed. Interpretation should learn.

## Resolved Tensions

### Node 1 vs Node 2: Where to learn
**Resolved: LP core only.** The structural wall (W_f hidden = 0) is not negotiable. It's the architectural guarantee that the prior cannot corrupt the classifier. Removing it for learning would be trading the project's core principle for a capability that can be achieved through the safer LP path. The indirection (LP learns → gate bias improves → GIE selectivity improves) is slower but preserves everything that matters.

The ROADMAP's Pillar 3 description should be updated: "LP core generates weight-update signal based on VDB mismatch and applies it to LP weights in-situ" (not "GDMA descriptor chain in-situ").

### Node 5 vs Node 7: The cascade problem
**Resolved by coupling Pillar 1 and Pillar 3.** The cascade (weights improve → VDB goes stale → error signal degrades) is self-correcting IF old VDB nodes are evicted fast enough. Pillar 1 (dynamic scaffolding) prunes redundant VDB nodes. Running Pillar 1 alongside Pillar 3 means: as LP weights improve and LP representations change, old VDB nodes that encoded the old representation get pruned (they're now "redundant" — their LP portion no longer matches the current LP's output for similar inputs). New insertions use the new representation. The VDB stays current.

This changes the dependency graph. The ROADMAP shows Pillar 3 as independent of Pillar 1. They should be co-developed: Pillar 1's pruning criterion needs to account for representation drift from Pillar 3's weight updates.

### Node 3 vs Node 6: Error signal reliability
**Resolved by gating on retrieval stability.** The error signal (per-trit VDB mismatch) is unreliable during transitions, noise, and early in a new pattern's exposure. The gate: require the same VDB top-1 node to be retrieved K consecutive times before the error signal drives an update. At K=5 (50ms), this filters out transient noise while allowing ~20 updates per 5-second pattern exposure. Aggressive enough to learn. Conservative enough to not corrupt.

One additional gate: **agreement with the TriX classifier.** Only update LP weights when TriX and the VDB retrieval agree on the pattern identity. If TriX says P1 and the VDB top-1 was stored during P1, the error signal is probably real. If they disagree, the system is in an ambiguous state and updates should be suppressed. This uses the structural guarantee: TriX is always right (100% label-free), so it's a reliable filter for the learning signal.

## Hidden Assumptions Challenged

### "Learning requires a loss function"
No. The Hebbian rule (strengthen connections that produced correct outputs, weaken those that produced errors) needs only: (1) a target (VDB retrieval), (2) a current output (LP hidden), (3) a per-trit comparison. All three are available and computable in ternary arithmetic. The "loss" is the Hamming distance, and the "gradient" is the per-trit disagreement mask.

### "The LP core is too constrained for learning"
The LP SRAM has ~4,400 bytes free for stack. CMD 6 (weight update) needs ~200-400 bytes of code. The per-trit error mask is 2 words (48 trits packed as pos/neg bitmasks). The update loop: 16 neurons × 1 weight flip per neuron = 16 loads + 16 conditional flips + 16 stores. At 16 MHz, this runs in ~50-100µs. Well within the 10ms wake budget. The constraint is real but not binding.

### "Weight updates need to be atomic"
They don't. The LP core runs CMD 6 during its wake cycle. The HP core reads LP hidden state asynchronously. If a weight update is mid-flight when the HP core reads, the worst case is a one-step inconsistency — the HP core sees a hidden state computed from partially-updated weights. This is equivalent to reading during a blend operation (already happens with CMD 5). The existing fence mechanism (`lp_data_ready`) handles this.

### "The system needs to discover new patterns"
Not yet. The current system classifies 4 known patterns with enrolled signatures. Self-organizing representation improves the temporal model for those 4 patterns. True unsupervised pattern discovery (detecting a 5th pattern that was never enrolled) is a different problem that requires VDB-side novelty detection + automatic signature extraction. That's a future step, not Pillar 3.

## What I Now Understand

The self-organizing representation has three components:

1. **The error signal:** Per-trit disagreement between LP hidden state and VDB-retrieved LP portion. Already computed during CMD 5. Needs ~6 lines of assembly to extract and store as a bitmask.

2. **The update rule:** Ternary Hebbian — for each LP neuron, for each input trit, if the weight's contribution produced an output that disagrees with the VDB target, flip that weight. One flip per neuron per update cycle. Rate-limited to K=5 consecutive stable retrievals.

3. **The safety gates:** (a) Retrieval stability (same top-1 for K steps). (b) TriX agreement (classifier and retrieval agree on pattern identity). (c) Rate limiting (one flip per neuron per N wake cycles). (d) VDB pruning (Pillar 1) to evict stale nodes.

The implementation is a new CMD 6 on the LP core (~200-400 bytes of assembly), a small modification to CMD 5 to store the per-trit error mask, and a gating mechanism on the HP core that decides when to issue CMD 6 vs CMD 5.

The structural wall stays intact. The classifier stays frozen. The prior becomes wiser through experience. And the system learns — on peripheral hardware, at 30 microamps, without a training loop, a GPU, or a floating-point number.
