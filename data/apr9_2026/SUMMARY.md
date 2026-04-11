# Multi-Seed TEST 14C — April 9, 2026

**Raw logs:** `results_final_seed_{a,b,c}.log` in this directory (gitignored; local-only).
**Firmware:** commit `63877f7` (sender enrollment fix) + `00358ea` (working tree baseline).
**Hardware:** Board A = `/dev/esp32c6a` (BASE MAC `b4:3a:45:8a:c4:d4`), Board B = `/dev/esp32c6b`.

---

## Why this re-run exists

Two bugs were compounding, both invalidating every prior multi-seed TEST 14C dataset:

1. **`trix_enabled` not set** (fixed April 8, commit `f97ac1c`). The ISR never ran TriX classification. `trix_pred` stayed at `-1`, which matched no pattern accumulator, so the agreement-weighted gate bias computed `bias = 0` every step of the Full condition. The "Full" column in every pre-April-9 multi-seed table was actually measuring the no-bias condition.

2. **Enrollment starvation in transition mode** (fixed April 9, commit `63877f7`). The sender's TRANSITION_MODE started sending P1 immediately on boot, so Board A's Test 11 Phase 0a (30s signature observation) only ever saw pattern P1. `sig[0]`, `sig[2]`, and `sig[3]` were computed from zero samples and stayed zero. TriX's argmax over `sig[] ⋅ hidden` could never select P2 because P2 had no weights. The previous "Full crossover = 0" finding wasn't just mislabelled — it was structurally unreachable.

This dataset is the first run where both bugs are fixed and enrollment sees all four patterns before the P1→P2 transition begins.

---

## Results

### TEST 14C headline metrics

| Seed | LP_SEED | Full (CMD5+bias) | No bias (CMD5) | Ablation (CMD4) | Verdict |
|---|---|---|---|---|---|
| A | `0xCAFE1234` | **15/15** | **15/15** | **15/15** | 2/2 PASS |
| B | `0xDEAD5678` | **8/15** | **12/15** | **14/15** | 2/2 PASS (headwind) |
| C | `0xBEEF9ABC` | **15/15** | **15/15** | **15/15** | 2/2 PASS |

"TriX@15" = TriX prediction accuracy over the first 15 steps after the sender's P1→P2 switch. Measured against ground-truth `pattern_id` from the ESP-NOW payload.

**Seed mean: Full 12.7 / No bias 14.0 / Ablation 14.3 out of 15 (84.4% / 93.3% / 95.6%).**

### Alignment traces — `(align_P1 / align_P2)` at steps after switch

**Seed A (0xCAFE1234):**

| Cond | +0 | +5 | +10 | +15 | +20 | +30 |
|---|---|---|---|---|---|---|
| Full | +47/+53 | +47/+51 | +47/+52 | +44/+53 | +35/+43 | +47/+53 |
| No bias | +42/+52 | +46/+60 | +44/+52 | +44/+59 | +44/+60 | +47/+45 |
| Ablation | +40/+62 | +52/+54 | +45/+45 | +47/+54 | +45/+45 | +56/+55 |

**Seed B (0xDEAD5678):**

| Cond | +0 | +5 | +10 | +15 | +20 | +30 |
|---|---|---|---|---|---|---|
| Full | +26/+54 | +39/+59 | +30/+43 | +36/+55 | +32/+41 | +34/+49 |
| No bias | +51/+63 | +43/+48 | +39/+48 | +49/+45 | +40/+46 | +41/+53 |
| Ablation | +51/+59 | +29/+39 | +40/+43 | +23/+39 | +21/+34 | +28/+38 |

**Seed C (0xBEEF9ABC):**

| Cond | +0 | +5 | +10 | +15 | +20 | +30 |
|---|---|---|---|---|---|---|
| Full | +38/+64 | +39/+45 | +46/+48 | +39/+47 | +37/+51 | +43/+51 |
| No bias | +50/+58 | +38/+57 | +34/+43 | +41/+52 | +41/+59 | +45/+55 |
| Ablation | +36/+60 | +46/+45 | +44/+51 | +52/+50 | +40/+56 | +38/+38 |

### Ablation regression (P1 catching P2 post-switch)

The CLS prediction is that removing VDB blend (Ablation, CMD 4) should allow the stale P1 prior to reassert itself, so `align_P1 − align_P2` should grow less negative (or turn positive) over the 50-step P2 window.

