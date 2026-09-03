# The Reflex: Repository Audit — September 2026

**Date:** September 2, 2026
**Auditor:** Claude (Opus 5)
**Branch:** `trix-integration` @ `075bf83` (clean)
**Scope:** Load-bearing firmware, claim-to-evidence traceability across README / `docs/` / `papers/`, raw silicon logs in `data/`, build config, repo hygiene.
**Method:** Every load-bearing source file read directly. Every headline number recomputed from the raw logs. No subagents.

**Remediation:** Completed September 2, 2026, same session. Status table below.
**Red-team pass:** Same day, against the remediation itself. Found four further
defects — one of them introduced by the remediation. See below.

---

## Red-Team Pass — findings against the remediation

The remediation was adversarially reviewed before commit. Four defects surfaced,
all now fixed.

### R1. The remediation introduced an unsupported number (664 Hz)

Finding A4 said 430 Hz was attached to the wrong configuration, and the fix
replaced it with "664 Hz in classification mode" across three papers, the
README, ROADMAP and CURRENT_STATUS. **That number is not a measurement.**

```c
/* test_live_input.c */
start_freerun();                      /* line 84  — resets trix_count and loop_count */
...30 s Phase 0a observation, Phase 0b, Phase 0c...
trix_enabled = 1;                     /* line 328 — trix_count starts accumulating */
int64_t test_start = ...;             /* line 608 — denominator starts HERE */
...
printf("... (at %d Hz)", trix_count * 1000000LL / (esp_timer_get_time() - test_start));
```

The numerator accumulates from line 328; the denominator starts at line 608.
They span different intervals, so the printed figure **overstates the rate by an
unknown factor**. It also counts only clean loops (`trix_count` increments once
per clean classification, not once per loop) — `28685` classifications against
`41599` logged loops.

Nor can the loop rate be recovered from `loop_count`, which is reset at line 84
and therefore spans the 30 s observation window as well.

**The only clean rate measurement in the corpus is 430 Hz** (`test_espnow.c:145`:
`start_freerun()` zeroes the counter, `vTaskDelay(1000 ms)`, read counter → 430).

*Fixed:* 664 Hz withdrawn everywhere. Papers now state 430 Hz with its
configuration and add a Limitation that the classification-mode rate is higher
but unmeasured. `test_live_input.c` now snapshots both counters at `test_start`
and reports loop rate, TriX rate, and clean-loop percentage over one window, so
the next run produces a citable number.

**Lesson:** the original finding (a scoping error) was real, but the fix replaced
it with a factual error. Correcting an overclaim is not automatically safe.

### R2. The firmware mislabels which classifier Test 14 measures

`test_kinetic.c` scores `pred`, which is `trix_pred` — the TriX ISR prediction.
The printout said `Classification Accuracy (CPU core_pred vs sender ground
truth)`. Every published per-packet figure derived from it was attributed to the
wrong classifier.

This works in the papers' favour: it makes the per-packet numbers the *same*
classifier as the 32/32 windowed figure, so the comparison in A3 is
apples-to-apples. But the label was wrong for months.

*Fixed:* printout and confusion-matrix header now say TriX ISR. Papers attribute
the figures to the TriX ISR explicitly.

### R3. Windowed accuracy is not uniformly 100%

The remediation wrote "32/32 in every configuration we tested". Across the 15
label-free runs captured after the P2-payload fix (`c7ef286`):

- **32/32** in ten runs
- **31/32** in five (`hebbian_3reps`, `multiseed_seed_b`, `multiseed_seed_c`, `t14c_labelfree_seed_a`, `t14_labelfree_run1`)

The honest figure is **31–32/32, i.e. 96–100%**. The audit caught that "100%"
was a windowed vote; the red-team caught that even the windowed vote is not
uniformly 100%.

*Fixed:* all documents now state 96–100% (31–32/32 across 15 runs).

### R4. The per-packet denominator is novelty-admitted packets

`test_kinetic.c` runs `if (core_best < NOVELTY_THRESHOLD) continue;` **before**
`total_confirms++`. The 90.5–96.4% figure is therefore accuracy over packets
admitted by the novelty gate (CPU signature score ≥ 60), not over all received
packets.

*Fixed:* described as "per admitted packet" throughout, with the gate stated.

---

## Remediation Status

