# B5 Sequence-Trit Ablation — September 2, 2026

**Result: the back-channel is real.** Masking the sequence counter from the GIE
input collapses VDB-only LP divergence by 82% in MTFP space and 93% in sign
space, while leaving classification completely unchanged.

Hardware: Board A rx = C6 #3 (`B4:3A:45:8A:C8:24`, /dev/ttyACM0), Board B tx =
C6 #2 (`B4:3A:45:8A:C7:D4`, /dev/ttyACM2). Both runs same session, minutes
apart, identical firmware except the one build flag. Tree @ `05c821b`.

## Result

VDB-only baseline (TEST 15 **Control** arm, CMD 5, no Hebbian), n=3 reps each:

| Metric | Sequence PRESENT | Sequence MASKED | Drop |
|---|---|---|---|
| MTFP /80 | **11.3 ± 3.4** (7.7, 12.0, 14.3) | **2.1 ± 2.1** (2.0, 4.2, 0.0) | −9.3 (82%), t=4.06, df≈3.4 |
| sign /16 | **3.6 ± 0.4** (4.0, 3.2, 3.5) | **0.2 ± 0.4** (0.7, 0.0, 0.0) | −3.3 (93%), t=10.1, df≈4.0 |

Two of three masked reps show sign-space divergence of **exactly 0.0** — all four
patterns produce an identical LP hidden sign vector. That is the same collapse
signature TEST 13 reports for the CMD 4 ablation, reached here without ablating
anything.

For reference, the published baseline is 9.7 ± 0.6/80 — in family with this
session's 11.3, so this is not an anomalous session.

## The controls that make it interpretable

**Classification is unchanged.** 31/32 = 96% windowed for both Core and ISR in
*both* runs. Expected: signatures already mask trits ≥ 104
(`test_live_input.c:191`), so the classifier never used sequence. The leak is
input → GIE hidden → VDB snapshot → LP, exactly the path predicted in audit B5.

**The machinery is not degenerate.** Without sequence the LP still fires
(13.2/16 vs 15.1/16), VDB matches stay strong (score 29 vs 30–32), and the blend
still applies (1.4 vs 1.9 trits/step). The system is running normally and simply
fails to distinguish patterns.

**The loop rate is identical** (490 Hz both), so this is not a timing artifact.

**The flag demonstrably acts on the input:** with sequence present, 226/226
packets encoded as distinct inputs; masked, 233/234 — the first duplicate input
this system has produced.

## Mechanism — SEPARATED by the random control

A third condition was run in the same session: trits [104..119] filled with a
per-packet **pseudo-random** ternary block (`RANDOM_SEQUENCE_INPUT=1`) — same
width, same energy (all ±1, no zeros), same per-packet variability as the
sequence encoding, and **zero correlation with pattern identity or phase**.

| `[104..119]` | | MTFP /80 | sign /16 |
|---|---|---|---|
| sequence counter | variation + phase info | 11.3 ± 3.4 | 3.6 ± 0.4 |
| **random block** | variation, **no info** | **8.4 ± 2.3** | **1.3 ± 1.2** |
| zeroed | neither | 2.1 ± 2.1 | 0.2 ± 0.4 |

`random vs masked` isolates *variation*; `sequence vs random` isolates
*information*. Welch, n=3, t(.05,df≈2) = 3.18:

- **MTFP:** random vs masked **t = +3.53 (significant)**; random vs sequence
  t = −1.27 (not significant). Pure noise recovers **68%** of the effect
  (6.3 of 9.2 trits). The MTFP divergence is driven by input *variation*, and
  is not distinguishable from the condition carrying no pattern information.
- **sign:** random vs masked t = +1.56 (n.s.); random vs sequence t = −3.16
  (n.s. at df≈2, but the largest of the three contrasts). Noise recovers only
  **32%** (1.1 of 3.4). The sign-space separation did depend substantially on
  the counter's correlation with the sender's schedule.

