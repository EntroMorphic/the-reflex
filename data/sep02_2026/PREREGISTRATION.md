# Pre-registration — written BEFORE the n=5 data landed

**Timestamp:** see the commit that introduces this file. It is committed while
`n5_seq.log` is still capturing and before any n=5 number has been read.

Purpose: fix the interpretation in advance so the result cannot be rationalised
after the fact. This session has already caught three post-hoc rationalisations
(664 Hz, "consistently harmful", nearly calling 8.4 a null). This is the
counter-measure.

## The contrast

Primary: **VDB-only Control arm, MTFP /80**, sequence-present config vs
null-shuffle config, n=5 each, Welch two-sided, α = 0.05.

At n=3 this sat at t = 3.15 against crit 3.18 — undecided.

## Declared outcomes

| Outcome | Reading | Consequence for the papers |
|---|---|---|
| **Clears null, p < 0.05** | The divergence is real signal above the metric's sampling floor. | Stratum 1 Claim 2 survives, **restated**: divergence requires per-packet input entropy and the published account mis-attributed its source. The "from VDB feedback alone" phrasing still must go. |
| **Does not clear, p ≥ 0.05** | The headline number is not separable from its own noise floor at n=5. | Claim 2 falls with Claim 3. Stratum 1 cannot be submitted on its present thesis. |
| **Ambiguous (0.05 ≤ p < 0.10)** | Underpowered still. | **Declare it undecided. Do not spin either way.** Report the interval and the required n. |

## Committed in advance

1. **The null is 4.3 ± 1.9/80, not the ~1 asserted in README.md.** That stands
   regardless of the n=5 outcome, and the README claim is wrong either way.
2. **"From VDB feedback alone" is unsupportable in every outcome.** A field with
   zero pattern information reproduces 68% of the MTFP effect. Even if the
   number clears its null, the causal attribution in the current text does not.
3. **Sign-space is not a fallback.** It clears its null decisively (t = 13.1)
   but it is the metric that depends on the sequence counter (3.6 → 0.2 masked;
   noise recovers only 32%). Retreating to sign-space if MTFP fails would be
   choosing the metric that carries the leak. Ruled out now, in writing.
4. **A null result is a publishable result** and will not be buried. Two
   downstream mechanisms already came back null; a third null on the baseline
   changes the paper, not the honesty of it.
5. **n=5 is the pre-committed stopping point for this contrast.** No running
   further reps *because the answer was not the one wanted*. If n=5 lands
   ambiguous, the required n gets computed from the observed SD and reported —
   not quietly accumulated until significance appears.

## Secondary runs, interpretation fixed in advance

- **MASK_RSSI_INPUT / MASK_GAP_INPUT.** If masking either collapses divergence
  comparably to masking sequence, then MTFP divergence is driven by input
  variation *from any source* and is not a measure of pattern structure. If
  neither collapses it, the sequence field was special and the leak reading
  strengthens.
- **TEST 13 × 3 paired.** The n=1 inversion (+3 → −2) is confirmed only if the
  VDB contribution is ≤ 0 in the majority of masked runs. A single positive
  masked run is enough to downgrade it to "unreplicated".

## Standing caveat, true in all outcomes

Everything here is one board pair, one RF environment, one session. More reps
tighten intervals; they do not establish generality. That limitation survives
every result below and belongs in the paper regardless.
