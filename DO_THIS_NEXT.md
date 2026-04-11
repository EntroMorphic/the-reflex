# DO THIS NEXT

*Written April 9, 2026. Updated April 11.*
*Supersedes the April 8 version. Two compounding bugs fixed; multi-seed TEST 14C measurable on silicon; label-free 100% classification achieved after P2 payload redesign; papers need rewriting around the corrected metrics.*

---

## Context — what actually happened

Two bugs were invalidating every multi-seed TEST 14C run on record. They compounded — fixing one alone would not have surfaced the other.

1. **`trix_enabled` was not set in Tests 12-13** (fixed April 8, commit `f97ac1c`). The ISR never ran TriX classification. `trix_pred = -1` matched no pattern accumulator, so the agreement-weighted gate bias computed `bias = 0` every step of the Full condition. The "Full" crossover column in every pre-April-9 multi-seed table was actually measuring the no-bias condition.

2. **Enrollment starvation in transition mode** (fixed April 9, commit `63877f7`). The sender's TRANSITION_MODE started sending P1 immediately on boot, so Board A's Test 11 Phase 0a (30s observation window) only ever saw pattern P1. `sig[0]`, `sig[2]`, and `sig[3]` were computed from zero samples and stayed zero. TriX's argmax over `sig[] · hidden` could never select P2 because P2 had no weights.

The April 8 version of this doc only identified bug #1 and concluded that "VDB stabilization data (no-bias vs ablation) is valid." That conclusion is wrong. Bug #2 affected every condition in every pre-April-9 run because the TriX classifier itself had no P2 signature. Alignment traces (computed from LP MTFP means accumulated during TEST 14C, not from `sig[]`) may be partially salvageable, but all TriX-accuracy numbers from the apr8 dataset are invalid.

---

## New authoritative dataset

`data/apr9_2026/results_final_seed_{a,b,c}.log` — three seeds × three conditions, all on silicon, both bugs fixed.

Digest: `data/apr9_2026/SUMMARY.md` (committed). Raw logs: gitignored (local-only).

Headline TriX@15 numbers:

| Seed | Full | No bias | Ablation |
|---|---|---|---|
| A | 15/15 | 15/15 | 15/15 |
| B |  8/15 | 12/15 | 14/15 |
| C | 15/15 | 15/15 | 15/15 |

Seeds A and C: clean. Seed B: documented degenerate-projection headwind visible — Full underperforms No-bias underperforms Ablation. This is the real "Seed B headwind" the papers describe, now properly grounded in data.

---

## BLOCKING: Papers need rewriting around the corrected metrics

The three paper drafts (`docs/PAPER_KINETIC_ATTENTION.md`, `docs/PAPER_CLS_ARCHITECTURE.md`, `docs/PRIOR_SIGNAL_SEPARATION.md`) cite crossover-step numbers from the broken dataset. Those numbers are all wrong and the metric itself is dead (see "Broken metrics" below). The papers need to be reframed around:

- **TriX@15** (first-15-step post-switch accuracy) as the headline metric
- **Alignment traces** as the secondary metric showing CLS structure
- **Bias release progression** (linear decay ~1/step, ~12–15 steps to full release) as the mechanism demonstration
- **`pred` flip latency** (step +1 for clean seeds) as the "how fast does the prior release" answer — and this is what should replace the "within 4 steps" language, because 4 steps was a measurement-window artifact, not the actual mechanism timing

Every crossover-step value currently printed in the papers is invalid. Do a full pass grepping for "crossover" in `docs/PAPER_*.md` and replace with the correct metric citations.

Also: the Seed B headwind is now a real finding with real numbers (Full 8/15 < No-bias 12/15 < Ablation 14/15). The papers currently report it qualitatively; they should report it quantitatively and use it as evidence that the mechanism is projection-dependent (motivating Pillar 3 Hebbian GIE).

---

## BLOCKING: Fix the test_kinetic.c verdict metric (not "broken" — saturated)

I previously called the `Crossover step: 0` value a "sentinel." That was wrong. After tracing the code:

```c
/* Detect crossover */
if (crossover_step < 0 && align2 > align1)
    crossover_step = phase2_step;
```

The metric is honestly measuring "first step where align_P2 > align_P1." It returns 0 in every condition because the LP MTFP state shifts the moment the first P2 packet arrives — `align_P2` exceeds `align_P1` immediately. The metric isn't lying; it has **zero discriminative resolution** for this experimental setup. The verdict gates pass trivially because comparing `0 ≤ 0` is true.

