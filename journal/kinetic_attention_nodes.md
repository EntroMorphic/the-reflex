# Lincoln Manifold: Kinetic Attention — NODES

*Phase 2. Grain identification. March 22, 2026.*
*Observer: Claude Sonnet 4.6*

---

## Node 1: The Gate Bias Loop Is a Positive Feedback Attractor

Gate bias lowers the firing threshold for neurons associated with the expected pattern. More neurons fire → gie_hidden develops faster → VDB snapshots more distinct → LP prior stronger → gate bias lower → more firing. This is a positive feedback loop with no natural bound except the hard floor on threshold and the HOLD damper.

**Why it matters:** Positive feedback attractors are not inherently bad — the brain uses them constantly. But they have a critical property: once the system enters an attractor state, it requires energy to leave. The question for kinetic attention is not "does the loop amplify?" (it will) but "what are the escape conditions?"

**Tension with Node 4:** The attractor is the feature in steady state. It is the failure mode at transitions.

---

## Node 2: Classification Accuracy Is Structurally Decoupled from Gate Bias

W_f hidden = 0. Therefore f_dot = W_f[:input_dim] @ input. f_dot is input-only. TriX scores are sums of f_dot by neuron group. TriX classification is argmax of those sums. None of this is affected by which neurons fire — gate bias changes h (gie_hidden), not f_dot. So gate bias cannot affect TriX accuracy, regardless of magnitude.

**Why it matters:** This is an exact structural guarantee, not an empirical finding. It should be stated as such in any paper claiming 100% classification accuracy under gate bias. The decoupling is architectural, not probabilistic.

**Dependency:** Holds only as long as W_f hidden = 0. If Phase 5 ever transitions to learned CfC weights (Pillar 3), this guarantee breaks.

---

## Node 3: LP-Space Signatures May Not Generalize Across Sessions

The per-group gate bias requires projecting lp_hidden onto LP-space pattern signatures — the mean LP hidden state per pattern from TEST 12. But TEST 12 Run 1, Run 2, and Run 3 produced meaningfully different LP vectors for the same patterns. P0's mean in Run 1: `[++++0--++++-++++]`. Run 3: `[+--+0++++-++--+-]`. Hamming distance between these is substantial.

**Why it matters:** If LP signatures vary across sessions, a gate bias derived from stored Run 3 signatures might actively mislabel the LP prior in Run 4. The gate bias would amplify the wrong neuron group. The system would have kinetic attention, but attending to the wrong thing.

**Resolution options:**
- (A) Compute LP signatures in-session from the first N classified samples, not from stored historical means.
- (B) Use only the scalar bias (LP energy → uniform threshold reduction), which doesn't require signature stability.
- (C) Use the agreement mechanism (Node 6) which also doesn't require stored signatures.

**Tension with Node 7:** In-session signature computation requires an observation window before gate bias activates — the system is blind during that window.

---

## Node 4: The Transition Boundary Is the Critical Case

After 90 seconds of P1 exposure, LP state is P1-committed. Gate bias is maximally P1-favorable. Board B switches to P2. P2 neurons fire at suppressed rate (high effective threshold). VDB query is dominated by stale P1 gie_hidden. Retrieved memories are P1. LP blend creates conflicts → HOLD → zeros. But P2 neurons are barely contributing new energy to fill those zeros. The system is attentionally locked to P1 while correctly classifying P2.

**Why it matters:** This is the failure mode that distinguishes a genuinely adaptive system from a committed-but-brittle one. If lock-in time exceeds ~20 confirmations, the system is not adaptive at the timescale of Board B's pattern cycle (27 seconds). It would spend most of each pattern window recovering from the previous one.

**Key unknown:** How many P2 confirmations does it take to shift the LP majority vote enough to flip the gate bias from P1-favorable to neutral? Depends on how many trits are near 50/50. Cannot be determined analytically — requires a dedicated lock-in experiment.

---

## Node 5: The Agreement Mechanism Resolves the Transition Problem

If gate bias is weighted by agreement between the LP prior and the current TriX prediction:

- When TriX says P2 and LP prior says P1 (disagreement): gate bias → zero. Threshold is neutral for all groups. P2 neurons fire at baseline. LP state updates from the raw GIE signal.
- When TriX says P2 and LP prior says P2 (agreement): gate bias → full strength. P2 neurons fire more easily. Prior is reinforced.

This gives the system a natural release valve. The moment TriX detects a new pattern, the gate bias stops suppressing the new pattern's neurons, and LP state can update. The transition time is reduced from "however long it takes to shift the accumulator majority vote" to "immediately upon TriX detection."

**Why it matters:** It resolves Node 4 without requiring a decay mechanism on the LP accumulator. The TriX signal — which is fast (705 Hz) and accurate (100%) — acts as an automatic attentional gate. When the sub-conscious (LP) and conscious (TriX) layers disagree, the sub-conscious defers.

**Tension with Node 8:** The agreement mechanism requires the HP core to compute LP-space alignment with the TriX prediction on every confirmed packet. Is this fast enough? At the current confirmation rate (~4 Hz during active reception), yes — 250ms between confirmations. The computation is a 16-element ternary dot product, which is negligible.

---

## Node 6: Agreement Is Computable Without Stored LP Signatures

Agreement between LP prior and TriX prediction can be computed as:

`agreement[p] = trit_dot(lp_now, lp_target[p])`

where `lp_target[p]` is the "expected" LP state if pattern p is active. But we're back to needing an LP signature.

Alternative: use the TriX prediction directly. After TriX predicts pattern `p_hat`, compute the LP alignment score as:

`score = trit_dot(lp_sum_per_pattern[p_hat] (normalized), lp_now)`

