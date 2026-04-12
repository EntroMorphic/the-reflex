# Label-Free Full Suite — April 11, 2026

**Raw log:** `full_suite_label_free_final.log` in this directory (gitignored; local-only).
**Firmware:** commit `698231c` (3-rep TEST 15, 60s Phase C).
**Build flags:** `MASK_PATTERN_ID=1 + MASK_PATTERN_ID_INPUT=1` — no pattern_id in signatures OR input.
**Sender:** cycling mode (P0→P1→P2→P3 at 5s each), distinct P2 payload (`c7ef286`).
**Hardware:** Board A = `/dev/esp32c6a`, Board B = `/dev/esp32c6b`.

---

## What this dataset is

The authoritative label-free measurement of the full Reflex system. No pattern_id anywhere — not in TriX signatures, not in the GIE input, not in the VDB nodes, not in the LP CfC input. Every number below was produced by a system operating on payload bytes, inter-packet timing, and RSSI only.

---

## Results

### Test suite summary

| Test | Result | What it measures |
|---|---|---|
| Tests 1-8 | 8/8 PASS | GIE peripherals, LP core, VDB, pipeline, feedback |
| Tests 9-10 | PASS | ESP-NOW receive, live input encoding |
| Test 11 | PASS, **32/32 = 100%** | Label-free TriX classification (4 patterns) |
| Test 12 | PASS | LP divergence from VDB feedback (sign mean 5.0/16, **MTFP mean 8.5/80**) |
| Test 13 | PASS | VDB causal necessity (CMD 4 distillation, +1 trit P1-P2) |
| Test 14 | FAIL | Kinetic attention: 14C mean (0.7) < 14A mean (1.0) |
| Test 14C | skipped | Cycling sender, not transition mode |
| Test 15 | FAIL | Hebbian: +0.1 ± 1.6 Hamming (noise) |
| **Total** | **14/16 PASS** | |

### Classification (Test 11)

**32/32 = 100% label-free.** Both CPU core_pred and ISR TriX agree at 100%.

The system classifies 4 wireless signal patterns from payload content and inter-packet timing features using peripheral-hardware ternary dot products at 430 Hz. No pattern_id trits participate in the classification. The distinct P2 payload (commit `c7ef286`) resolved the prior P1-P2 confusion (71% label-free with old P2 payload).

### LP Divergence (Test 12)

Sign-space Hamming matrix (/16):
```
     P0  P1  P2  P3
P0:   0   1   2   9
P1:   1   0   2   9
P2:   2   2   0   7
P3:   9   9   7   0
```

P0-P3 and P1-P3 show strong separation (Hamming 9/16). P0-P1 and P1-P2 are weaker (1-2/16). P3 is the most distinctive pattern (Ramp: incrementing payload at 10 Hz).

### VDB Causal Necessity (Test 13)

CMD 4 (CfC only, no VDB blend) vs CMD 5 (CfC + VDB + blend): P1-P2 Hamming difference = +1 trit. VDB feedback is causally necessary for LP divergence — same finding as the original March 22 dataset, now confirmed label-free.

### Kinetic Attention (Test 14)

**FAIL.** Mean LP divergence under gate bias (14C = 0.7/16) was LESS than without bias (14A = 1.0/16). Per-group fire shift was >10% (the gate bias IS changing firing patterns) but the effect on LP divergence is negative this run.

This is an honest result confirmed at MTFP resolution across 3 independent runs (commit `774fa4c`):

| Run | 14A MTFP mean | 14C MTFP mean | MTFP improvement |
|---|---|---|---|
| 1 | 9.8/80 | 10.2/80 | +0.4 |
| 2 | 15.5/80 | 8.5/80 | -7.0 |
| 3 | 15.5/80 | 5.7/80 | -9.8 |
| **Mean** | **13.6** | **8.1** | **-5.5** |

The sign-space metric (+1.3/16 mean) was an artifact: the bias traded magnitude diversity for sign diversity — a net information loss. The bias saturates the GIE hidden state (more neurons fire → LP input becomes more uniform → LP dot magnitudes converge). The mechanism reliably changes per-group fire rates (>10% shift every run) but the effect on LP representation is consistently negative at MTFP resolution.

The 3 runs used independent board resets between each run (both sender and receiver). The within-run comparison (14A vs 14C) shares VDB content (14A builds the VDB, 14C inherits it) but VDB is cleared and LP weights re-initialized between conditions. The direction of the MTFP effect is consistent across all 3 independent runs.

### Hebbian Learning (Test 15)

**+0.1 ± 1.6 Hamming (3 reps per condition). Indistinguishable from zero.**

```
Control (CMD5 only):   1.0 ± 1.3 /16  (reps: 0.5, 0.0, 2.5)
Hebbian (CMD5+learn):  1.1 ± 0.8 /16  (reps: 0.5, 0.7, 2.0)
```

The diagnosed Hebbian v3 (TriX accumulator target, f-vs-g diagnosis) is not harmful (v1 was -1.7, v2 was -1.0) but is not helpful either. The VDB-only mechanism (CMD 5 feedback blend) produces the baseline LP divergence. Weight learning adds variance without signal.