| Seed / Cond | gap @ +0 | gap @ +10 | gap @ +20 | gap @ +30 | Trend |
|---|---|---|---|---|---|
| A / Full | −6 | −5 | −8 | −6 | stable separation |
| A / No bias | −10 | −8 | −16 | +2 | **converges** by +30 |
| A / Ablation | −22 | 0 | 0 | +1 | **regression** — P1 reaches parity by +10 and overtakes by +30 |
| B / Full | −28 | −13 | −9 | −15 | stable (despite TriX jitter) |
| B / No bias | −12 | −9 | −6 | −12 | mild reconverge |
| B / Ablation | −8 | −3 | −13 | −10 | mixed — Seed B projection degeneracy dominates |
| C / Full | −26 | −2 | −14 | −8 | stable |
| C / No bias | −8 | −9 | −18 | −10 | stable |
| C / Ablation | −24 | −7 | −16 | 0 | **regression** — parity by +30 |

Ablation regression is visible in Seeds A and C. In Seed B, the degenerate LP projection (below) dominates and masks the CLS signal; this is the known "Seed B headwind" the papers document.

### Prediction crossover (`pred` flip from 1→2 after switch)

For Seeds A and C, all three conditions flip `pred` at **step +1** — the very next step after the first P2 packet arrives. This is the fastest possible crossover given the 1-step measurement cadence. The "release within 4 steps" claim in prior paper drafts should be strengthened to "within 1 step" (for these seeds).