| # | Finding | Status | Where |
|---|---|---|---|
| A1 | "Consistently harmful" contradicted by own table | **Fixed** | 3 papers, README, ROADMAP, `PAPER_KINETIC_ATTENTION.md`, `apr11/SUMMARY.md` |
| A2 | Asymmetric statistical treatment | **Fixed** | Dispersion + t/p reported everywhere; both nulls held to one standard |
| A3 | "100% label-free" is a 32-window vote | **Fixed** (revised by R3/R4) | Windowed (31–32/32, 96–100%) vs per-admitted-packet (90.5–96.4%, TriX ISR); per-class recall table; "perfect" removed |
| A4 | 430 Hz attached to wrong configuration | **Fixed** (revised by R1) | 430 Hz stated with its configuration; classification-mode rate declared unmeasured, not replaced with a bad number |
| B1 | Gate-bias index-space mismatch | **Fixed** | `gie_engine.c` ISR now maps group→pattern; counter renamed `gie_gate_fires_per_pattern` |
| B2 | Calibration never re-run after restart | **Mitigated** | Mapping echoed at each condition start; drift now visible in logs. Full recalibration not added (costs a packet + settling time per condition) |
| B3 | Group 3 never fires the gate | **Fixed** | Reported in all 3 papers, README, ROADMAP, `SUMMARY.md` as a scope limit |
| B4 | Agreement metric didn't measure agreement | **Fixed** | `test_kinetic.c` now compares `trix_pred` to `core_pred` directly |
| B5 | Load-bearing encoder still carries sequence | **RUN — CONFIRMED** | Ablation on silicon Sep 2: VDB-only divergence collapses 82% MTFP / 93% sign with classification unchanged. `data/sep02_2026/SUMMARY.md`. **Escalates to a paper-blocking finding.** |
| B6 | `gie_gate_bias_pn` dead public API | **Labelled** | Not deleted: deliberate scaffolding per `journal/projection_aware_bias_reflect.md` |
| B7 | `cfc_homeostatic_step` dead code | **Labelled** | Not deleted: planned reuse per `journal/pillar3_concerns_synth.md` |
| C1–C6 | Verified strengths | **No action** | C2 and C3 strengthened in Stratum 1/3 text |
| D1 | All raw logs gitignored | **Fixed (staged)** | `.gitignore` narrowed to build logs; 58 logs staged; `data/README.md` added. **Needs a commit** |
| D2 | README says PARLIO 10 MHz | **Fixed** | Now 20 MHz |
| D3 | Docs 4.7 months stale | **Fixed** | `CURRENT_STATUS.md` + `ROADMAP.md` headers updated; TriX branch + UART blocker called out |
| D4 | "12× richer" not defensible | **Fixed** | Normalized to ~2.8×; defensible framing given |
| D5 | MTFP 5th trit degenerate | **Fixed** | Quantified in Stratum 1 Limitations, referenced from Stratum 2 |
| D6 | `SUMMARY.md` reset protocol wrong | **Fixed** | Corrected, with its (nil) practical consequence stated |
| D7 | Condition order not counterbalanced | **Fixed** | Added to Stratum 1 and 2 Limitations, `SUMMARY.md` |
| D8 | Minor hygiene | **Partial** | Root-level logs documented rather than moved — see below |
| R1 | Remediation introduced unsupported 664 Hz | **Fixed** | Withdrawn everywhere; rate diagnostic corrected in `test_live_input.c` |
| R2 | Test 14 accuracy mislabelled as CPU core_pred | **Fixed** | `test_kinetic.c` printout + confusion header; papers attribute to TriX ISR |
| R3 | Windowed accuracy not uniformly 100% | **Fixed** | 31–32/32 (96–100%) across 15 runs, everywhere |
| R4 | Per-packet denominator is novelty-admitted only | **Fixed** | "per admitted packet" throughout, gate stated |
| R5 | Secondary live docs never remediated | **Fixed** | `THE_PRIOR_AS_VOICE.md`, `KINETIC_ATTENTION.md`, `MEMORY_MODULATED_ATTENTION.md`, `PERIPHERALS-ONLY-COMPUTE.md`, `LCACHE_TEST14C_SIM.md` + a missed line in `PAPER_CLS_ARCHITECTURE.md` |
| R6 | Withdrawn ISR rate figures (705/711 Hz) still cited | **Fixed** | Same root cause as R1; withdrawn in live docs, correction headers cover in-body labels |
| R7 | Classification-mode rate now measured | **Fixed** | 490 Hz (97% of the 503 Hz PARLIO ceiling). The withdrawn 664 Hz exceeded the hardware ceiling by 32% — physically impossible |
| R9 | Every ± in the papers is a within-session SD; between-session spread is several times larger | **OPEN — paper-blocking** | Same config, two sessions: sign 3.6±0.4 vs 0.6±0.4 (t=9.9), MTFP 11.3±3.4 vs 6.2±2.0. Discrepancy ≈7× the within-session SD |
| R8 | Three measurements, three binning variables | **OPEN — paper-blocking** | TEST 12 bins by `core_pred`, TEST 13 by `trix_pred`, TEST 15 by `gt`. Claim 3's comparison subtracts across two different classifiers; the papers' "8.5–9.7/80" range spans two schemes |

**Verification performed:** ESP-IDF v5.4 firmware builds clean before and after
(exit 0, zero new warnings — the five project warnings present after the change
are pre-existing, in files outside the diff, surfaced by a full recompile).
`MASK_SEQUENCE_INPUT=1` builds clean in a separate tree and the define is
confirmed absent from the default build. All three papers compile with zero
undefined references (12/8/7 pages); PDFs regenerated.

### R5. The remediation stopped at the papers and the front door

