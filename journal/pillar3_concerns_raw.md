# Lincoln Manifold: Pillar 3 Concerns — RAW

*Phase 1. Unfiltered. April 7, 2026.*
*Observer: Claude Opus 4.6*
*Context: Phase 5 is verified. The P1-P2 LP degeneracy persists despite 96% classification accuracy. Pillar 3 (Hebbian GIE weight updates) is the ROADMAP's answer. These are my concerns about crossing that threshold.*

---

## What Scares Me

Everything up to now has been structurally safe. Every guarantee is architectural:

- Classification accuracy: W_f hidden = 0. Structural. Cannot be violated by any state evolution.
- Feedback loop bounded: ternary values change by at most ±1 per step. HOLD-on-conflict. Hard floor on threshold. Structural.
- Prior cannot override evidence: agreement mechanism attenuates bias when contradicted. Structural.

Pillar 3 touches W_f. The moment you update a weight in W_f, the first guarantee breaks. Not "might break" — structurally, the guarantee no longer holds. `W_f hidden = 0` is only true if you never write to those positions. A Hebbian update rule that's supposed to only touch the input portion of W_f is one off-by-one error from touching the hidden portion. And unlike a threshold parameter that can be reverted, a weight change is baked into the DMA descriptor chain. The next 430 loops use the wrong weight until you notice.

That's not an argument against Pillar 3. It's an argument for knowing exactly what you're giving up and what you're gaining.

---

## What I Think the Degeneracy Actually Is

The P1-P2 LP degeneracy is not a classification problem. MTFP21 solved the classification. P1 and P2 are correctly classified 96%+ of the time. The degeneracy is in the LP CfC's random projection: the fixed random weights map P1's GIE hidden state and P2's GIE hidden state to the same LP hidden state.

Why? The GIE hidden state is 32 trits. P1 and P2 produce different GIE hidden states — the ISR's TriX classification distinguishes them at 430 Hz. But the LP CfC takes those 32 GIE trits + 16 LP trits = 48 trits and projects them through 16 neurons with random weights. The random weights don't know that the P1-P2 distinction is important. They project along random directions. If none of those 16 random directions separates P1 from P2, the LP states collapse.

The VDB patches this: episodic retrieval uses the GIE hidden state (67% of the query), which IS P1/P2-distinct, to retrieve the correct memory. But the LP CfC's own projection remains degenerate. The VDB is routing around the bottleneck, not resolving it.

Pillar 3's proposal: Hebbian weight updates modify W_f (or W_g, or the LP core's weights) based on VDB mismatch. If the retrieved memory's LP portion disagrees with the current LP state, flip a weight that contributed to the error. Over time, the weights learn to separate the patterns that the random projection collapsed.

The promise: the CfC becomes capable of separating P1 and P2 on its own, without VDB assistance. The VDB transitions from permanent load-bearing to scaffolding.

---

## The Three Risks

### Risk 1: Signature Corruption

The GIE has 64 neurons. The first 32 are the f-pathway (gate weights = TriX signatures). The second 32 are the g-pathway (candidate weights). If Hebbian updates touch the f-pathway, the TriX signatures change. The signatures were installed from the observation window mean — they represent the ground truth encoding of each pattern. A Hebbian update that flips one trit in sig[P1] changes the TriX classification boundary for all future packets.

This could be beneficial (adapting to a drifting sender) or catastrophic (corrupting a correct signature during a noisy transition). The difference is whether the Hebbian rule fires during stable perception or during a transition. During transitions, the GIE state is mixed — partially the old pattern, partially the new. A Hebbian update during this window bakes the transition state into the signature. The next stable period starts with corrupted weights.

