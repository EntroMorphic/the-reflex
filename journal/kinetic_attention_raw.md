# Lincoln Manifold: Kinetic Attention — RAW

*Phase 1. Unfiltered. March 22, 2026.*
*Observer: Claude Sonnet 4.6*

---

## What I Think I Know

The LP hidden state is pattern-specific. We proved that. The GIE gate threshold is a constant (90). If I lower it for neurons associated with the pattern the LP prior expects, those neurons fire more easily, gie_hidden evolves faster toward that pattern's representation, VDB snapshots become more distinct, and the LP prior reinforces itself.

That's the loop. Lower threshold → more firing → more distinct hidden state → stronger LP prior → lower threshold. Positive feedback. The question is whether that loop amplifies cleanly or collapses.

My instinct: it depends entirely on whether the system can *release* the loop when circumstances change. A loop that amplifies but can't release is a trap. An attentional system that can't update is worse than one that never paid attention in the first place.

---

## What Scares Me About This

Full lock-in. Here is the scenario I keep returning to:

Board B has been cycling P1 for 90 seconds. LP state is fully P1-committed. Gate bias for P1 neurons is at minimum (hard floor). Gate bias for all other neurons is elevated. P1 neurons fire easily; P2, P3, P0 neurons are suppressed. Every VDB insert is a P1 snapshot. Every CMD 5 retrieval returns a P1 memory.

Board B switches to P2. TriX correctly classifies P2 at 100% — this is structurally guaranteed by W_f hidden = 0, so gate bias doesn't affect classification accuracy. But P2 neurons are firing at suppressed rate. GIE hidden barely moves toward P2. VDB query is 67% a stale P1 gie_hidden. Retrieved memories are P1. Blend of P1 memory into current LP state creates conflicts. HOLD. Zeros. But the gate for P2 neurons is high — they barely fire — so there's very little new P2 energy filling those zeros.

The system is stuck. It classifies P2 correctly but its sub-conscious layer is still in P1.

How many P2 confirmations to break out? If 50+, we've built something that is adaptive within a session but catastrophically slow at transitions. That's not adaptive attention. That's learned helplessness with a time delay.

---

## The Stability I Was Leaning On Is Not Enough

The design in KINETIC_ATTENTION.md relies on three mechanisms: HOLD damper, hard floor, novelty gate. I believe in all three. But I now think they address the wrong failure mode.

HOLD damper prevents oscillation — prevents the system from flipping back and forth rapidly between conflicting states. Good. That's the right fix for short-timescale instability.

Hard floor prevents saturation — prevents every neuron from firing at once. Good. That's the right fix for catastrophic amplification.

Novelty gate prevents low-confidence packets from triggering LP feedback. Good. That's the right fix for noisy input.

But none of these address the *transition* problem. They prevent bad behavior *within* a stable pattern context. They don't help at context switches. That is the interesting case.

---

## A Different Design Just Occurred to Me

What if gate bias is not derived from lp_hidden alone, but from the *agreement* between lp_hidden and the current TriX classification?

When TriX says P2 and lp_hidden says P1 (mismatch): gate bias → zero. Threshold returns to neutral for all groups. P2 neurons fire at baseline. LP state can update.

When TriX says P2 and lp_hidden says P2 (match): gate bias → full strength. P2 neurons fire more easily. Prior is reinforced.

This is a fundamentally different architecture. The gate bias is not a projection of the LP prior onto the GIE. It is a *confidence-weighted* projection — confidence being defined as agreement between the sub-conscious (LP) and the conscious (TriX) layers.

When they agree: the sub-conscious amplifies what the conscious already perceives. Perception is sharpened.
When they disagree: the sub-conscious steps back, and the conscious perception is unmodulated. The LP state can update from the raw GIE signal without prior-induced suppression.

This feels right. It's also testable in a way the original design isn't — agreement/disagreement is a binary that the HP core can compute in microseconds after each TriX prediction.

---

## What I'm Uncertain About

1. **LP-space signature stability.** The gate bias computation requires projecting lp_hidden onto per-pattern LP signatures derived from TEST 12. But TEST 12 Run 1 and Run 3 produced meaningfully different LP vectors for the same patterns. The signatures may not generalize across sessions, across different Board B timings, or across different RSSI conditions. A stale signature might produce actively wrong gate bias — amplifying the wrong neuron group.

