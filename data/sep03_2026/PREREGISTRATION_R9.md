# R9 Campaign — Pre-registration

**Written before any campaign data was collected.** Committed while the first
run is still queued.

## Why

Audit R9: every `±` in the papers is a *within-session* SD, while the
between-session term is several times larger (sign 3.57±0.40 vs 0.58±0.43 for
the same configuration, t=9.86). Within-session reps estimate rep noise, not
measurement uncertainty. Every headline figure therefore has error bars that
measure the wrong thing.

Audit R8 (now fixed): the divergence measurements binned on three different
variables. All measurement binning is now ground truth.

## Independence mechanism, and its limit

Sessions are separated by a **hard reset of the sender**, which restarts its
firmware — resetting the sequence counter to zero and re-drawing the phase
alignment between that counter and the 5 s pattern schedule. That alignment is
the hypothesised driver of R9's between-session variance (see B5).

**Limit, stated up front:** a firmware reset is not a power cycle. Thermal state,
RF environment and board temperature do not reset. This campaign therefore
tests the sequence-phase component of session variance and *not* all of it.
Reported intervals are lower bounds on true between-session spread.

## Design

All runs use ground-truth binning (R8 default) and label-free input
(`MASK_PATTERN_ID=1 MASK_PATTERN_ID_INPUT=1`).

**Claim 3 — VDB causal necessity.** 3 sessions of Tests 12/13. Per session:
CMD 5 (TEST 12) and CMD 4 (TEST 13) P1–P2 Hamming; contribution = CMD5 − CMD4.

**Claim 2 — divergence vs null floor.** 3 sessions each of the sequence-present
Control arm and the null-shuffle arm. Primary endpoint: MTFP /80, Control arm.

Sender hard-reset between every session.

## Declared analysis

- The reported statistic is the **between-session** mean ± SD, n=3 sessions.
  Within-session rep SD is reported separately and never as the uncertainty.
- Claim 3 supported only if the VDB contribution is **> 0 in the majority of
  sessions**. Currently 0 of 1 under correct binning.
- Claim 2 supported only if the Control arm exceeds the null floor with the
  between-session interval excluding zero difference.
- **n=3 sessions is the declared stopping point.** No adding sessions because
  the answer was not the one wanted. If the interval is inconclusive, report the
  interval and the n required — do not accumulate.

## Committed in advance, regardless of outcome

1. Whatever these produce **replaces** the published figures. The existing
   numbers were computed under mixed binning with within-session error bars and
   cannot be repaired by re-labelling.
2. A negative or inconclusive result is publishable and will not be buried.
3. n=3 sessions is small. If the between-session SD is large, the honest output
   is "this measurement needs more sessions than we ran", stated as such.
4. The firmware-reset limitation above is reported wherever these numbers appear.