A second sweep found six **live** documents still carrying the corrected claims:
`THE_PRIOR_AS_VOICE.md`, `KINETIC_ATTENTION.md`, `MEMORY_MODULATED_ATTENTION.md`,
`PERIPHERALS-ONLY-COMPUTE.md`, `LCACHE_TEST14C_SIM.md`, and one line inside
`PAPER_CLS_ARCHITECTURE.md` that the first pass missed.

`THE_PRIOR_AS_VOICE.md` was the worst of these: it is the document that makes the
strongest claim about the least-verified component, it still read "correct 100% of
the time across all hardware runs," and its §4 still described the bias
attenuating "to zero in one confirmation" — the code path that is never entered.
All six now carry correction headers; §4 and §5 of the perspective paper are
rewritten in place.

Dated session records (`SESSION_*.md`, `AUDIT_APRIL_2026.md`, `REDTEAM_MAR22.md`,
`REFLECTION_MAR22.md`, `REMEDIATION_PLAN_*.md`) were **deliberately left alone**.
They record what was believed at the time; editing them would falsify the history
the project depends on.

### A3 was a regression, not a discovery

`SESSION_APR06_07_2026.md` line 137, written April 6:

> The TEST 11 "100% accuracy" was an ensemble result (TriX Cube voting over
> multiple packets). The raw per-packet argmax accuracy is the same for both —
> ~80% pre-MTFP21, ~96% post-MTFP21.

The project had already found and documented this. The April 12 paper rewrites
then published "100% label-free accuracy" anyway. The finding did not need to be
discovered; it needed to survive the trip from the session record into the paper,
and it did not. Worth noting as a process failure distinct from a measurement one.

### R9. The reported error bars are the wrong error bars

The n=5 resolution run used the **same nominal configuration** as the earlier
n=3 run — same flags, same boards, same bench. Between them, the boards were
disconnected and reconnected, which power-cycled the sender.

| Metric | Session A (n=3) | Session B (n=5) | Welch |
|---|---|---|---|
| sign /16 | 3.57 ± 0.40 | **0.58 ± 0.43** | t = 9.86, df≈4.6 |
| MTFP /80 | 11.33 ± 3.35 | **6.16 ± 1.96** | t = 2.44, df≈2.8 |

The sign discrepancy is **≈7× the within-session SD**. These are not the same
population.

**Consequence: within-session SD is not a valid uncertainty estimate for any of
these measurements.** Every "±" in the papers — 9.7 ± 0.6, +0.1 ± 1.1,
−5.5 ± 5.3 — is computed from reps *inside one boot*, and the between-session
term is several times larger than the within-session term it reports. The error
bars are not merely underpowered; they are measuring the wrong source of
variance.

This also resolves a puzzle from earlier in the audit: the published baseline is
9.7 ± 0.6, session A gave 11.3 ± 3.4, session B gave 6.2 ± 2.0. Three sessions,
three means, with quoted SDs that do not span the differences between them.

**Candidate causes, not separated:**

1. **Sender power-cycle re-randomises the leak's phase.** The sender restarts its
   sequence counter at zero and restarts the pattern cycle, so the alignment
   between counter and 5 s pattern schedule is re-drawn at every sender boot. If
   the leak's strength depends on that alignment — as the B5 result implies it
   should — this predicts exactly the observed instability. Most likely cause.
2. RF environment / time of day.
3. Thermal or board state.

**Fix:** every quoted figure needs ≥3 *independent sessions* with sender
power-cycles between them, and the reported ± must be the between-session SD.
Within-session reps estimate rep noise, not measurement uncertainty. This
applies retroactively to every number in all three papers.

### R8. The divergence measurements do not share a binning variable

Found by reading `test_memory.c` — which the original audit never opened,
having worked from its output.

| Test | Produces | Bins LP samples by |
|---|---|---|
| TEST 12 | 8.5/80 matrix (Stratum 1 Claim 2) | `core_pred` — CPU classifier |
| TEST 13 | CMD 4 comparator (Claim 3) | `trix_pred` — TriX ISR |
| TEST 15 | 9.7 ± 0.6 (the quoted baseline) | `gt` — sender ground truth |

Two consequences:

1. **Claim 3's comparison is not apples-to-apples.** "CMD 5 (TEST 12) P1-P2 = 4"
   minus "CMD 4 (TEST 13) P1-P2 = 1" subtracts an ISR-binned measurement from a
   CPU-binned one. The two classifiers agree 94–96%, so 4–6% of samples land in
   different bins between the halves of the comparison — material at Hamming
   values of 1 and 4.
2. **The papers' "8.5–9.7/80" range spans two binning schemes.** 8.5 is TEST 12
   (`core_pred`); 9.7 is TEST 15 (`gt`). They are quoted as one quantity.

**Does this affect the Sep 2 results?** No, for the primary contrast. The
null-shuffle floor (4.3) and every n=3/n=5 sequence-vs-null comparison are all
measured inside TEST 15's `gt` framework and are internally consistent. But
TEST 12's 8.5 has **no matching null**, and TEST 13's comparison is cross-scheme.

