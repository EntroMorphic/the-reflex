# DO THIS NEXT

*Written April 8, 2026. End of session.*
*Context: 4 commits pushed today. TriX dispatch working. Ternary agreement implemented. Multi-seed 14C data collected but invalid for the Full condition (trix_enabled was not set — bias was never active). VDB stabilization data is valid.*

---

## CRITICAL: Multi-Seed 14C Must Be Re-Run

The multi-seed TEST 14C crossover data in the papers and session doc was collected with `trix_enabled` not set in Tests 12-13. The ISR never ran TriX classification during those runs. The Full condition (CMD5+bias) was accidentally running as no-bias — the bias was computed from `trix_pred = -1`, which doesn't match any pattern, so the accumulator selection returned NULL and no bias was ever applied.

**The VDB stabilization finding (no-bias vs ablation) is valid.** Those conditions don't use the bias mechanism. The no-bias condition dispatches CMD 5 (with VDB blend) and the ablation dispatches CMD 4 (no blend). The transition dynamics depend on VDB feedback, not on the bias or the classification label. The alignment traces for no-bias and ablation are real.

**The Full condition crossover data is invalid.** Every "Full" crossover number in the papers (0, 22, 2 for Seeds A, B, C) is actually the no-bias crossover. The ternary agreement mechanism was never tested on silicon during those runs.

### What to do

1. Flash sender with `TRANSITION_MODE=1`:
   ```
   cd embedded
   idf.py fullclean
   idf.py -DREFLEX_TARGET=sender -DTRANSITION_MODE=1 build
   idf.py -p /dev/ttyACM0 flash
   ```
   Verify: serial output should show `[MODE] TRANSITION: P1 (90s) -> P2 (30s) -> repeat`

2. For each seed (0xCAFE1234, 0xDEAD5678, 0xBEEF9ABC):
   ```
   idf.py fullclean
   idf.py -DREFLEX_TARGET=gie -DLP_SEED=<seed> -DSKIP_TO_14C=1 build
   idf.py -p /dev/ttyACM1 flash
   ```
   Capture serial to `data/apr8_2026/results_final_seed_{a,b,c}.log`

3. Sync timeout is 200s. Full condition runs first (needs 60s sync). Each condition takes ~2-3 min. Total per seed: ~10-15 min. Total for 3 seeds: ~30-45 min.

4. **What to look for in the data:**
   - `bias=[X,Y]` should be non-zero during P1 phase for the Full condition. If `bias=[0,0]` throughout, the mechanism is still not engaging — debug.
   - `pred=1` or `pred=2` (not `pred=-1`). If pred is -1, trix_enabled is not set.
   - Crossover step for Full condition vs no-bias. The ternary disagree-count should release bias within 4 steps of the switch.
   - Ablation regression at step +20 (P1 alignment exceeding P2). This is the CLS finding.

5. Update papers with corrected crossover data. Update session doc.

---

## BLOCKING: UART-Only Verification

Every run in the project's history uses USB-JTAG for serial output and power. The "peripheral-autonomous" and "~30 µA" claims require data without a development tool attached.

### What to do

1. Wire GPIO 16 (TX) and GPIO 17 (RX) to a secondary serial bridge (FTDI, CP2102, or another ESP32)
2. Power Board A from battery or dumb USB (5V VBUS, no data lines)
3. Run the full 15-test suite with the normal sender
4. Measure current with a µA-resolution meter (INA219 or bench DMM in series)
5. Record: all 15 tests PASS/FAIL, current draw during GIE free-run, current draw during LP sleep

### What this proves

- The GIE runs on peripheral hardware independent of USB-JTAG
- The ~30 µA claim is measured, not inferred from datasheet
- The "peripheral-autonomous" language in all three papers is backed by data

---

## SHOULD DO: Remove Last Float

The bias magnitude computation in Test 14 still uses a float intermediate:
```c
int b = (margin > 0)
    ? (BASE_GATE_BIAS * BIAS_SCALE * margin + LP_HIDDEN_DIM / 2) / LP_HIDDEN_DIM
    : 0;
```