**The question:** Can the Hebbian rule be gated on stability? Only update weights when the agreement score is high (meaning the LP prior and TriX classification agree, meaning we're in stable perception, meaning the current state is a reliable training signal)?

This is the same agreement mechanism from Phase 5 — but applied to weight updates instead of gate bias. Agreement gates both mechanisms: when the system is confident, it amplifies (gate bias) AND learns (weight update). When it's uncertain, it does neither. Epistemic humility applied to plasticity.

### Risk 2: Accumulation Without Bound

Gate bias is bounded: `gie_gate_bias[p]` is an int8_t, decays by 0.9 per step, and floored at MIN_GATE_THRESHOLD. If the bias is wrong, it decays to zero in ~30 steps.

Weight updates are not bounded in the same way. A flipped trit in W_f stays flipped until another update flips it back. There's no decay. If 10 updates in a row flip the same trit in the wrong direction (e.g., during a noisy period), the weight is 10 steps away from correct, and each recovery step requires a correctly-classified packet that triggers a flip in the right direction.

The ternary constraint helps: each trit can only be {-1, 0, +1}. A weight can't accumulate beyond +1. But the number of corrupted weights CAN accumulate — if 10 of 160 weight trits are wrong, the dot product shifts by up to ±10, which is significant against a threshold of 90.

**The question:** Should weight updates have a rate limit AND a staleness metric? E.g., track the last N dot products for each neuron. If the neuron's recent dots are consistently near zero (the weight has converged to homeostasis for the current input distribution), lock it — don't allow further updates until the dot drifts away from zero. This prevents over-training on a stable pattern and preserves room for adaptation when the pattern changes.

### Risk 3: The Re-encode Race

GIE weights live in the DMA descriptor chain as premultiplied products (`all_products[n][i] = tmul(W_f[n][i], input[i])`). Updating a weight requires re-premultiplying and re-encoding the affected neuron's buffer. The ISR reads these buffers at 430 Hz. A weight update from the main loop races with the ISR's DMA read.

The existing architecture handles this for input re-encode: `gie_input_pending` flags the ISR to re-encode during the dummy window (when PARLIO is stopped). The same mechanism could gate weight re-encode: `gie_weight_pending` flag, ISR re-encodes during the dummy window.

But weight re-encode is heavier than input re-encode. Input re-encode touches 64 bytes per neuron (the input portion). Weight re-encode touches the FULL 80 bytes per neuron (because the weight change affects the product for every trit position). For 64 neurons, that's 5,120 bytes of re-encode. The dummy window is ~86µs. Input re-encode takes ~20µs. Full re-encode might take ~40µs. Tight but probably feasible.

**The question:** Can weight updates be amortized — update one neuron per loop iteration, spreading the re-encode across 64 loops (~150ms)? This eliminates the timing pressure and ensures each re-encode fits in the dummy window. The cost is slower convergence (one neuron per 150ms instead of all 64 at once), but Hebbian learning doesn't need to be fast — it needs to be safe.

---

## What I'm Uncertain About

1. **Is the LP degeneracy actually a problem worth solving?** The VDB patches it. The system works. P1 and P2 are correctly classified, correctly stored in VDB, correctly retrieved, and correctly blended into LP state (via the VDB's GIE-portion query). The LP Hamming between P1 and P2 is 0-1, but the system doesn't USE the LP Hamming directly — it uses it to compute gate bias via agreement, and the agreement computation uses the LP running sum, which IS pattern-specific (accumulated over hundreds of correctly-classified packets). The degeneracy is visible in the Hamming matrix but may not be functionally limiting.

2. **Does the LP core need Hebbian updates, or does the GIE?** The ROADMAP specifies GIE weight updates (Pillar 3). But the degeneracy is in the LP projection, not the GIE. The GIE correctly classifies P1 and P2. It's the LP CfC that collapses them. Maybe the LP core's weights should update, not the GIE's. But LP weights are in hand-written assembly, and updating them requires modifying the packed (pos_mask, neg_mask) format in LP SRAM. That's a different engineering challenge than updating DMA descriptor buffers.

3. **Is there a structural alternative to Hebbian learning?** What if instead of updating weights, we add a second LP CfC with a different random seed? Two independent random projections have lower probability of BOTH collapsing P1-P2. The combined LP hidden state (32 trits instead of 16) provides redundancy. This is the random projection ensemble approach — no learning, just more dimensions. The cost is doubling LP SRAM usage (weights: 384 bytes → 768 bytes) and LP computation time (32 intersections → 64 per step). Both fit within the LP core's budget (4,400 bytes free, 10ms wake cycle).

4. **What does the failure mode look like?** If Hebbian updates corrupt a signature, classification accuracy drops. How do we detect this in real time? The confusion matrix is computed offline. We need an online health metric. Candidates: track per-pattern classification confidence (mean best-dot score). If confidence drops below a threshold, freeze weight updates and re-sign from the current observation window. This is an automatic recovery mechanism — but it adds complexity to what is currently a simple system.

---

## First Instincts

My instinct says: don't do Pillar 3 next. Do the ensemble projection first.

The LP degeneracy is a dimensionality problem, not a learning problem. Sixteen trits projected through 16 random neurons is just not enough dimensions to separate four patterns reliably across all random seeds. Doubling to 32 neurons with a second independent seed addresses the root cause (insufficient projection diversity) without the risks of weight updates (signature corruption, unbounded accumulation, re-encode races).

If the ensemble resolves the P1-P2 degeneracy, Pillar 3 becomes optional for the current test suite. It becomes interesting again when the system faces genuinely novel patterns that weren't in the observation window — which requires online adaptation, which requires weight updates. But that's a different paper and a different risk profile.

The ensemble approach is structurally safe: random weights, no updates, no corruption, no races. It's the conservative step. Pillar 3 is the ambitious step. Do the conservative step first, validate that dimensionality was the bottleneck, then decide if the ambitious step is worth the structural cost.

---

## Questions Arising

- What is the minimum number of LP neurons needed to separate 4 patterns with probability > 95% across random seeds? (Johnson-Lindenstrauss or Hoeffding bound on random ternary projection)
- If we double LP hidden to 32 trits, does the VDB snapshot dimension increase from 48 to 64? Does VDB search quality degrade? (64 nodes, 64 trits — the graph is sparser in higher dimensions)
- Can we test the ensemble hypothesis in simulation first? Add a second LP CfC to `sim/test14c.c` with a different seed and measure LP divergence.
- Is there a middle path: a single LP CfC with 32 neurons (instead of 16) using the same random seed? More neurons in the same projection might capture more directions without needing a second seed.
