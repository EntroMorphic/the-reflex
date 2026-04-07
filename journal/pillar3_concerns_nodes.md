# Lincoln Manifold: Pillar 3 Concerns — NODES

*Phase 2. Grain identification. April 7, 2026.*
*Observer: Claude Opus 4.6*

---

## Node 1: Pillar 3 Breaks the Only Structural Guarantee That Matters

W_f hidden = 0 is the structural wall between classification and temporal state. Every safety property downstream depends on it: TriX accuracy is input-only, gate bias can't corrupt classification, the prior can't override evidence. Pillar 3 modifies W_f. Even if the Hebbian rule is designed to only touch input-portion weights, the guarantee becomes conditional on the rule's correctness, not the architecture's structure.

**Why it matters:** The system goes from "correct by construction" to "correct by discipline." Discipline fails under edge cases. Construction doesn't.

**Tension with Node 4:** The degeneracy IS a real limitation. Avoiding Pillar 3 preserves the guarantee but accepts the limitation permanently. The question is whether the limitation matters enough to justify the risk.

---

## Node 2: The Degeneracy Is a Dimensionality Problem, Not a Learning Problem

The LP CfC projects 32 GIE trits through 16 random neurons. By the Johnson-Lindenstrauss lemma, random projections preserve pairwise distances with probability that depends on the target dimension. For 4 patterns in 32 dimensions projected to 16 dimensions, some random seeds will collapse specific pairs. This is expected, not pathological.

Doubling to 32 LP neurons (or using two 16-neuron projections with independent seeds) reduces the collapse probability quadratically. No learning. No weight updates. No structural guarantees broken.

**Why it matters:** If the problem is "not enough random directions," the fix is "more random directions," not "learned directions." Learning is a stronger solution but carries structural cost. The dimensionality fix is free of risk.

**Tension with Node 5:** The ensemble uses more LP SRAM and more LP computation time. The budget is tight (4,400 bytes free, 10ms wake cycle). Feasible but not free.

---

## Node 3: Agreement Can Gate Plasticity

The agreement mechanism already gates gate bias: amplify when confident, defer when uncertain. The same signal can gate weight updates: learn when confident, freeze when uncertain.

This bounds the corruption risk. During transitions (agreement low), no weights change. During stable perception (agreement high), the current state is a reliable training signal. The Hebbian rule only fires when the system is confident it knows what it's looking at.

**Why it matters:** This transforms Pillar 3 from "always learning" to "learning when safe." The agreement signal is already computed (zero additional cost). The corruption risk is bounded by the quality of the agreement signal, which is empirically strong (94-96% duty cycle during stable patterns, 0% during transitions).

**Tension with Node 1:** Even agreement-gated learning can corrupt weights if the agreement signal is wrong. At 96% accuracy, 4% of packets are misclassified with high agreement (the system is confidently wrong). A Hebbian update during a confident-but-wrong packet bakes the error into the weights.

---

## Node 4: The VDB Already Solves the Problem

The LP degeneracy is routed around, not resolved. The VDB query uses 67% GIE hidden state, which IS pattern-distinct. Retrieved memories carry the correct LP portion. The LP state is modulated through episodic retrieval, not CfC projection.

This is the CLS architecture working as designed: the hippocampus (VDB) compensates for the neocortex's (CfC) inability to discriminate rapidly. The question is whether this compensation is sufficient or whether the neocortex must eventually learn.

**Why it matters:** If VDB compensation is sufficient for all downstream uses (gate bias, LP divergence metric, future SAMA coordination), then the degeneracy is cosmetic. LP Hamming P1-P2 = 0 looks bad in a paper table but doesn't affect system behavior.

If any downstream use REQUIRES the LP CfC to separate P1 and P2 without VDB assistance, then the degeneracy is functional and must be resolved. Current known uses: gate bias (uses agreement with running sum, which is pattern-specific from accumulation), SAMA (uses LP state for inter-agent coordination — unclear if degeneracy propagates).

---

## Node 5: The Ensemble Is the Conservative Path

Two LP CfC networks with independent random seeds. Combined hidden state: 32 trits. No weight updates.

**Cost:**
- LP SRAM: weights double from 384 to 768 bytes (within 4,400 byte budget)
- Computation: 64 intersections per step instead of 32 (each is ~40 cycles on LP core — 64 × 40 = 2,560 cycles at 16 MHz = 160µs, well within 10ms wake)
- VDB snapshot dimension: 32 GIE + 32 LP = 64 trits (up from 48). Node size goes from 32 to 40 bytes. 64 × 40 = 2,560 bytes (up from 2,048). Fits in LP SRAM.

**Benefit:** Two independent random projections. Probability of BOTH collapsing the same pair is the square of the single-projection collapse probability. If single-projection collapse is ~30% for P1-P2, dual-projection collapse is ~9%.

**What doesn't change:** W_f hidden = 0. TriX classification. Gate bias mechanism. Agreement computation. Everything structural stays intact.

---

## Node 6: There Is a Middle Path — Wider LP, Not Deeper

Instead of two independent 16-neuron CfCs, use one 32-neuron CfC. Same random seed. More neurons = more projection directions from the same seed. The extra 16 neurons capture additional directions that the original 16 missed.

This is simpler than the ensemble (one CfC instead of two, one set of weights, one VDB snapshot format) but provides less diversity (same seed, correlated directions). It may or may not resolve the P1-P2 degeneracy depending on whether the additional directions happen to separate them.

**Why it matters:** It's the minimum-change version of the dimensionality fix. If it works, no ensemble needed. If it doesn't, the ensemble is still available. Test this first.

**Cost:** Same as ensemble for SRAM and computation. Simpler implementation.

---

## Node 7: The Hebbian Rule Has a Natural Partner — Sleep Consolidation

The ROADMAP mentions "offline replay during sleep" as the CLS consolidation mechanism. If the Reflex enters a sleep/idle period (no packets for >10 seconds), the LP core could replay VDB memories against the CfC weights and apply Hebbian updates offline. This has the same effect as online Hebbian learning but without the race condition (no ISR running during sleep, no DMA descriptors being read).

**Why it matters:** It separates the learning mechanism from the real-time perception loop. Learning happens between sessions, not during them. The structural guarantees hold during active perception. The weights change only during explicit consolidation phases.

**Tension with Node 3:** Agreement-gated online learning is more responsive (adapts within a session). Sleep consolidation is safer (no race conditions) but slower (adapts between sessions). For a pattern that changes mid-session, only online learning can track it. For a pattern that changes between sessions (different sender, different environment), consolidation is sufficient.

---

## Key Tensions

| Tension | Nodes | Resolution Direction |
|---------|-------|---------------------|
| Structural safety vs adaptability | 1, 3 | Agreement-gated learning OR ensemble (no learning) |
| Dimensionality vs learning | 2, 4 | Test ensemble first; Pillar 3 only if dimensionality insufficient |
| LP SRAM budget | 5, 6 | Both fit; wider LP is simpler, test first |
| Online vs offline learning | 3, 7 | Sleep consolidation is safer; online is more responsive |
| Degeneracy as cosmetic vs functional | 4, 1 | Depends on downstream use — gate bias works, SAMA unclear |