**Fix:** bin all three on one variable. `gt` is the defensible choice for a
divergence metric — binning by a classifier's own output makes the grouping a
function of the input, and any input-derived structure then appears as
"divergence". At minimum, TEST 12 and TEST 13 must agree with each other before
their difference is called a VDB contribution.

### Three items deliberately left open

1. **B5 needs silicon.** The `MASK_SEQUENCE_INPUT` flag defaults OFF so the
   firmware continues to match the committed apr9/apr11 datasets. Turning it on
   changes measured behaviour, so the ablation must be run and the MTFP
   baseline re-confirmed before any paper cites results under it.

2. **D1 needs a commit.** The 58 log files are staged, not committed —
   committing was not requested.

3. **D8, root-level logs.** `data/full_suite_remediation{,_v2,_v3}.log` are
   April 8, 2026 captures sitting outside the dated-directory convention. They
   were *not* moved into `data/apr8_2026/`, because that directory's
   `DEPRECATED.md` scopes itself to `results_*.log` and its second bug
   (enrollment starvation) is `TRANSITION_MODE`-specific, while these are
   full-suite runs. Whether they predate the `f97ac1c` fix cannot be settled
   from the logs alone. Documented in `data/README.md` and marked
   not-to-be-cited pending bench notes. Relocating research data on an
   auditor's inference is exactly the failure this audit exists to prevent.

---

## Executive Summary

The engineering is sound. The three findings that matter are not about the code — they are about the
distance between what the logs show and what the papers say.

The mechanism path holds up under direct inspection: no floating point anywhere in `gie_engine.c`,
`reflex_vdb.c`, the headers, or `ulp/main.S`; the structural wall (`W_f[hidden] = 0`) is intact on every
write path; and the ISR's decoded dot products match the CPU reference element-for-element in every
calibration record on disk. `reflex-deploy` passes 97/97. The arithmetic behind `9.7 ± 0.6`, `13.6 / 8.1 /
−5.5`, and the per-run MTFP matrices all reproduces from the raw logs.

Four claims headed for arXiv do not survive contact with the project's own data:

1. **Kinetic attention is not "consistently harmful."** Run 1 of 3 is `+0.4` — helpful. The direction is 1 positive, 2 negative.
2. **The kinetic result is not statistically distinguishable from zero at n=3** (`−5.47 ± 5.27`, t(2) = −1.80, p ≈ 0.21) — by exactly the standard the papers correctly apply to Hebbian.
3. **"100% label-free classification" is a 32-window majority vote.** The same firmware measures 90.5–96.4% per packet, with P3 recall as low as 63%.
4. **430 Hz is the blend-active loop rate, not the classification rate.** Classification runs at a measured 645–676 Hz.

Plus one reproducibility gap that outranks all of them for publication: **every raw experiment log is
gitignored.** The papers cite files that exist only on this machine.

| Severity | Count |
|---|---|
| Claim integrity (paper-blocking) | 4 |
| Mechanism / correctness | 7 |
| Reproducibility & hygiene | 8 |
| Verified strengths | 6 |

---

## A. Claim Integrity — Paper-Blocking

### A1. "Consistently harmful" is contradicted by the project's own table

`papers/stratum1_vdb_temporal_context.tex:40` — "agreement-weighted gate bias (kinetic attention) is
**consistently harmful** at MTFP resolution (mean −5.5/80, 3 runs)."

`data/apr11_2026/SUMMARY.md` — "The direction of the MTFP effect is **consistent across all 3
independent runs**."

The table directly above that sentence, and the identical table in `stratum1.tex:207-217`:

| Run | 14A (no bias) | 14C (full bias) | Improvement |
|---|---|---|---|
| 1 | 9.8 | 10.2 | **+0.4** |
| 2 | 15.5 | 8.5 | −7.0 |
| 3 | 15.5 | 5.7 | −9.8 |

Run 1 is positive. I recomputed all three from the raw MTFP divergence matrices in
`data/apr11_2026/t14_labelfree_run{1,2,3}.log` — the table is arithmetically correct, so the defect is
purely in the word "consistently." One of three runs shows the mechanism helping.

Also worth noting: run 1's 14A baseline is 9.8 while runs 2 and 3 are both exactly 15.5. The baseline
itself is unstable across runs, which is the more interesting story.

**Fix:** Replace "consistently harmful" with "harmful in 2 of 3 runs" everywhere it appears
(`stratum1.tex:40`, `stratum1.tex:75`, `docs/PAPER_KINETIC_ATTENTION.md:6,20,56`,
`data/apr11_2026/SUMMARY.md`), and delete the "direction is consistent" sentence.

### A2. Asymmetric statistical treatment between the two negative results

The Hebbian result is reported with dispersion and correctly called indistinguishable from zero:
`+0.1 ± 1.1/80 MTFP at n=3`.

The kinetic result is reported as a bare mean, `−5.5/80`, and described as consistent and reliable.
Applying the same standard to the same n:

```
per-run improvement: [+0.4, −7.0, −9.8]
mean = −5.47   sd = 5.27   sem = 3.04
t(2) = −1.80   two-tailed p ≈ 0.21
```

At n=3 the kinetic effect is **not** distinguishable from zero either. The papers currently report one
null result honestly and one null result as a finding.

(For the record, the Hebbian `±1.1` is propagated from the two independent condition SDs — `√(0.61² +
0.96²) = 1.14`. The paired SD across reps is `0.55`. Either way the conclusion holds; the paired
statistic is the more appropriate one, since reps are matched.)

**Fix:** Report `−5.5 ± 5.3 (n=3)` and state plainly that neither downstream mechanism separates from
zero at this sample size. This *strengthens* the paper's actual thesis — VDB alone is the finding, and
neither add-on beats it.

### A3. "100% label-free classification" is a 32-window majority vote

`data/apr11_2026/full_suite_label_free_final.log:509-514`, Test 11:

```
Test samples: 32
Core (CPU per-pkt vote):   32/32 = 100%
ISR  (HW 430 Hz TriX):     32/32 = 100% (all samples)
Baseline (packet-rate):    28/32 = 87%
```

Each "sample" is a majority vote over a window of ~8–9 packets (276 packets → 32 windows). The trivial
packet-rate baseline already scores 87% on this test, so its discriminating power is low.

In the **same firmware build, same boot**, Test 14 measures per-packet accuracy over ~550 confirms per
condition (`full_suite_label_free_final.log:782-785`):

```
14A (no bias)               526/556 = 94.6%
14C (full bias)             503/522 = 96.4%
14C-iso (bias after 60s)    524/579 = 90.5%
```

And the confusion matrices show heavy class imbalance and a weak P3:

| Condition | P0 recall | P1 | P2 | P3 | Macro avg |
|---|---|---|---|---|---|
| 14A | 299/300 (99.7%) | 138/144 (95.8%) | 60/66 (90.9%) | **29/46 (63.0%)** | **87.4%** |
| 14C-iso | 303/306 (99.0%) | 117/123 (95.1%) | 60/65 (92.3%) | **44/85 (51.8%)** | **84.6%** |

The papers are more careful than the README here — `stratum1.tex:115` does disclose "32/32" and
`stratum3.tex:107` says "(32/32)". But these two statements are contradicted by the logs above:

- `README.md:57` — "The classifier is already perfect (100% across all hardware runs)"
- `stratum1.tex:263` — "TriX accuracy remains 100% label-free in every configuration"

**Fix:** Qualify the headline as "100% (32/32) on windowed classification; 90.5–96.4% per packet, macro
average 84.6–87.4%." Delete "perfect" and "every configuration." Report P3 recall explicitly — it is the
honest weak spot and it connects to B3 below.

### A4. 430 Hz is attached to the wrong configuration

The 430 Hz figure has clean provenance: `test_espnow.c:145` runs the engine for exactly
`vTaskDelay(pdMS_TO_TICKS(1000))` and reads `loop_count` → `Static input: 430 loops`. That is the
**CfC-blend-active** loop rate (Tests 1–10).

Classification runs in Phase 3, where `test_live_input.c:317` sets `gate_threshold = 0x7FFFFFFF` to
disable the blend. The ISR then skips the ~20 µs re-premultiply/re-encode block
(`gie_engine.c:480`), and the loop runs faster. The only measured classification rate in the logs:

```
TriX ISR classifications: 28685 (at 664 Hz)   # 645–676 Hz across all runs
```

Every "430 Hz" / "441 Hz" string in the firmware is a hardcoded `printf` literal
(`geometry_cfc_freerun.c:63`, `gie_engine.c:9,139,425`, `test_live_input.c:327,932,1019`), not a
measurement.

So `stratum2.tex:36` — "classifies 4 wireless signal patterns at 100% label-free accuracy in peripheral
hardware at 430 Hz" — pairs the blend-active rate with the classification claim. This *understates* the
system by ~1.5×, but a reviewer who opens the logs will find 664 Hz and 441 Hz and 430 Hz in the same
file.

**Fix:** State both rates and what each configuration is: "430 Hz with the CfC blend active; 664 Hz in
classification mode (blend disabled)."

---

## B. Mechanism & Correctness

### B1. Gate-bias index-space mismatch (latent, currently benign)

The ISR reads the bias in **dots-space group** index:

```c
/* gie_engine.c:455-456 */
int group = n / TRIX_NEURONS_PP;
int32_t eff = thresh - (int32_t)gie_gate_bias[group];
```

`test_kinetic.c:311` writes it in **pattern-space**, indexed off `trix_pred`, which has already been
mapped through the permutation:

```c
/* gie_engine.c:405 */
trix_pred = trix_group_to_pattern[best_g];
```

The whole reason `trix_group_to_pattern[]` exists is that "the GDMA circular chain offset means ISR group
index g may not correspond to pattern g" (`test_live_input.c:354`). Half the consumers apply the map
(`trix_pred`); the other half do not (`gie_gate_bias[]`, `gie_gate_fires_per_group[]`).

