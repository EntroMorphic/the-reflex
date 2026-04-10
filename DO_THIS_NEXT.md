# DO THIS NEXT

*Written April 9, 2026. End of session.*
*Supersedes the April 8 version. Two compounding bugs fixed; multi-seed TEST 14C now measurable on silicon; papers need rewriting around the corrected metrics.*

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

## BLOCKING: Fix the test_kinetic.c verdict logic

`embedded/main/test_kinetic.c` prints `Crossover step: 0` for every condition in every run of this dataset. That sentinel value is also what the verdict gates compare against, so:

```
Full system crossover ≤ 30:    PASS (step 0)
Bias helps (full ≤ no-bias):   PASS (0 vs 0)
Ablation slower:               PASS (0 vs 0)
```

All four gates pass trivially and the verdict is always PASS, even for Seed B where the TriX@15 evidence clearly shows bias is **hurting** for that seed. The verdict logic is lying.

### What to do

1. Replace the "crossover step" metric with one of:
   - First step where `pred` flips from the P1 ground truth to the P2 ground truth **and stays there for ≥3 consecutive steps**.
   - First step where `(align_P2 − align_P1) > 0` sustained for ≥3 steps.
2. Rewrite the verdict gates against the new metric:
   - `TriX@15 > 10/15` for Full (>66%)
   - `Full TriX@15 ≥ No-bias TriX@15 − margin` with a 2-step slack (so Seed B would correctly FAIL this gate and the overall run would FAIL for Seed B, flagging the headwind)
   - `Ablation final alignment gap < No-bias final alignment gap` (regression test)
3. Re-run the seed sweep against the new verdict to confirm gates behave correctly.

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

### Deprecate the apr8_2026 dataset

`data/apr8_2026/` contains ~16 log files from the broken-enrollment era. None of them have valid TriX classification for any condition. Alignment traces may be partially salvageable but need careful re-reading.

**Action:** Add a `data/apr8_2026/DEPRECATED.md` note pointing at `data/apr9_2026/SUMMARY.md` and explaining that these logs predate both the `trix_enabled` and enrollment-starvation fixes. Keep the files; the historical record matters. But flag them as invalid so no one accidentally cites them.

### Current apr9_2026 contents

- `results_final_seed_a.log` — Seed A, full TEST 14C, 15/15 across all conditions
- `results_final_seed_b.log` — Seed B, full TEST 14C, headwind visible
- `results_final_seed_c.log` — Seed C, full TEST 14C, 15/15 across all conditions
- `SUMMARY.md` — digest with tables, analysis, caveats (committed)

---

## KNOWN LIMITATIONS (report honestly)

### TriX "release within 4 steps" claim is wrong

The previous paper language said "bias releases within 4 steps of the switch." The actual mechanism is:
- `pred` flips at **step +1** (1 step after first P2 packet) for clean seeds
- The bias **magnitude** decays linearly at ~1 unit/step, reaching zero at ~step +12–15
- The new P2 prior forms in parallel, reaching stable magnitude by step +20

The "4 steps" number appears to have been the agreement-weighted disagree-count threshold (4 trits), not a time constant. The paper should cite both: `pred` latency = 1 step, bias-release duration = ~12 steps, new-prior formation = ~20 steps.

### Seed B headwind is quantified

Full 8/15 < No bias 12/15 < Ablation 14/15. Full bias is **actively harmful** for Seed B. The LP projection for this seed collapses P1 and P2 to near-parallel directions; the bias amplifies the wrong neurons.

This is a negative result that belongs in the paper. It motivates Pillar 3 (Hebbian weight updates) as the designed fix.

### Seed A/C 15/15 is real but context-dependent

Both clean seeds show 100% TriX accuracy post-switch because the ground-truth-labeled ESP-NOW packet (pattern_id in the payload) survives the degraded RSSI thermometer. TriX is mostly reading the pattern_id trits, not the RSSI. This is the same result the papers document at 100% for 4-pattern classification. It's honest to note that post-switch TriX accuracy is bounded by the informativeness of the payload trits, which are explicitly labeled. Once the RSSI dead zone lands (and the explicit label trits are masked more aggressively), this will become a sterner test.

---

## FORWARD RESEARCH (not blocking)

### Pillar 1: Dynamic Scaffolding
Unchanged. VDB pruning requires kinetic-attention data to know what's load-bearing. The apr9 dataset provides that data.

### Pillar 2: SAMA (Substrate-Aware Multi-Agent)
Unchanged. Treat ESP-NOW packets as GIE inputs without OS involvement. Requires kinetic attention for context-sensitive response — now demonstrated.

### Pillar 3: Hebbian GIE
**Priority raised.** The Seed B headwind is now quantitative evidence that fixed random weights are the bottleneck. Learned weights would fix projection degeneracy. This is the path from "works on 2/3 seeds" to "works on every seed."

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