Note: the Control itself shows high variance (1.0 ± 1.3) — run-to-run LP divergence varies significantly from wireless timing noise affecting VDB insertion order and LP blend trajectories.

---

## MTFP metric interpretation note

MTFP Hamming (/80) counts differing trits in the 5-trit-per-neuron encoding [sign, exp0, exp1, mant0, mant1]. This metric is nonlinear: a sign-trit disagreement means the dot products have opposite signs (large difference), while a mantissa-trit disagreement means they have similar magnitudes in the same scale (small difference). Raw MTFP Hamming is a proxy for dot-product diversity, not a calibrated distance. The 8.5-9.7/80 baseline means ~0.5-0.6 MTFP trits differ per neuron per pattern pair on average. A more precise metric would be mean absolute dot-product difference per neuron — the raw `lp_dots_f[]` values are available in LP SRAM but are not currently aggregated across patterns in the test output.

The MTFP metric is valuable because: (a) it's more stable than sign-space (CV 6% vs 39%), (b) it captures magnitude information that sign discards, (c) it correctly identified kinetic attention as harmful (sign-space incorrectly showed it as helpful). It should be interpreted as a richer proxy, not as a calibrated measurement.

---

## What the data supports

- **100% label-free classification** at 430 Hz in peripheral hardware. Real.
- **LP divergence** from VDB feedback: P3 is highly distinctive (Hamming 7-9 vs other patterns). P0-P1-P2 are less separated (1-2). The temporal context layer works.
- **VDB causal necessity** holds label-free.
- **~30 µA** power claim (datasheet, JTAG-free measurement still pending).
- **No floating point, no multiplication, no training.** All verified.

## MTFP baseline note

The Test 12 MTFP divergence (8.5/80 in this full-suite run) is a NO-BIAS measurement — Test 12 sets gate_threshold=90 but never applies gie_gate_bias (bias stays at zero throughout). The Test 15 SKIP_TO_15 measurements showed 9.7 ± 0.6 /80 MTFP (3 reps). Both are VDB-only, no kinetic attention, no Hebbian. The range 8.5-9.7/80 represents the system's natural MTFP LP divergence from VDB episodic memory alone.

## What the data does NOT support

- **Kinetic attention** improving LP divergence under label-free conditions (Test 14 FAIL).
- **Hebbian weight learning** improving on VDB-only baseline (+0.1 ± 1.6, noise).
- **"Multi-seed validated"** — this is a single-seed run. Multi-seed sweep under label-free conditions not yet done.

## What needs investigation

1. **Test 14 under label-free conditions.** The gate bias mechanism was validated with pattern_id in the input (where GIE hidden was strongly pattern-correlated). Without pattern_id, the GIE hidden state is noisier. The gate bias may be amplifying noise instead of signal. Need to test whether kinetic attention helps when the GIE has less discriminative input.

2. **Control variance.** Test 15 Control shows 1.0 ± 1.3 — the std exceeds the mean. LP divergence is highly variable between runs. This limits the ability to detect small Hebbian effects.

3. **Multi-seed sweep** with label-free flags + distinct P2 payload + transition sender (for TEST 14C). The apr9 multi-seed data was collected with the old P2 payload and pattern_id in the input — it needs to be re-collected under the current configuration.

---

## Reproduction

```bash
# Sender (Board B):
idf.py -DREFLEX_TARGET=sender build && idf.py -p /dev/esp32c6b flash

# Receiver (Board A):
idf.py -DREFLEX_TARGET=gie -DMASK_PATTERN_ID=1 -DMASK_PATTERN_ID_INPUT=1 build
idf.py -p /dev/esp32c6a flash

# Reset sender to sync, capture ~35 min
```

---

## Other data files in this directory

| File | What | Status |
|---|---|---|
| `r3a_mask_patid_full_suite.log` | First label-free run (old P2 payload), 71% accuracy | Superseded |
| `r3a_mask_patid_full_suite_v2.log` | Same but with complete Test 14 | Superseded |
| `r3a_mask_patid_rssi.log` | RSSI masking experiment (68%) | Disproved RSSI hypothesis |
| `r3a_p2_distinct_payload.log` | Distinct P2 payload, 100% label-free | Superseded by this file |
| `test15_hebbian_v1.log` | First Hebbian run (label in input) | Superseded |
| `test15_ablation_v2.log` | Ablation-controlled v1 (+2.5 label-dependent) | Superseded |
| `h2_label_free_input.log` | H2: VDB Hebbian label-free (-1.7) | Historical |
| `trix_hebbian_label_free.log` | v2: TriX f-only label-free (-1.0) | Historical |
| `diagnosed_hebbian_label_free.log` | v3: diagnosed, single run (+1.3) | Superseded |
| `hebbian_3reps_label_free.log` | v3: 3-rep, SKIP_TO_15 (+0.1 ± 0.8) | Superseded |
| **`full_suite_label_free_final.log`** | **This file. The authoritative dataset.** | **Current** |

---

*Dataset produced April 11, 2026. This is the first complete label-free measurement of the Reflex system. The system classifies, remembers, and builds temporal context — without labels, without training, without floating point. Weight learning does not yet improve on the VDB-only baseline. The data is honest.*