This IS integer now (the float was removed). But verify by grep: `grep -n "float" embedded/main/geometry_cfc_freerun.c` — any remaining floats should be in display/diagnostic code only, not in the mechanism path.

The "no floating point in the mechanism path" claim should be airtight before submission.

---

## SHOULD DO: RSSI Dead Zone (Phase B, Ternary Remediation)

The RSSI thermometer (trits [0..15]) is binary: `(rssi >= threshold) ? +1 : -1`. Never 0. The input is 81% binary.

### What to do

In `gie_engine.c`, `espnow_encode_input()`:
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

### Validation

1. Build with normal sender, flash Board A
2. Run Test 11: check signature energy (expect ~2-4 fewer non-zero trits per signature)
3. Run Test 14C: check TriX accuracy (should remain high)
4. If accuracy drops: try RSSI_MARGIN = 1

### Why this matters

The ternary engine is designed for {agree, disagree, uncertain}. An input that never says "uncertain" denies the engine access to the zero state. The dead zone adds honest uncertainty where the signal is noisy.

Full LMM cycle: `journal/ternary_remediation_{raw,nodes,reflect,synth}.md`

---

## KNOWN LIMITATIONS (Report Honestly)

### Seed B Headwind

Seed B (0xDEAD5678) shows a transition headwind under gate bias. The LP projection for this seed doesn't separate P1 from P2, so the ternary disagree-count stays below 4 for many steps. This is a projection limitation, not a mechanism failure.

**The corrected headwind number needs to come from the re-run (item #1 above).** The current number (22 steps) is from a run where the bias was never active.

Fix: Pillar 3 (Hebbian GIE) — learned weights fix the projection degeneracy. The mechanism should not be enabled unconditionally with random weights in production.

### TriX Accuracy Scope

100% on the 4-pattern sender with well-separated signatures. Degrades on the P1-P2 pair under the 2-pattern transition sender (73% cross-dot ratio). The structural guarantee (W_f hidden = 0) ensures TriX is independent of the prior, but does not guarantee discrimination between patterns with high signature overlap. Documented in PAPER_KINETIC_ATTENTION.md Section 2.2.

### Test 12 P2-P3 Under TriX Dispatch

TriX dispatch produces a different label distribution than CPU core_pred (P0=310, P1=156, P2=39, P3=51 vs the more even core_pred distribution). Some pairs may collapse (P2-P3 Hamming=0 in one run). Within expected variance but noted.

---

## FORWARD RESEARCH (Not Blocking)

### Pillar 1: Dynamic Scaffolding
VDB pruning — LP core monitors node utility, prunes redundant nodes, retains outliers. The 64-node limit becomes a sliding window. Requires kinetic attention data to know what's load-bearing.

### Pillar 2: SAMA (Substrate-Aware Multi-Agent)
Treat incoming ESP-NOW packets as GIE inputs without OS involvement. Robot A classifies Robot B's GIE state through its own representational lens. Requires kinetic attention for context-sensitive inter-agent response.

### Pillar 3: Hebbian GIE
LP core generates weight-update signal from VDB mismatch, applies to GDMA descriptor chain in-situ. Fixes Seed B. Requires kinetic attention as the behavioral signal. This is the path to learned weights that fix projection degeneracies.

### Phase C: MTFP RSSI Encoding
Replace the 16-trit RSSI thermometer with a 5-trit MTFP value (sign + 2 exp + 2 mant). Frees 11 trits for other features. Separate LMM cycle. Changes input layout — needs full re-enrollment and validation.

### Phase C: Timing Feature Expansion
Use freed sequence trits [104..119] for MTFP timing features: gap variance, burst count, steady count. Adds temporal discrimination without increasing input dimension.

---

## COMMIT ORDER FOR NEXT SESSION

1. Re-run multi-seed 14C (the critical data fix)
2. Update papers with corrected data
3. RSSI dead zone (Phase B)
4. Validate full suite with RSSI dead zone
5. Commit and push

Item 1 is the priority. The papers have invalid Full-condition data. Fix the data before anything else.

---

*The one-line fix (`trix_enabled = 1`) unblocked TriX dispatch. The data collected before that fix is invalid for the bias condition. The VDB stabilization data is valid. Re-run with the mechanism actually engaged.*