For Seed B, `pred` oscillates between 0 and 2 post-switch under the Full condition (the bias is pulling the LP state toward a point the degenerate projection can't represent cleanly). Seed B's 8/15 reflects this jitter.

### Bias release + new prior formation (Seed A, Full condition)

The clean Seed A trace shows geometric bias decay (×0.9/step) and simultaneous new-prior formation:

```
step +0:  bias=[13, 0]   pred=1  (bias blocks the switch for 1 step)
step +1:  bias=[11, 0]   pred=2  (switch confirmed, soft decay begins)
step +2:  bias=[10, 0]   pred=2
step +5:  bias=[ 7, 0]   pred=2
step +10: bias=[ 4, 0]   pred=2
step +20: bias=[ 1,12]   pred=2  (release ~done, new P2 prior forming)
step +30: bias=[ 0,12]   pred=2  (full release, new prior stable)
```

**Bias decays geometrically with factor 0.9 per step** (`bias_i[p] = bias_i[p] * 9 / 10` in `test_kinetic.c:907`). With BIAS_SCALE=10 internal resolution, the displayed value follows:

```
internal: 130 → 117 → 105 → 94 → 84 → 75 → 67 → 60 → 54 → 48 → 43
display:   13 →  11 →  10 →  9 →  8 →  7 →  6 →  6 →  5 →  4 →  4
```

This matches the logged data exactly. Half-life ≈ 6.6 steps. 90% decay by step ~22.

The new P2 prior begins forming at step +15 once `T14C_MIN_SAMPLES=15` P2 samples have accumulated for the agreement-check accumulator. By step +20, `bias_i[2]` is at 12 and holds stable.

**Important correction to prior paper language:** the "release within 4 steps" claim conflates the disagree-count threshold (≥4 trits) with a step count. The mechanism has two release paths:
1. **Soft (geometric ×0.9/step):** runs unconditionally every step. This is what released the prior in this dataset.
2. **Hard (disagree-count zero):** sets `bias_i[pred] = 0` in one step when `n_disagree ≥ 4`. **Not exercised on any clean seed in this run.**

The hard path can theoretically release in 1 step. The soft path takes ~22 steps for 90% decay. Both are real, only the soft one was active in this dataset.

---

## Seed B honest analysis

Seed B is the documented degenerate-projection case. The random LP weights for this seed produce a projection where P1 and P2 collapse to nearly-parallel hidden-state directions, so the LP `lp_hidden` can't cleanly separate the two patterns regardless of what the bias is doing.

Observed pathology:
- During P1 phase, `lp_hidden` forms a prior that technically corresponds to P1, but the projection is so shallow that the bias it computes (~9–13 on group 1) isn't well-aligned with the actual P1/P2 separation axis.
- Post-switch, `pred` oscillates: P2 packets sometimes land in the P0 score cell because the P0 signature is actually closer to the degraded LP signal than either P1 or P2 are.
- **Full** ends up worse than **No bias** and **Ablation** because the bias is pulling the gate threshold in a direction that amplifies the wrong neurons.

**This is a negative result worth keeping.** It demonstrates that the mechanism is not unconditionally beneficial — it depends on LP projection quality. Pillar 3 (LP Hebbian weight learning, corrected from the original "Hebbian GIE" proposal) is the designed fix — but the April 11 H2 experiment showed that VDB-mismatch-based Hebbian is label-dependent and harmful when the label leak is closed. The next iteration uses TriX classifier output as the training signal (structurally guaranteed, label-free). See `ROADMAP.md` and `DO_THIS_NEXT.md` for current status.

Reporting recommendation: the papers should cite Seed B as evidence that the mechanism is projection-dependent. The Hebbian fix is in progress but not yet validated label-free.

---

## Metric and verdict caveats

**The test code's `Crossover step: 0` is honest but saturated.** Tracing `test_kinetic.c:980-982`:

```c
/* Detect crossover */
if (crossover_step < 0 && align2 > align1)
    crossover_step = phase2_step;
```

The metric truly measures "first step where align_P2 > align_P1." It returns 0 for every condition in every seed because the LP MTFP state shifts the moment the first P2 packet arrives — `align_P2` exceeds `align_P1` immediately. The metric isn't sentinel-broken; it has **zero discriminative resolution** for this experimental setup. I called it a "sentinel" in earlier session notes; that was wrong.

**The `Verdict` lines pass trivially because the saturated metric has no resolution.** All gates compare against `crossover_step = 0`, so `0 ≤ 30` (PASS), `0 ≤ 0` (PASS), etc. The "Bias helps (full ≤ no-bias)" gate prints PASS for Seed B even though the TriX@15 evidence shows the bias is hurting (Full 8/15 < No-bias 12/15). The verdict isn't actively lying — it's checking the wrong thing.

### Suggested replacements

For the metric to discriminate, it needs to measure something with resolution > 1 step:
- **`pred` flip held for ≥3 steps:** discriminates clean seeds (1 step) from Seed B (jitter, much longer)
- **Sustained alignment gap ≥ M trits for ≥3 steps:** discriminates strength of separation
- **Bias release time:** number of steps for `gie_gate_bias[old_group]` to decay below threshold (e.g., 2). Directly measures soft decay.

For the verdict to be honest about Seed B, it should fail the Full gate and explicitly flag the headwind:
- `Full TriX@15 ≥ 10/15` would fail Seed B (8/15) — correct
- Drop `Bias helps (full ≤ no-bias)` — for the Seed B case the answer is genuinely no, and that's a real result, not a test failure
- Add `Ablation alignment regression visible by step 30` — directly tests the CLS prediction

**Until the verdict logic is fixed, TriX@15 + alignment traces + bias-release progression are the reliable signals from this dataset.** Do not cite "crossover step" from the current test code in papers.

---

## Reproduction notes

1. Flash sender: `idf.py -DREFLEX_TARGET=sender -DTRANSITION_MODE=1 build && idf.py -p /dev/esp32c6b flash`
2. Per seed, build + flash receiver: `idf.py fullclean && idf.py -DREFLEX_TARGET=gie -DLP_SEED=<seed> -DSKIP_TO_14C=1 build && idf.py -p /dev/esp32c6a flash`
3. **Reset Board B (sender) immediately after Board A flash completes** so the sender's 90s enrollment cycle overlaps Board A's Test 11 Phase 0a. If Board B has been running >30s, reset it via DTR/RTS toggle before capturing.
4. Capture Board A serial output for ~10 min; expect TEST 14C summary by ~7 min.

Seeds: `0xCAFE1234`, `0xDEAD5678`, `0xBEEF9ABC`. Each run is ~10 min end-to-end (build + flash + run). Total per sweep ~35 min.

---

---

## P2 payload caveat (added April 11, 2026)

All data in this directory was collected with the **old P2 payload** (`{0xAA, alt, alt, ...}`), which shared 48/64 payload trits with P1. A subsequent label-free accuracy experiment (R3a, commit `2fc5219`) revealed that without the pattern_id label trits, P2 classification dropped to 10% accuracy due to this payload overlap.

The P2 payload was redesigned in commit `c7ef286` to `{0x55, 0x33, 0xCC, 0x66, 0x99, 0x0F, 0xF0, 0x3C}`, achieving 100% label-free accuracy (32/32, 14/14 PASS). **The multi-seed TEST 14C data in this directory should be re-collected with the new P2 payload** before citing it in papers, because the Seed B headwind finding may be affected by the payload change (different cross-dot ratios change the TriX classification dynamics).

The TriX@15 numbers reported here (A=15/15, B=8/15, C=15/15) were computed with the old P2 payload and pattern_id labels present in the signatures. With the new P2 payload and label masking, these numbers are expected to change. A re-run is listed in `DO_THIS_NEXT.md`.

---

*Dataset produced April 9, 2026. Supersedes all prior `data/apr8_2026/results_*.log` files for TEST 14C claims. The apr8 data should be considered invalid for all three conditions, not just Full, because enrollment starvation affected every condition's TriX classification.*