**The two metrics are compromised in opposite directions.** MTFP is mostly
variation-driven and largely indifferent to whether the varying field carries
pattern information. Sign is mostly information-driven and collapses when the
phase correlation is removed.

### What the random arm is NOT

It is **not a null.** Payload [24..87] and MTFP21 gap history [88..103] remain
present and pattern-specific in all three conditions — the random arm removes
pattern information only from those 16 trits, not from the input as a whole.
8.4/80 must not be read as "divergence from nothing."

The true null requires shuffling the metric's *bins* — accumulating each sample
into a uniformly random pattern bin instead of ground truth, so all four means
are drawn from one distribution and the reported divergence is pure
finite-sample noise. Implemented as `NULL_SHUFFLE_BINS=1`; **not yet run.** It
directly tests `README.md`'s asserted "null ~1", which no run in this corpus
substantiates.

## Causal necessity (TEST 13) — sequence present, reference

`t13_seq_on.log`, via the new `SKIP_TO_13` flag (enrollment + Tests 12/13):

```
CMD 5 (TEST 12) P1 vs P2 Hamming: 4
CMD 4 (TEST 13) P1 vs P2 Hamming: 1
VDB feedback contribution:        +3 trits
```

Reproduces the published direction (papers report CMD 4 = 1, CMD 5 = 2,
contribution +1).

## Causal necessity under masking — THE CLAIM INVERTS

`t13_seq_off.log`, identical build plus `MASK_SEQUENCE_INPUT=1`:

| P1 vs P2 Hamming (sign, /16) | CMD 4 (no blend) | CMD 5 (VDB blend) | VDB contribution |
|---|---|---|---|
| sequence present | 1 | 4 | **+3 trits** |
| sequence masked | 2 | **0** | **−2 trits** |

With the sequence counter removed, CMD 5 collapses P1 and P2 to Hamming **0** —
identical LP representations — while plain CMD 4, with no VDB blend at all,
preserves 2 trits of separation. **The blend is not merely unnecessary here; it
is actively destroying separation that survives without it.**

Stratum 1's third Demonstrated claim — "the episodic memory is causally
necessary; ablation collapses what VDB feedback separates" — does not survive
the ablation of the sequence counter. It reverses.

**Scope, stated plainly:** n=1 per condition, and these are small integer
Hamming values. The published claim was itself only 2-of-3 runs, so single runs
are known to be noisy at this resolution. This is not sufficient on its own to
overturn the claim. It is, however, consistent in direction with the n=3 TEST 15
result (sign divergence 3.6 → 0.2, t = 10.1), and the two together make
replication mandatory before any causal-necessity claim is published.

## Null-shuffle run — how to read it

`NULL_SHUFFLE_BINS=1` randomises the bin each divergence sample lands in.
Verified by inspection *before* running:

- **Control arm is valid.** With `enable_hebbian = 0`, `gt` is used *only* for
  accumulation (`sum_a/c`, `mtfp_sum_a/c`, `count_a/c`). Shuffling draws all
  four means from one distribution — a clean finite-sample null.
- **Hebbian arm is NOT valid in this run and must not be quoted.**
  `trix_agrees = (pred == gt)` sits inside the `if (enable_hebbian)` block and
  gates the weight updates; shuffling `gt` randomises that gate, so that arm
  reflects a corrupted learning signal, not a null.
- Bin occupancy is comparable to the real condition: the shuffle is uniform
  over 4 bins and the sender gives all four patterns equal airtime.

**Report the Control arm only.**

### THE FLOOR — measured

`null_shuffle.log`, Control arm, n=3:

| Condition | MTFP /80 | sign /16 |
|---|---|---|
| sequence (published config) | 11.3 ± 3.4 | 3.6 ± 0.4 |
| random (variation, no info) | 8.4 ± 2.3 | 1.3 ± 1.2 |
| **NULL (shuffled bins)** | **4.3 ± 1.9** | **0.5 ± 0.0** |
| masked (no variation, no info) | 2.1 ± 2.1 | 0.2 ± 0.4 |

