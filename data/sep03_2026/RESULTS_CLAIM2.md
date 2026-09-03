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
