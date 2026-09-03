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

## Mechanism — two candidates, not yet separated

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
