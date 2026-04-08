# Lincoln Manifold: April 8 Findings — REFLECT

*Phase 3. Axe sharpening. April 8, 2026.*
*Observer: Claude Opus 4.6*

---

## Core Insight

**The system's ternary constraint is not a limitation to be compensated for. It is a structural invariant that must be preserved through every computation in the signal path. Every departure from ternary lost exactly the information that mattered.**

This emerged from two independent observations:
1. sign() quantization lost magnitude → P1-P2 degeneracy → resolved by MTFP (ternary encoding of magnitude)
2. Float agreement lost trit structure → transition headwind → resolved by ternary disagree-count

The pattern is not "ternary is good." The pattern is: **the information that distinguishes correct from incorrect behavior lives in the ternary structure, and any computation that collapses that structure becomes a bottleneck.**

This reframes the entire project. The ternary constraint didn't force the architecture. The architecture is ternary because the information it processes is ternary. The three blend modes (UPDATE/HOLD/INVERT) are not a workaround for the absence of gradients — they are the correct dynamics for a system where every signal is {agree, disagree, uncertain}. The HOLD-on-conflict rule is not a damper — it is the ternary representation of "I don't know." The disagree-count is not a threshold — it is a count of the trit positions where reality contradicts expectation.

---

## Resolved Tensions

### Does Kinetic Attention Add Value? (Nodes 4 and 5)

**Tension:** The VDB stabilization finding is robust across all conditions. The no-bias baseline is simpler and produces the fastest transitions. Gate bias helps 2/3 seeds, hurts 1/3, and the benefit (+1-2.5 Hamming points) is modest. Is kinetic attention worth the complexity?

**Resolution:** Kinetic attention adds value as an engineering contribution, not as a performance improvement. The contribution is:
1. A complete attentional loop (perceive→classify→remember→retrieve→modulate→perceive differently) running on peripheral hardware at 30 µA — the first demonstration that such a loop is physically possible in this substrate.
2. The agreement mechanism as an instantiation of prior-signal separation — the five-component architecture proven on silicon.
3. The ternary disagree-count as a structural disagreement detector — not a statistical threshold but a count of conflicting trit positions.

The performance improvement (LP divergence) is a secondary finding. The primary finding is that the mechanism exists, works within the substrate's constraints, and degrades gracefully (Seed B: bias holds rather than corrupting; the classifier remains 100% accurate regardless).

The paper should present kinetic attention as a demonstrated mechanism with honest performance bounds, not as an optimization.

### Mechanism vs Projection Failures (Node 3)

**Tension:** Same symptom (bias doesn't release), different causes. The temptation is to keep tuning the mechanism to handle degenerate projections.

**Resolution:** This is a boundary. The agreement mechanism operates on the LP hidden state. If the LP hidden state doesn't separate P1 from P2 (because the random projection is degenerate), the agreement mechanism cannot distinguish them. No amount of threshold tuning fixes this — the information is not present in the representation.

The right fix is in the representation (Pillar 3: Hebbian learning). The right boundary for the current paper is: "the mechanism works correctly on the data it receives; for degenerate projections, the data does not contain the distinction."

This is the same principle as W_f hidden = 0: the system's guarantees are structural, and they hold within their structural boundaries. The agreement mechanism guarantees release when 4+ trits disagree. It does not guarantee that 4+ trits will disagree. That depends on the projection.

### The Float Question (Node 8)

**Tension:** The last float in the mechanism path. Functionally irrelevant (rounds to int8 anyway). Philosophically relevant (the "no floating point" claim).

**Resolution:** Fix it. Not because it matters functionally, but because the principle matters. The system's claim is ternary/integer throughout. One float in the bias computation is a leaky abstraction. The integer version is `int b = (BASE_GATE_BIAS * margin + LP_HIDDEN_DIM/2) / LP_HIDDEN_DIM;` (with rounding). Trivial change. Complete the claim.

Do it in a follow-up commit, not today. Today's commit is about the three-bug fix and multi-seed data.

---

## What the Day Revealed About the Project

### The Three Strata Are Load-Bearing

Stratum 1 (engineering) produced the mechanism. Stratum 2 (CLS architecture) produced the prediction (stabilization) that motivated the experiment (14C) that revealed the bugs. Stratum 3 (prior-signal separation) produced the framework (five components) that guided the fix (component 4: disagreement detection must be structural, not statistical).

The strata are not just a publication strategy. They are three lenses on the same system, and today's work required all three:
- The engineering lens identified the bugs (CPU classifier, hardcoded accumulator, float collapse)
- The architecture lens motivated the experiment (multi-seed transition)
- The principle lens guided the fix (ternary structure preservation, structural disagreement detection)

### The LMM Cycle Structure Worked

The March 22 kinetic attention LMM designed the agreement mechanism correctly. The RAW pass identified the transition lock-in failure. The REFLECT pass resolved it with agreement-weighted bias. The implementation deviated from the design (float instead of ternary, CPU instead of TriX, hardcoded accumulator). Today's work brought the implementation back to the design.

The April 8 TriX dispatch LMM identified the contamination path and the hybrid dispatch solution. But the real fix (ternary agreement) came from outside the LMM — it came from the user asking "Is the bias decay signal in binary?" That question cracked the problem open. The LMM found the right direction. The human found the right frame.

### What Remains

1. **Full test suite validation.** All today's runs used SKIP_TO_14C with the transition sender. The full 15-test suite with the normal sender (4-pattern cycling) has not been run with TriX dispatch and ternary agreement. This is the immediate next step.

2. **UART-only verification.** Every run in the project's history used USB-JTAG. The "peripheral-autonomous" claim requires JTAG-free data. This is blocking for paper submission.

3. **Seed B.** The 22-step headwind is the honest limitation. The paper should report it as a projection boundary, not a mechanism failure. A cross-layer disagreement signal (TriX says P2 for N steps but LP hasn't moved) could help, but this is a Pillar 3 concern.

4. **The float.** One `float` remains in the bias computation. Integer replacement is trivial and should be done before paper submission.
