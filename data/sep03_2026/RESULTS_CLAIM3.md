# R9 Campaign — Claim 3 (VDB causal necessity)

3 independent sessions, sender hard-reset between each, **ground-truth binning**
(R8 fixed), label-free input. Tests 12/13, `SKIP_TO_13`.

| Session | CMD 5 (blend) | CMD 4 (no blend) | VDB contribution |
|---|---|---|---|
| 1 | 1 | 2 | **−1** |
| 2 | 0 | 4 | **−4** |
| 3 | 1 | 0 | **+1** |

**Between-session: −1.33 ± 2.52 trits, t(2) = −0.92, not significant.**

## Verdict against the pre-registered criterion

> *Claim 3 supported only if the VDB contribution is > 0 in the majority of sessions.*

**1 of 3 sessions positive → NOT SUPPORTED.**

Published claim: *+1 trit, "the episodic memory is causally necessary — ablation
collapses what VDB feedback separates."*

## Reading it precisely

The contribution is **not distinguishable from zero** (t = −0.92). It is also not
reliably negative. What the campaign rules out is the *published* claim: a
positive, reproducible VDB contribution. Across three sessions the sign is not
stable and the magnitude spans −4 to +1.

The between-session SD of **2.52 trits dwarfs the published effect of +1**. That
is R9 stated in the units of Claim 3: the original finding sits comfortably
inside the session-to-session noise that the original error bars never measured.

**This does not show the VDB does nothing.** It shows that this measurement, at
this sample size and this metric resolution, cannot detect what the papers
claimed to have detected. TEST 13's original result (CMD 4 collapsing P1=P2 in
2 of 3 runs) may still describe something real about the CfC projection
degeneracy — but the *attribution of a positive contribution to the VDB blend*
is not supported.

## Limits

n=3 sessions. Hamming values of 0–4 over a 16-trit vector: one trit is one
neuron changing sign. Sessions separated by sender firmware reset, not power
cycle, so thermal and RF state carry over — the true between-session spread is
likely **larger** than 2.52, not smaller.