### Why this matters

For Seed B, the TriX@15 metric clearly shows the bias is **hurting** (Full 8/15 < No-bias 12/15). The verdict prints PASS because crossover-based comparisons all succeed. A reviewer reading the verdict would think Seed B's bias is fine. It isn't.

### What to do

1. **Replace the crossover metric with one that has resolution.** Candidates:
   - **`pred` flip latency held for ≥3 steps:** first step where `core_pred == ground_truth_p2` and stays there for at least 3 consecutive steps. Discriminates Seed B (jitter) from Seeds A/C (clean).
   - **Sustained alignment gap:** first step where `(align_P2 − align_P1) ≥ M` for ≥3 steps, with M chosen to require real separation (e.g., M=5).
   - **Bias release time:** number of steps for `gie_gate_bias[old_pattern_group]` to decay below a threshold (e.g., 2). Directly measures the release dynamics.
2. **Rewrite the verdict gates against the new metric:**
   - `Full TriX@15 ≥ 10/15` (>66%) — would FAIL for Seed B, correctly flagging the headwind
   - `Ablation alignment gap regresses by ≥3 by step 30` (CLS prediction)
   - Drop `Bias helps (full ≤ no-bias)` — for the Seed B case the answer is NO and that's a real result, not a failure
3. **Keep the crossover-step value** as a logged-but-not-gated diagnostic. It's honest, just not useful as a pass/fail signal.
4. **Re-run the seed sweep** against the new verdict to confirm gates behave correctly (Seeds A/C PASS, Seed B FAILs the Full gate and PASSes the others — flagging the headwind explicitly in the test output).

---

## BLOCKING: UART-only verification (carryover)

Unchanged from the April 8 version. Every run to date uses USB-JTAG for serial and power. The "peripheral-autonomous" and "~30 µA" claims require data without a development tool attached.

### What to do

1. Wire GPIO 16 (TX) and GPIO 17 (RX) to a secondary serial bridge
2. Power Board A from battery or dumb USB (5V VBUS, no data lines)
3. Run the full suite with the normal (cycling) sender
4. Measure current with INA219 or bench DMM in series
5. Record: all tests PASS/FAIL, current draw during GIE free-run, current draw during LP sleep

This is orthogonal to the TEST 14C work. Can run in parallel.

---

## SHOULD DO: Robustify the sender enrollment window

The April 9 fix (`63877f7`) depends on **manual reset synchronization**. Board A's enrollment (~45s of activity starting ~10s into boot) must overlap Board B's 90s cycling window. In practice this works if Board B is reset within ~30s of Board A. If someone later runs the test without knowing this, Board B may already be in the transition loop and the bug returns silently.

### Options

- **(A) Document and accept.** Add reset-sync note to `embedded/docs/FLASH_GUIDE.md` and the paper methods section. Cheapest.
- **(B) Periodic re-cycling.** Sender runs the 90s enrollment window at the start of every P1 phase (not just at boot). Airtime cost: ~6 extra cycles every 120s, ~50% duty cycle spent on enrollment. Robust but wasteful.
- **(C) ESP-NOW handshake.** Board A sends a "begin enrollment" packet, sender responds by starting the cycling window. Complex and invasive.

Recommend (A) for the paper submission, (B) as a v2 if reviewers ask.

---

## SHOULD DO: RSSI Dead Zone (carryover, Phase B ternary remediation)

Unchanged from the April 8 version. The RSSI thermometer is 81% binary. Add a dead zone so the ternary engine gets access to the {+1, 0, −1} state space it was designed for.

In `gie_engine.c::espnow_encode_input()`:
```c
#define RSSI_MARGIN 2
for (int i = 0; i < 16; i++) {
    int threshold = -80 + i * 4;
    int diff = st->rssi - threshold;
    new_input[i] = (diff > RSSI_MARGIN) ? T_POS
                 : (diff < -RSSI_MARGIN) ? T_NEG
                 : T_ZERO;
}
```

**Previous attempt:** `RSSI_MARGIN=2` was tested on silicon (April 8) and caused P0/P1/P2 LP hidden collapse in Test 12. Reverted. The comment in `gie_engine.c:1264` documents this. Retry with `RSSI_MARGIN=1` first.

**When to do it:** After papers are rewritten. This changes input encoding and would invalidate the apr9 dataset if done first.

---

## DATA HYGIENE

### Deprecate the apr8_2026 dataset [DONE]