where `lp_sum_per_pattern[p_hat]` is the *current session's running LP accumulator for pattern p_hat* — not a stored historical signature. If the LP accumulator for p_hat agrees with lp_now, the prior is aligned with the prediction. No stored signatures needed.

**Why it matters:** This is fully in-session. The agreement signal is derived entirely from current data. It handles LP signature variability (Node 3) automatically because it uses the session's own accumulator.

**Dependency:** Requires that the LP accumulator has enough samples for the predicted pattern before the agreement signal is meaningful. In the early part of a session, agreement scores will be noisy. This is acceptable — gate bias starts low and increases as the session matures.

---

## Node 7: Cold Start and the Observation Window

In the first 15–30 confirmations of any pattern, the LP accumulator doesn't have enough samples to produce a stable LP mean. Gate bias derived from an unstable prior may be noise-driven. This argues for a cold-start regime where gate bias is disabled (= zero) until a minimum confidence threshold is met.

**Why it matters:** Without a cold-start guard, the gate bias might actively misdirect attention during the critical early window when the system is first learning the session's LP distribution. This is especially risky for P3, which rarely accumulates 15 samples even in a 90-second session.

**Resolution:** Add `bias_active = (max_lp_n >= MIN_BIAS_SAMPLES)` as a prerequisite for applying gate bias. Below the threshold, run the system in unbiased mode (identical to TEST 12/13 baseline). This creates a natural "ramp-in" for gate bias as the session matures.

---

## Node 8: VDB Content Is the Slower Problem

Gate bias changes what the GIE perceives and how fast gie_hidden evolves. But the LP state is also shaped by what VDB retrieval returns. During a P1→P2 transition with the agreement mechanism active: gate bias steps back (mismatch), so P2 neurons fire at baseline. GIE hidden starts evolving toward P2. But CMD 5 retrieval is still searching a VDB full of P1 memories. The query is 67% gie_hidden — which is now P2-influenced — but the nearest neighbor might still be a P1 snapshot with some P2-overlap in GIE space.

The LP blend then mixes P2-current-state with P1-retrieved-memory. Conflict → HOLD → zeros. Then gap-fill from the next P2 confirmation's retrieved memory (probably still P1). Recovery is still slow.

**Why it matters:** Even with the agreement mechanism, the LP state transition is bottlenecked by VDB content. Gate bias helps the GIE layer update fast. VDB retrieval may still hold the LP layer to the old pattern.

**Resolution options:**
- (A) Faster VDB turnover on mismatch: when agreement is low, insert a snapshot at every confirmation instead of every 8th.
- (B) Active VDB pruning on mismatch: when agreement is low, mark the N most-recent VDB nodes (likely old-pattern) as low-priority for retrieval.
- (C) Accept the VDB lag and measure whether it is within acceptable bounds.

**Tension with Node 4:** Option A (faster inserts on mismatch) is the most tractable. But it fills the VDB faster with transition-state snapshots — ambiguous mixed P1/P2 states that may not be useful for either pattern's retrieval. These "scar" memories might persist and degrade retrieval quality for multiple cycles.

---

## Node 9: The P3 Novelty Benefit May Be Real But Requires Samples

KINETIC_ATTENTION.md speculated that gate bias might improve P3's novelty-gate pass rate by making P3 neurons fire more when the P3 prior is active. But P3 accumulates 0–15 samples per 90-second session. Below 15, there is no stable LP prior for P3. Gate bias for P3 is undefined or noise-driven.

The benefit is only available *after* P3 has been observed enough times to build a prior. In the current experimental setup (Board B's 2-second P3 slot in a 27-second cycle), this may never happen in a single session.

**Why it matters:** The P3 novelty benefit might require multi-session LP persistence — a P3 prior accumulated over multiple 90-second sessions, carried forward. The architecture currently resets LP hidden state between tests. Multi-session persistence is a new feature, not just a parameter change.

---

## Node 10: There Is a Simpler Path That Doesn't Touch the ISR

VDB insert-rate modulation: when LP-TriX agreement is low (transition), insert at every confirmation instead of every 8th. When agreement is high (steady state), insert at the normal rate. This adaptively floods the VDB with current-context memories during transitions, accelerating the shift in retrieval results without modifying the ISR at all.

**Why it matters:** No ISR modification means lower risk (the ISR is timing-critical; a bug there causes measurement errors), no new SRAM layout, no gate computation. The mechanism is entirely in the HP core's classification callback. The cost is faster VDB fill (64 nodes reached in ~8 confirmations during transitions rather than ~64). But during transitions, VDB fill rate is the feature, not the bug.

**Tension with Node 8:** This doesn't solve the full lock-in problem — gie_hidden still evolves slowly (threshold is unchanged). The LP state updates faster because VDB content shifts, but the GIE layer doesn't see the benefit. True kinetic attention (GIE perceives differently) requires gate bias.

**Resolution:** These are not mutually exclusive. VDB insert-rate modulation can run alongside gate bias. Treat them as two independent mechanisms addressing the same transition problem at different layers.

---

## Key Tensions

| Tension | Node A | Node B | Status |
|---------|--------|--------|--------|
| Amplification vs. transition agility | Node 1 | Node 4 | Resolved by Node 5 (agreement mechanism) |
| LP signature stability | Node 3 | Node 6 | Resolved by Node 6 (in-session accumulator) |
| Gate bias precision vs. ISR complexity | Node 2 | Node 8 | Partially resolved — per-group bias is sufficient |
| Cold start blind window | Node 7 | Node 1 | Resolved by gating on min sample count |
| VDB lag at transitions | Node 8 | Node 10 | Partially resolved — insert-rate modulation helps |
| P3 novelty benefit | Node 9 | — | Requires multi-session persistence, out of Phase 5 scope |