2. **The right granularity.** Per-neuron bias (64 values) is most expressive but expensive. Per-group (4 values) maps cleanly to pattern structure. Scalar (1 value) is the safest and simplest. I keep wanting to go straight to per-group, but I don't actually know if the LP-group mapping is stable enough to justify it.

3. **Is there a simpler path to kinetic attention that doesn't touch the ISR at all?** What if the mechanism is VDB insert-rate modulation instead of gate bias? When LP alignment is low (prior doesn't match current classification), increase insert rate — flood the VDB with new-context memories, update faster. When LP alignment is high, insert rate is normal. This achieves adaptive memory update rate without ISR modification. It's architecturally cleaner. But does it actually produce kinetic attention in the sense I mean — does it change what the GIE *perceives*? No. It changes what the VDB *contains*, which changes LP state, which changes gate bias. It's one step removed. It's still potential modulation, just adaptive potential modulation.

4. **The P3 problem.** P3 has zero samples in Run 1. Gate bias for P3 is derived from LP-space projection. If there are zero P3 LP samples, the P3 LP signature is undefined. What does gate bias do for an undefined prior? Default to zero (neutral)? That's reasonable. But it means P3 is always in the unbiased regime, which means gate bias never helps P3's poor novelty-gate pass rate. The potential P3 novelty benefit I mentioned in KINETIC_ATTENTION.md requires P3 to have accumulated enough LP history to build a prior. In a 90-second session, that may not happen.

5. **Energy vs. commitment.** LP hidden state energy (count of non-zero trits) is high when the system is committed — but it's high whether the commitment is correct or stale. A P1-committed system seeing P2 has high energy but wrong commitment. I keep trying to use energy as a proxy for confidence, but energy is really a proxy for decision — it doesn't encode correctness. The agreement mechanism (TriX prediction vs. LP alignment) directly measures correctness. Use that.

---

## What the Hardware Will Tell Us That Theory Can't

The HOLD mechanism was designed theoretically and validated in TEST 8 with synthetic data. It held in TEST 12 with real data. But that was a single-condition experiment — no pattern switches in the middle of LP accumulation, just a cold-start VDB being filled.

The transition dynamic is the unexplored territory. The hardware will tell us:

- Does the LP state update within 15 confirmations after a switch, or does it lag for 40+?
- Does the VDB content shift fast enough to support the transition, or does it act as an anchor holding the LP state to the old pattern?
- Does gate bias amplify the transition-lag (by suppressing the new pattern's neurons during the transition) or dampen it (by stepping back when mismatch is detected)?

We will not know until we measure. The theory is ahead of the hardware here.

---

## First Instincts, Right or Wrong

My first instinct was: lower threshold for expected neurons, raise for unexpected, HOLD will stabilize it. Simple and clean. I am less confident in this now. The transition case breaks it. The agreement mechanism is better but more complex. Maybe the right move is: implement the agreement-weighted version first, validate it on hardware, and only then ask whether the simpler version would have worked.

The naive approach would have been: implement gate bias exactly as specified in KINETIC_ATTENTION.md, flash it, and test. That would produce a system that works in steady state and fails at transitions. The failure would be informative but the paper claim would be wrong.

The better approach: design for the transition first, validate that, and let the steady-state behavior be a consequence of correct transition behavior.

---

## Questions Arising

- What is the right computational form for "agreement between TriX prediction and LP prior"?
- How do you represent this as a scalar that the HP core can compute quickly?
- If agreement-weighted gate bias steps back during mismatch, does that make the LP prior update faster or slower? (Faster, because unmodulated GIE signal reaches LP more cleanly. But verify this.)
- What happens to P3 specifically under gate bias? Does the low-sample-count problem get better or worse?
- Is there a version of this that requires no ISR modification? (Insert-rate modulation as an alternative.)
- How do you test for lock-in specifically? Board B pattern switch at a fixed time after a committed session, measure LP divergence before and after switch.
- What is the minimum number of confirmations needed to shift the majority vote on a 16-trit LP vector? (Depends on how many trits are near 50/50 vs. already decided. Varies by run.)