`data/apr8_2026/DEPRECATED.md` committed in `ef4902d`. Points at `data/apr9_2026/SUMMARY.md`. Explains both bugs. Note: the deprecation may be too sweeping — VDB distillation (TEST 13) and LP characterization data may still be valid (see red-team item R4).

### Current apr9_2026 contents

- `results_final_seed_a.log` — Seed A, full TEST 14C, 15/15 across all conditions
- `results_final_seed_b.log` — Seed B, full TEST 14C, headwind visible
- `results_final_seed_c.log` — Seed C, full TEST 14C, 15/15 across all conditions
- `SUMMARY.md` — digest with tables, analysis, caveats (committed)

---

## KNOWN LIMITATIONS (report honestly)

### TriX "release within 4 steps" claim conflates a threshold with a time constant

The previous paper language said "bias releases within 4 steps of the switch." After tracing `test_kinetic.c::run_test_14c` and `gie_engine.c::isr_loop_boundary`, the actual mechanism has **two release paths**:

**Soft path (geometric decay, runs every step unconditionally):**
```c
bias_i[p] = (int16_t)(bias_i[p] * 9 / 10);
```
This is multiplied through every step regardless of agreement. Half-life ≈ 6.6 steps. With BIAS_SCALE=10 internal resolution, the displayed `gie_gate_bias` value follows:

```
bias_i: 130 → 117 → 105 → 94 → 84 → 75 → 67 → 60 → 54 → 48 → 43
disp:    13 →  11 →  10 →  9 →  8 →  7 →  6 →  6 →  5 →  4 →  4
```

This matches Seed A's logged data exactly. The "linear ~1/step" appearance I described in the SUMMARY is an integer-truncation artifact of geometric decay — not actually linear.

**Hard path (disagree-count zero):**
```c
if (n_disagree >= 4) {
    bias_i[pred] = 0;
}
```
This zeros the bias **in one step** when ≥4 trits of the current LP state disagree with the predicted-pattern accumulator's sign vector. **It was not exercised on any clean seed in this dataset.** The release happened entirely via the soft geometric path.

**The "4" in the original claim was the disagree-count threshold (number of trits), not a time constant (number of steps).** The two were conflated. The hard path can technically zero the bias in 1 step if conditions are met; the soft path is what actually runs in practice and takes ~22 steps to drop bias by 90% (0.9^22 ≈ 0.10).

### Corrected mechanism timing for the papers

| Event | Timing | Mechanism |
|---|---|---|
| `pred` flip from old to new pattern | step +1 (clean seeds) | TriX classification |
| Old-prior bias decay (90% gone) | ~step +22 | Geometric ×0.9/step |
| Old-prior bias decay (50% gone) | ~step +7 | Geometric ×0.9/step |
| New-prior bias formation begins | step +15 | Gated on `T14C_MIN_SAMPLES` P2 samples |
| New-prior bias stable | step +20 (Seed A) | Agreement-check sets `bias_i[2]` from accumulator |
| Disagree-zero hard release | not observed | Would fire if `n_disagree ≥ 4` post-switch — didn't happen |

### What this means for the prior-as-voice framing

The "soft decay + hard floor" structure is actually richer than "release within 4 steps" suggested. The prior doesn't disappear on contact with disagreement — it **fades** over a characteristic time of ~6.6 steps while the new prior is being constructed in parallel. The hard disagree-zero path is a safety release for cases where the new evidence is overwhelming. In normal operation (Seed A/C), the soft decay is sufficient and the system transitions smoothly. This is closer to "the prior is a fading voice" than "the prior is a verdict that gets revoked," which actually fits the project's epistemic framing better than the original claim did.

### Seed B headwind is quantified

Full 8/15 < No bias 12/15 < Ablation 14/15. Full bias is **actively harmful** for Seed B. The LP projection for this seed collapses P1 and P2 to near-parallel directions; the bias amplifies the wrong neurons.

This is a negative result that belongs in the paper. It motivates Pillar 3 (Hebbian weight updates) as the designed fix.

### Seed A/C 15/15 is real but context-dependent

Both clean seeds show 100% TriX accuracy post-switch because the ground-truth-labeled ESP-NOW packet (pattern_id in the payload) survives the degraded RSSI thermometer. TriX is mostly reading the pattern_id trits, not the RSSI. This is the same result the papers document at 100% for 4-pattern classification. It's honest to note that post-switch TriX accuracy is bounded by the informativeness of the payload trits, which are explicitly labeled. Once the RSSI dead zone lands (and the explicit label trits are masked more aggressively), this will become a sterner test.