**Currently benign:** every calibration record on disk resolves to identity —
`G0→P0 G1→P1 G2→P2 G3→P3`, across all 26 logs that contain the line. Better: the printed ISR group scores
match the CPU pattern dots *element-for-element* in every run, e.g.

```
ISR groups:  [-2, -4, 18, 22]
CPU patterns: [-2, -4, 18, 22]
```

So the offset is genuinely zero and no published result is affected. But the mechanism that exists to
handle a non-zero offset would silently mis-target the bias the first time it fired.

**Fix:** Either index the bias through the map (`gie_gate_bias[trix_group_to_pattern[group]]`, same for
the fire counters), or assert identity at calibration and delete the map. Do not leave it half-applied.

### B2. Calibration is never re-run after an engine restart

`trix_group_to_pattern[]` is calibrated once, in Test 11 (`test_live_input.c:349-430`), from a **single
packet** and a **single clean ISR loop**, with a greedy nearest-dot assignment over 4 values. Test 14 then
calls `stop_freerun()` / `build_circular_chain()` / `start_freerun()` three times — once per condition —
without recalibrating.

The unpacked ISR scores are also clamped to `int8_t` at the ISR (`gie_engine.c:415-417`) while the CPU
dots are not, so a dot beyond ±127 would saturate on one side of the comparison only.

**Fix:** Recalibrate after each `start_freerun()`, or assert that the mapping still holds.

### B3. Neuron group 3 never fires the gate — in any condition, in any run

```
Per-Group Gate Fires (total across 120s):
Condition                  | G0       G1       G2       G3
14A (no bias)              | 82608    27825    36320    0
14C (full bias)            | 101432   74313    83848    0
14C-iso (bias after 60s)   | 97680    68393    66224    0
```

Identical structure in runs 2 and 3. Kinetic attention operates on 3 of 4 neuron groups; group 3 is
structurally inert at `gate_threshold = 90`.

`stratum1.tex:223` states "The mechanism fires (per-group fire rate shift > 10% every run)." That is true
for G0–G2 and false for G3. And P3 — the group that never fires — is both the pattern the paper calls
"the most distinctive" and the pattern with the worst classification recall (A3).

**Fix:** Report the G3 = 0 result. It is a real, interesting constraint on the mechanism, and it partly
explains the negative result: a bias that can only reach 3/4 of the network is not a clean test of the
hypothesis.

### B4. The TriX-vs-CPU "agreement" metric does not measure agreement

`test_kinetic.c:198-232` does not compare `trix_pred` to `core_pred`. It takes the **maximum ISR group
score**, then searches for the CPU pattern whose dot is numerically closest to that maximum, and calls
*that* the ISR's prediction:

```c
int isr_best_val = -9999;
for (int g = 0; g < 4; g++)
    if (isr_d[g] > isr_best_val) isr_best_val = isr_d[g];
/* Match ISR max to CPU pattern */
for (int pp = 0; pp < 4; pp++) {
    int dist = isr_best_val - cpu_d[pp];
    ...
}
```

When two patterns have similar dots — routinely, e.g. `[50, 54, 54, 19]` — this misattributes. The
reported 94.3–96.9% agreement figures are artifacts of the estimator in both directions, and cannot
support either the "100% ISR/CPU agreement" claim in `docs/CURRENT_STATUS.md` or a sub-100% one.

**Fix:** Compare `trix_pred` to `core_pred` directly. `trix_pred` is already mapped to pattern space.

### B5. Two divergent input encoders; the load-bearing one still carries sequence

`espnow_encode_input()` (`gie_engine.c:1318-1327`) zeroes trits [104..127] and documents why —
"Sequence counter is monotonic and not pattern-specific... Now silenced at the source... eliminating ~661K
unnecessary AND+popcount operations per second."

`espnow_encode_rx_entry()` (`gie_engine.c:1400-1414`) — used by **every** load-bearing test
(`test_live_input`, `test_kinetic`, `test_memory`, `test_hebbian`) — still writes sequence features into
[104..119]:

```c
uint32_t seq_lo = pkt->sequence & 0x0F;
... new_input[104 + i] = ...
... new_input[112 + i] = bit ? T_POS : T_NEG;
```

The legacy encoder that got the fix is called only from `test_espnow.c`. So the "silenced at the source"
claim and the operations saving apply to the path that isn't used.

Signatures mask trits ≥104 at enrollment (`test_live_input.c:191`), which blocks the direct
classification route. But the input still carries sequence into the GIE hidden state, and from there into
the VDB snapshots and the LP path — which is where the divergence measurements are taken. Given that this
project has already been bitten twice by exactly this class of back-channel (pattern_id in signatures;
sender enrollment starvation), it deserves an explicit ablation rather than an assumption.

**Fix:** Zero [104..127] in `espnow_encode_rx_entry()` too, and re-run one divergence measurement to
confirm the MTFP baseline is unchanged.