**`README.md` asserts the null is ~1. It is 4.3 — off by more than 4×.**

Does the published configuration clear its own noise floor?

| Metric | Null | Published | Clears null? |
|---|---|---|---|
| sign /16 | 0.5 ± 0.0 | 3.6 ± 0.4 | **YES** — t(2) = 13.1 vs crit 4.30 |
| MTFP /80 | 4.3 ± 1.9 | 11.3 ± 3.4 | **NO at n=3** — t = 3.15 vs crit 3.18 |

Neither `random` (8.4, t = 2.36) nor `masked` (2.1, t = −1.39) is separable from
the null in MTFP. `masked` sits numerically *below* the floor.

### The trap this closes

The two metrics fail in complementary ways, and there is no configuration in
which the project currently holds a clean result:

- **Sign-space clears its null decisively — but it is the metric that depends on
  the sequence counter.** Masking the counter takes it from 3.6 to 0.2, and
  pattern-agnostic noise recovers only 32% of it. The signal is largely the leak.
- **MTFP is robust to the counter — but it does not clear its own null at n=3.**
  It is 68% reproducible from noise, and the margin over the floor is t = 3.15
  against a critical value of 3.18.

The April decision to switch the papers from sign-space to MTFP ("when
sign-space and MTFP disagree, trust MTFP") was correct about sign-space being
misleading, but it moved the headline onto the metric with the *worse*
signal-to-null ratio.

**t = 3.15 vs 3.18 is a hair, and must not be read as "the result is noise."**
It means the published number is not separable from its own sampling floor *at
the sample size the papers use*. n=5 paired may well resolve it — which is
precisely the power argument already written into
`docs/BLEND_GATED_DEFERENCE.md` §7. Resolving it is now the single highest-value
run available.

**Caveat on the null itself:** the LP state evolves under real cycling input, so
samples are not i.i.d.; temporal correlation could bias the floor in either
direction. It is nonetheless a far better null than an asserted ~1, and it is
the first one this project has measured.

## Superseded framing — the two candidate mechanisms, now resolved

*Retained for the record; the random control above settles between these.*

The sender holds each pattern for **5 s** (`espnow_sender.c:288`) at ~7.5
packets/s ≈ 37 packets per block, ~150 per four-pattern cycle. Input trits
[112..119] carry sequence bits 0–7 (sequence mod 256). A 256-valued counter
against a ~150-packet cycle correlates strongly with pattern phase.

1. **Informational leak.** Sequence bits correlate with position in the pattern
   cycle, so the LP was partly reading a clock that happens to track the
   sender's schedule. Under this reading the published divergence is
   substantially an artifact.

2. **Dimensional scaffolding.** Sequence made every VDB snapshot unique. Without
   it, snapshots for different patterns are too similar for retrieval to
   disambiguate, and the known CfC projection degeneracy (TEST 13: CMD 4
   collapses P1=P2) is no longer compensated. Under this reading sequence is not
   leaking a label but supplying the variation that makes episodic retrieval
   work at all.

**Both readings require the same disclosure**, and neither is currently in the
papers: the reported VDB-only divergence depends critically on a monotonic
packet counter being present in the input.

**Decisive control (not yet run):** replace sequence with a per-packet *random*
value uncorrelated with pattern phase. Divergence holding up ⇒ reading 2;
collapsing ⇒ reading 1. This needs a small sender or encoder change.
`MASK_RSSI` is **not** a usable control — it masks signatures only
(`test_live_input.c:199`), not the input.

## Scope

n=3 reps per condition, one session, one board pair, 4 patterns, one sender.
The sign-space effect is large relative to its spread (t≈10); the MTFP effect is
noisier (t≈4.1) because this session's baseline SD was 3.4 against the published
0.6 — itself evidence that n=3 is underpowered and that the published ±0.6 was a
fortunate draw.

## Files

- `b5_seq_on.log` — sequence present (control)
- `b5_seq_off.log` — sequence masked (`MASK_SEQUENCE_INPUT=1`)