---

## RESOLVED: Label-free accuracy (R3a)

**Pattern_id trits [16..23] were the "primary discriminator" in TriX signatures, undisclosed in papers.** With label masked, accuracy was 71% (P2 at 10%). Root cause: P2 payload `{0xAA, alt, alt, ...}` shared 48/64 payload trits with P1 `{0xFF, i, 0, 0, ...}`. Margin: ~9-13 discriminative trits out of 96. RSSI noise hypothesis disproved (masking RSSI → 68%, worse).

**Fix: distinct P2 payload** `{0x55, 0x33, 0xCC, 0x66, 0x99, 0x0F, 0xF0, 0x3C}` (commit `c7ef286`). P1-P2 cross-dot dropped from 78/96 (81%) to 29/96 (30%). Label-free accuracy: **32/32 = 100%. 14/14 PASS.**

Build flags for measurement: `-DMASK_PATTERN_ID=1` (mask label trits from signatures), `-DMASK_RSSI=1` (mask RSSI trits).

**Remaining from R3:**
- R3b: Disclose full input encoding (all 6 regions) in all three papers
- R3c: Re-examine Seed B puzzle (if TriX reads features, not labels, why does Seed B differ?)
- R3d: Reframe headline claims honestly
- Multi-seed TEST 14C re-run needed with new P2 payload (apr9 data used OLD payload)

---

## FORWARD RESEARCH (not blocking)

### Pillar 1: Dynamic Scaffolding
Unchanged. VDB pruning requires kinetic-attention data to know what's load-bearing. The apr9 dataset provides that data.

### Pillar 2: SAMA (Substrate-Aware Multi-Agent)
Unchanged. Treat ESP-NOW packets as GIE inputs without OS involvement. Requires kinetic attention for context-sensitive response — now demonstrated.

### Pillar 3: LP Hebbian — TriX-output-based (corrected April 11)
**Major revision.** The original "Hebbian GIE" proposal was wrong — it would break the structural wall. The corrected target is LP weights. An HP-side Hebbian step (`lp_hebbian_step()`) was implemented and tested with ablation control (commit `4343447`): +2.5 Hamming over control.

**BUT: label-dependent** (commit `a0d3a36`). H2 experiment: with pattern_id removed from the GIE INPUT (not just signatures), Hebbian contribution became -1.7 (harmful). The VDB mismatch error signal was exploiting label information leaked through the GIE hidden state.

**Critical secondary finding:** removing pattern_id from the input IMPROVED VDB-only LP divergence from 0.7 to 3.3/16. The recommended operating mode is `MASK_PATTERN_ID_INPUT=1`.

**Next step:** Replace VDB mismatch with TriX classifier output as the training signal. TriX is 100% accurate (structural guarantee) and genuinely label-free. Use TriX-predicted pattern identity to select the target LP state, then compute the Hebbian error against that target. This is supervised-from-classifier, architecturally clean, and doesn't leak labels through the GIE. LMM cycle in progress.

### Phase C: MTFP RSSI Encoding
Unchanged. Replace 16-trit RSSI thermometer with 5-trit MTFP value.

---

## COMMIT ORDER FOR NEXT SESSION

1. **Rewrite PAPER_KINETIC_ATTENTION.md** — replace crossover metric with TriX@15 + alignment traces. Cite apr9 SUMMARY.md. Fix the "within 4 steps" language.
2. **Rewrite PAPER_CLS_ARCHITECTURE.md** — update transition experiment section with corrected alignment data. Seed B becomes quantitative evidence for Pillar 3 necessity.
3. **Rewrite PRIOR_SIGNAL_SEPARATION.md** — shortest rewrite; check every cited number against apr9 SUMMARY.md.
4. **Fix test_kinetic.c verdict logic** — replace sentinel crossover with real metric, re-run seed sweep.
5. **Add DEPRECATED.md to data/apr8_2026/** — historical flag.
6. **UART-only verification** — physical rewiring, separate session.
7. **RSSI dead zone retry** (RSSI_MARGIN=1) — after papers settle.

Items 1-3 and 5 are docs/data only. Item 4 is code. Item 6 is physical. Item 7 is code + silicon re-run.

---

*The mechanism works. The data is honest. Two seeds pass cleanly, one seed shows the designed-fix motivation. The metric was broken in three different ways and is now either fixed or flagged for fixing. Everything downstream depends on rewriting the papers against the new numbers.*