### B6. `gie_gate_bias_pn[32]` is declared, exported, and never used

Defined at `gie_engine.c:132`, exported at `gie_engine.h:94` as `/* per-neuron bias */`. Zero reads, zero
writes anywhere in the tree. It advertises a per-neuron bias mechanism in the public header that does not
exist — a trap for anyone who writes to it and expects an effect.

### B7. `cfc_homeostatic_step()` is dead code

~70 lines (`gie_engine.c:1491-1560`), never called from anywhere. Worth noting *because* it is dead: it is
the one function that writes to `cfc.W_f` at arbitrary indices, and the fact that it never runs is part of
why B-side finding C2 (structural wall) holds unconditionally.

Minor, inside it: the "pick pseudo-randomly among contributing weights" selection
(`if (best_i < 0 || (cfc_rand() % 3) == 0)`) is biased toward later indices, and it draws from the same
PRNG stream as `cfc_seed()`, which would perturb seed reproducibility if it were ever enabled.

---

## C. Verified Strengths

These were checked directly, not taken on trust.

**C1. The no-floating-point invariant holds.** `grep` for `float|double` across `gie_engine.c`,
`reflex_vdb.c`, `embedded/include/*.h`, and `ulp/main.S` returns **zero hits**. Floats appear only in test
files, for printing percentages. The invariant comment at the top of `gie_engine.c` is accurate.

**C2. The structural wall is intact.** Every write to the hidden portion of `cfc.W_f` sets `T_ZERO`
(`gie_engine.c:271`, `test_live_input.c:309`). The online re-sign path (`test_live_input.c:764-766`)
writes only `j < CFC_INPUT_DIM`. `lp_hebbian_step()` touches `lp_W_f` / `lp_W_g` only, never `cfc.W_f`.
The one function that could break it never runs (B7), and even if it did, it only selects weights where
`cfc.W_f[n][i] != T_ZERO`. The wall holds on every path.

**C3. ISR dot decoding is exact.** In every calibration record, the ISR's per-group scores equal the CPU's
brute-force pattern dots element-for-element, including negative values. This is strong evidence that the
GDMA → PARLIO → PCNT → ISR decode chain is correct, and it is under-claimed in the papers.

**C4. VDB recall matches the claim.** `Recall@1: 19/20 = 95%`, `Recall@4: 72/80 = 90%`, identical across
13 logs. Caveat: `cfc_seed(0xC0E4A42)` fixes the query set, so those 13 logs are one experiment replayed,
not 13 independent trials — and both figures sit exactly on the pass threshold (`r@1 >= 95%`,
`r@4 >= 90%`), where one additional miss flips the verdict.

**C5. `reflex-deploy` is green.** 97/97 tests pass in 113 s.

**C6. The reported arithmetic reproduces.** `9.7 ± 0.6` from reps `[10.2, 9.8, 9.0]`; `13.6 / 8.1 / −5.5`
from the three-run table; every per-run MTFP mean recomputed from the raw divergence matrices. No
transcription errors found.

---

## D. Reproducibility & Hygiene

### D1. Every raw experiment log is gitignored — this is the top publication risk

`.gitignore:32` — `*.log`. Result:

```
tracked in data/:   SUMMARY.md ×2, DEPRECATED.md ×1
present in data/:   41 .log files
```

The papers cite these files as evidence by name — `data/apr11_2026/t14c_labelfree_seed_a.log` in
`docs/CURRENT_STATUS.md`, and the whole `apr9`/`apr11` corpus underpins every number in all three strata.
None of it is in version control. It exists on one machine, with `backups/` also gitignored.

For an arXiv submission whose central virtue is honest negative results, the supporting evidence being
untracked is the single largest gap in the repository.

**Fix:** Force-add the `data/**/*.log` corpus (1.9 MB total — trivial), or publish it as a release
artifact / Zenodo DOI referenced from the papers. Narrow the ignore rule to build logs
(`embedded/build/**/*.log`, `papers/*.log`) rather than a global `*.log`.

### D2. README architecture diagram states the wrong PARLIO clock

`README.md:26` — `GDMA → PARLIO (2-bit, 10MHz) → GPIO loopback → PCNT`.

Code: `gie_engine.c:705` — `.output_clk_freq_hz = 20000000`. Logs: `[INIT] PARLIO TX (2-bit, 20MHz,
loopback)`. `docs/CURRENT_STATUS.md` itself records the March 19 "Silicon Interlock ... forced 20MHz
PARLIO." The diagram was never updated.

### D3. Documentation is ~4.7 months behind the branch

`docs/CURRENT_STATUS.md` and `README.md` were last touched 2026-04-12. Today is 2026-09-02. The
`trix-integration` branch (2026-05-16) is 3.5 months old, unmerged, and adds four build flags
(`TRIX_DUMP_TRAINING`, `USE_TRIX_WEIGHTS`, `TRIX_DISABLE_NOVELTY`, parameterized `PEER_MAC`) plus an
externally-trained signature override path that **no document describes**. A reader following
`CURRENT_STATUS.md` would not know TriX integration exists.

