# R9 Campaign — Claim 2 (divergence above the null floor)

3 **paired** sessions: both arms measured inside each session, sender hard-reset
only *between* sessions. Ground-truth binning (R8 fixed), label-free, TEST 15
Control arm only.

| Session | sequence present | null (shuffled bins) | difference |
|---|---|---|---|
| 1 | 8.4 ± 1.8 | 3.5 ± 2.2 | 4.9 |
| 2 | 9.7 ± 1.1 | 4.1 ± 0.7 | 5.6 |
| 3 | 11.7 ± 0.9 | 4.6 ± 1.4 | 7.1 |
| **mean** | **9.93 ± 1.66** | **4.07 ± 0.55** | **5.87 ± 1.12** |

*(the ± inside each cell is the within-session rep SD; the ± on the means is the
between-session SD, which is the one that counts)*

**Paired t(2) = 9.04, crit 4.30 → SIGNIFICANT.** 95% CI on the difference:
**3.07 to 8.66**, excluding zero.

## Verdict: SUPPORTED

Against the pre-registered criterion — *supported only if the Control arm
exceeds the null floor with the between-session interval excluding zero* — this
passes, and not marginally.

**The published central value replicates.** 9.7 published, 9.93 measured across
three independent sessions. What was wrong was the error bar (±0.6 within-session
vs ±1.66 between-session) and the absence of a null, not the number.

## Why this differs from the n=5 within-session result

The earlier n=5 comparison gave p = 0.079 — undecided. That comparison was
**unpaired across a sender reboot**: the sequence arm came from one session and
the null from another, so the between-session term (R9) sat entirely inside the
contrast and swamped it.

Pairing the arms within each session removes that term. The effect was there the
whole time; the earlier design could not see it. This is R9 cutting the other
way — bad pairing hides real effects as readily as it manufactures false ones.

## What this does and does not establish

**Establishes:** LP divergence in the sequence-present configuration is reliably
and substantially above the metric's own sampling floor. ~5.9 trits of the
~9.9/80 is signal, not noise. The measurement is detecting *something* real.

**Does not establish that the something is pattern structure.** B5's random
control still stands: a per-packet field carrying *zero* pattern information
reproduces 68% of the effect. So the divergence is real but is driven largely by
input variation rather than by pattern-specific content.

The two results are consistent and belong together:

> The temporal layer produces a measurable, reproducible signal well above
> chance — **and most of that signal does not depend on what the patterns are.**

## Correct form of the claim

Not: *"8.5–9.7/80 MTFP divergence from VDB feedback alone."*

But: *"9.9 ± 1.7/80 across three independent sessions, against a measured
shuffled-bin null floor of 4.1 ± 0.6 (paired difference 5.9, 95% CI 3.1–8.7).
A pattern-agnostic per-packet field reproduces roughly two-thirds of the effect,
so the metric reflects input variation as much as pattern structure."*

That is a weaker claim about mechanism and a **much stronger claim about
measurement** — it is the first version of this number with a null, a
between-session interval, and a stated confound.

## Limits

n=3 sessions. Sessions separated by sender firmware reset, not power cycle, so
thermal and RF state carry over — true between-session spread is likely larger.
One board pair, four patterns, one sender.

---

# RED-TEAM OF THIS RESULT

Attacked deliberately, because Claim 2 came back positive and that is the result
most at risk of insufficient scrutiny. Four real problems; the conclusion
survives, weakened in one specific way.

## RT1. Order confound — in my own design

Phase 2 ran `for ARM in seq null` in **all three sessions**. The sequence arm
always went first, the null arm always second, ~12 minutes later. **Never
counterbalanced.**

This is exactly the flaw recorded as audit finding D7 against TEST 14 ("the
three conditions always run in the same order, so drift is confounded with
condition"). I raised it, then reproduced it.

**Mitigating evidence, and it is strong.** The null arm was independently
measured on Sept 2 *running alone*, not after a sequence arm, and gave 4.3. The
Phase 2 nulls, all in second position, gave 3.5 / 4.1 / 4.6. Across four
measurements spanning two orderings the null is **4.12 ± 0.46**. If order were
driving the difference, the null would move with position. It does not.

A counterbalanced replication is still owed.

## RT2. The shuffled null decorrelates samples; the real condition does not

`NULL_SHUFFLE_BINS` assigns each sample to a uniformly random bin, so every bin
draws interleaved samples spanning the whole run. In the real condition each
pattern's samples arrive in 5-second blocks. If the LP state drifts on a
timescale comparable to the cycle, real bins inherit block structure the null
does not, and the floor may be **understated**.

Partly mitigated: the sender cycles all four patterns every ~20 s, so over a 60 s
phase each pattern samples three separated blocks spanning the phase, and drift
affects all four similarly.

**Not quantified.** A stricter null would shuffle *which pattern label is
assigned to each 5 s block* rather than per sample, preserving temporal
structure. That is the right next control.

## RT3. The confound I use to qualify Claim 2 rests on the weaker design

B5's "a pattern-agnostic field reproduces 68% of the effect" comes from
comparing random (8.4) against masked (2.1) against sequence (11.3) — all
**within one session, unpaired**, on Sept 2.

That is the *same design weakness* that made Claim 2 look undecided at p = 0.079
before pairing fixed it. The 68% figure could be materially over- or
under-stated. **It should not be quoted with the confidence of the Phase 2
result until it is re-measured with paired sessions.**

This matters: it is the number doing the work of qualifying an otherwise
positive claim.

## RT4. Effect exists; magnitude is loosely constrained

Paired t(2) = 9.04 rests on 2 degrees of freedom. The 95% CI on the difference is
**3.07 to 8.66** — a factor of nearly three. The *existence* of an effect above
the floor is solid; its size is not.

## Pooling every session ever measured

| arm | sessions | pooled |
|---|---|---|
| sequence | 11.3, 6.16, 8.4, 9.7, 11.7 | **9.45 ± 2.26**, range 6.2–11.7 |
| null | 4.3, 3.5, 4.1, 4.6 | **4.12 ± 0.46**, range 3.5–4.6 |

The null is stable across sessions *and* orderings (spread 1.1 trits). The
sequence arm is not (spread 5.5 trits). The separation holds under pooling —
~5.3 trits — but the sequence arm's session-to-session variance is larger than
Phase 2 alone implies, which further widens any magnitude claim.

## Verdict after red-team

**Claim 2 survives as an existence claim.** LP divergence is reliably above the
metric's sampling floor; the floor is well-characterised and stable; the
separation holds across five sessions and two orderings.

**It does not survive as a magnitude claim**, and the qualifying confound (B5's
68%) is measured with a design now known to be unreliable. The correct paper
sentence asserts separation from a measured null, states the wide interval, and
flags the confound as *estimated on weaker evidence than the main result*.
