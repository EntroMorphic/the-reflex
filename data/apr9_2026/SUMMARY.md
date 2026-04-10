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

The clean Seed A trace shows linear bias decay and simultaneous new-prior formation:

```
step +0:  bias=[13, 0]   pred=1  (bias blocks the switch for 1 step)
step +1:  bias=[11, 0]   pred=2  (switch confirmed, release begins)
step +2:  bias=[10, 0]   pred=2
step +5:  bias=[ 7, 0]   pred=2
step +10: bias=[ 4, 0]   pred=2
step +20: bias=[ 1,12]   pred=2  (release ~done, new P2 prior forming)
step +30: bias=[ 0,12]   pred=2  (full release, new prior stable)
```

Bias decays at ~1 unit/step (agreement-weighted disagree-count threshold is 4, so each step with 4+ disagreeing trits decrements the bias by 1). Full release takes ~12–15 steps, not 4. The new P2 prior ramps up to 12 by step +20 and holds. **The papers' "release within 4 steps" claim is inaccurate for this mechanism — the actual release is ~12–15 steps, and what flips at step 1 is the `pred` value, not the bias magnitude.**

---

## Seed B honest analysis

Seed B is the documented degenerate-projection case. The random LP weights for this seed produce a projection where P1 and P2 collapse to nearly-parallel hidden-state directions, so the LP `lp_hidden` can't cleanly separate the two patterns regardless of what the bias is doing.

Observed pathology:
- During P1 phase, `lp_hidden` forms a prior that technically corresponds to P1, but the projection is so shallow that the bias it computes (~9–13 on group 1) isn't well-aligned with the actual P1/P2 separation axis.
- Post-switch, `pred` oscillates: P2 packets sometimes land in the P0 score cell because the P0 signature is actually closer to the degraded LP signal than either P1 or P2 are.
- **Full** ends up worse than **No bias** and **Ablation** because the bias is pulling the gate threshold in a direction that amplifies the wrong neurons.

**This is a negative result worth keeping.** It demonstrates that the mechanism is not unconditionally beneficial — it depends on LP projection quality. Pillar 3 (Hebbian GIE weight updates, per `ROADMAP.md`) is the designed fix because learned weights would move the projection axis to something that actually separates P1 and P2.

Reporting recommendation: the papers should cite Seed B as evidence that the mechanism is projection-dependent and that projection quality is the bottleneck the Hebbian layer will address.

---

## Metric and verdict caveats

**The test code's `Crossover step: 0` is a sentinel.** It represents "alignment of P2 exceeded alignment of P1 at step 0 or earlier" but the 0 value is also what prints when nothing crossed. For every condition in every seed of this run, the value is 0. It is **not a useful metric** and should be replaced with either:
- First step where `pred` flips from 1 to 2 **and stays there for ≥N steps** (proposed N=3)
- First step where `align_P2 − align_P1 > 0` sustained for ≥N steps

**The `Verdict` lines in the test log pass trivially.** All four gates (`Full system crossover ≤ 30`, `Bias helps (full ≤ no-bias)`, `Ablation slower`, `Sufficient P2 data`) are computed against the sentinel crossover value or against coarse thresholds that any real run satisfies. The "Bias helps" gate passes for Seed B even though the TriX@15 evidence contradicts it. This is a known issue — the verdict logic needs tightening in `embedded/main/test_kinetic.c`.

**TriX@15 is the reliable headline metric for this experiment.** Use that, plus the alignment traces, plus the bias-release progression. Do not cite "crossover step" from this test code until the metric is fixed.

---

## Reproduction notes

1. Flash sender: `idf.py -DREFLEX_TARGET=sender -DTRANSITION_MODE=1 build && idf.py -p /dev/esp32c6b flash`
2. Per seed, build + flash receiver: `idf.py fullclean && idf.py -DREFLEX_TARGET=gie -DLP_SEED=<seed> -DSKIP_TO_14C=1 build && idf.py -p /dev/esp32c6a flash`
3. **Reset Board B (sender) immediately after Board A flash completes** so the sender's 90s enrollment cycle overlaps Board A's Test 11 Phase 0a. If Board B has been running >30s, reset it via DTR/RTS toggle before capturing.
4. Capture Board A serial output for ~10 min; expect TEST 14C summary by ~7 min.

Seeds: `0xCAFE1234`, `0xDEAD5678`, `0xBEEF9ABC`. Each run is ~10 min end-to-end (build + flash + run). Total per sweep ~35 min.

---

*Dataset produced April 9, 2026. Supersedes all prior `data/apr8_2026/results_*.log` files for TEST 14C claims. The apr8 data should be considered invalid for all three conditions, not just Full, because enrollment starvation affected every condition's TriX classification.*