### D4. "12× richer" is not a defensible comparison

`docs/CURRENT_STATUS.md:29` — "MTFP baseline is 9.7 ± 0.6 /80 from VDB alone — **12× richer** than
sign-space suggested."

`docs/PAPER_CLS_ARCHITECTURE.md:72` says "4-5× richer." `docs/PAPER_KINETIC_ATTENTION.md:223` says "5×
more dimensions." The 12× compares raw Hamming *counts* across different dimensionalities (9.7 vs 0.7).
Normalized: 9.7/80 = 12.1% vs 0.7/16 = 4.4%, i.e. ~2.8×. The papers use the defensible framing; the status
doc does not.

### D5. MTFP's fifth trit is degenerate over most of the operating range

Replicating `encode_lp_dot_mtfp()` (`test_harness.h:92-121`) over the full LP dot range [−48, +48]:

| Scale | \|dot\| | trit 3 (mantissa hi) | trit 4 (mantissa lo) |
|---|---|---|---|
| 0 | 0 | {0} | {0} |
| 1 | 1–3 | {−1, 0, +1} | **{−1} — constant** |
| 2 | 4–8 | {−1, 0, +1} | {−1, 0} |
| 3 | 9–15 | {−1, 0, +1} | {−1, 0} |
| 4 | 16–24 | {−1, 0, +1} | {−1, 0} |
| 5 | 25–35 | {−1, 0, +1} | {−1, 0, +1} |
| 6 | 36–48 | {−1, 0, +1} | {−1, 0, +1} |

`sub_pos = pos % sub_range` makes trit 4 a sawtooth rather than a refinement, and it carries no
information at all for |dot| ≤ 3, and at most one bit for |dot| ≤ 24. With a 48-trit concat at ~40%
sparsity (~29 non-zero terms), typical |dot| sits in exactly that low range. Across the whole reachable
range the encoder emits 75 distinct codes out of 243 possible.

This does not invalidate any measurement — Hamming over a lower-capacity code is still a valid relative
metric, and `stratum1.tex:307` already caveats that MTFP is "a proxy for dot-product diversity, not a
calibrated distance." But the `/80` denominator overstates the resolution, which is worth one sentence
rather than a footnote.

### D6. `SUMMARY.md` misstates the Test 14 reset protocol

`data/apr11_2026/SUMMARY.md` — "VDB is cleared and **LP weights re-initialized** between conditions."

`test_kinetic.c:111-112` clears only the VDB and `lp_hidden`. `init_lp_core_weights()` is called once, in
`geometry_cfc_freerun.c:98`. No practical consequence — nothing modifies LP weights during Test 14 — but
the record is wrong.

### D7. Condition order is never counterbalanced

Test 14 always runs 14A → 14C → 14C-iso, in that order, in all three runs. Any drift over the 6-minute
sequence — thermal, RF environment, VDB/board state — is confounded with condition, and a fixed order
across all three replicates means it does not average out. The 60-second snapshot is a control for
*maturity within* a condition, not for *order between* conditions.

### D8. Minor hygiene

- `data/full_suite_remediation{,_v2,_v3}.log` sit undated at `data/` root, outside the dated-directory convention.
- `reflex-deploy/firmware/*.bin` is tracked while `.gitignore` lists `*.bin` — grandfathered, but inconsistent.
- 333 tracked markdown files vs 25 C files. `docs/archive/lmm/` alone holds 48 files of a superseded exploration format.
- `pulse-arithmetic-lab/` (839 MB) and `tools/` (605 MB) dominate the 1.8 GB working tree; both are gitignored or near-empty in the index, so this is local-only bloat.

---

## What I Would Do First

Ordered by publication risk, not by effort.

1. **Track the data** (D1). One `git add -f data/**/*.log`, 1.9 MB. Nothing else in this list matters if the evidence can't be checked.
2. **Fix "consistently"** (A1) and **report dispersion on the kinetic result** (A2). Two sentence-level edits in three files. They convert a claim a reviewer would attack into a claim that supports the paper's real thesis.
3. **Qualify the 100%** (A3) and **report P3 recall and G3 = 0** (B3). These belong together — they are the same weakness seen from two directions.
4. **Split the rate claim** (A4). One sentence.
5. **Zero the sequence trits in `espnow_encode_rx_entry()`** (B5) and re-run one divergence measurement. This is the only item that requires silicon time, and it closes the last plausible back-channel of the kind that has already invalidated two datasets.
6. **Resolve the half-applied group→pattern map** (B1, B2). Correctness landmine, currently dormant.
7. **Update `CURRENT_STATUS.md` and merge or close `trix-integration`** (D3).

The system is in better shape than the paper drafts are. The strongest result in the repository — VDB
episodic memory alone producing pattern-discriminative temporal states, with two well-designed
attempts to beat it that both fail to separate from zero — is a cleaner and more publishable story than
the one currently written, which overstates one null result and understates the classifier's honest
limits.
