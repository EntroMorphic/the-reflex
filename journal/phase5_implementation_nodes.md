# Lincoln Manifold: Phase 5 Implementation — NODES

*Phase 2. Grain identification. April 6, 2026.*
*Observer: Claude Opus 4.6*

---

## Node 1: The Sign Error in the March 22 Synthesis

The synthesis spec (`kinetic_attention_synth.md` line 113) uses `gate_threshold + gate_bias[group]`. The simulation (`test14c.c` line 194) uses `GATE_THRESHOLD - gate_bias[group]`. Positive bias is supposed to lower the threshold (fire more easily). The sim is correct; the spec has a sign error.

**Why it matters:** If implemented as written in the spec, gate bias would *suppress* the expected pattern's neurons instead of amplifying them. The mechanism would work in reverse — punishing priors instead of reinforcing them. TEST 14 would appear to show no effect or negative effect, and the conclusion would be "kinetic attention doesn't work," when in fact it was wired backwards.

**Resolution:** Follow the sim. `effective_threshold = gate_threshold - gie_gate_bias[group]`. Floor at MIN_GATE_THRESHOLD. Document the correction.

---

## Node 2: The Sim Is Miscalibrated Against Hardware

Hardware LP CHAR data (from today's 13/13 run):
- fires/step: 13-16 out of 16 (81-100% firing rate)
- blend/step: 9-11 out of 16 (56-69% blend rate)

Simulation parameters:
- `LP_SIM_THRESHOLD=2` → produces sparse firing (many trits stay at 0)
- `BLEND_ALPHA=0.2` → 20% per-trit blend probability

The sim is too sparse and under-blending by ~3×. Recalibrating to match hardware will dramatically change the dynamics — LP state will be noisier, more volatile, and gate bias effects may wash out.

**Why it matters:** The sim's positive result (lp_delta > 0 at step 30 for 14C vs 14A) may be an artifact of artificially low firing rates. In the sim, a few neurons changing is visible. On hardware, nearly all neurons fire every step — the signal is in the *which* neurons fire (the pattern of the 13-15 that fire vs the 1-3 that don't), not whether they fire at all.

**Tension with Node 5:** If recalibration kills the sim effect, do we still proceed to firmware? The sim is a prediction tool, not a gate. Hardware might show effects the sim can't model. But if the sim can't find the effect at all after recalibration, it means our measurement approach (LP Hamming) might need to change too.

---

## Node 3: Gate Bias Lives in HP BSS, Not LP SRAM

The March 22 synthesis placed `gate_bias[4]` in LP SRAM (`ulp_gate_bias`). But the ISR and the HP core both run on the HP core's CPU. The ISR reads gate_bias during `isr_loop_boundary()`. The test harness writes it during the classification callback. Both are HP-core contexts.

LP SRAM access from the HP core goes through the AHB-Lite bus and has latency. Regular BSS is direct cache. The gate bias should be a regular `volatile int8_t gie_gate_bias[4]` in `gie_engine.c`, exported through `gie_engine.h`.

**Why it matters:** Putting it in LP SRAM adds bus contention with the LP core's CMD 5 execution. The ISR runs at 430 Hz. An LP SRAM access during every ISR invocation × 32 neurons = 13,760 LP SRAM reads per second. The LP core wakes at 100 Hz. Bus contention could stall the ISR or the LP core, introducing timing jitter. HP BSS avoids this entirely.

**Resolution:** `volatile int8_t gie_gate_bias[4]` in `gie_engine.c`. Exposed via `gie_engine.h`. Written by test harness, read by ISR. No LP SRAM involvement.

---

## Node 4: The ISR Modification Is Three Lines

In `isr_loop_boundary()`, the current blend code (step 4) is:

```c
int32_t thresh = gate_threshold;
// ...
if (thresh > 0) {
    f = (f_dot > thresh || f_dot < -thresh) ? tsign(f_dot) : T_ZERO;
}
```

The Phase 5 modification:

```c
int32_t thresh = gate_threshold;
int group = n / TRIX_NEURONS_PP;  // n is the neuron index, already in scope
int32_t bias = gie_gate_bias[group];
int32_t eff = thresh - bias;
if (eff < MIN_GATE_THRESHOLD) eff = MIN_GATE_THRESHOLD;
if (eff > 0) {
    f = (f_dot > eff || f_dot < -eff) ? tsign(f_dot) : T_ZERO;
}
```

Five lines, technically. All in IRAM (the function is already `IRAM_ATTR`). `gie_gate_bias` is a BSS read — one load instruction. `TRIX_NEURONS_PP` is a compile-time constant so the division compiles to a shift.

**Why it matters:** The ISR modification is the smallest part of the implementation. The risk is not in the ISR code — it's in the HP-side agreement computation and the bias calibration.

---

## Node 5: The Sim Recalibration Is the Gate Decision

Before writing firmware, recalibrate the sim and re-run. Two outcomes:

**A. Effect survives recalibration:** lp_delta > 0 at step 30, 14C beats 14A. Proceed to firmware with confidence that the mechanism produces a measurable LP-level effect at hardware-realistic parameters.

**B. Effect collapses:** lp_delta ≈ 0 at all steps. This means the gate bias mechanism, while architecturally sound, doesn't produce a measurable LP divergence difference when LP dynamics are as noisy as hardware. Options:
- Change the measurement from LP Hamming to something more sensitive (LP trajectory correlation, VDB retrieval pattern shift)
- Increase BASE_BIAS significantly (30-40 instead of 15)
- Accept that the LP-level effect is below measurement threshold and look for the effect at the GIE level instead (gate firing rate by pattern group)
- Proceed to firmware anyway — the sim is a model, not the system

**Why it matters:** Spending time on firmware that the sim predicts will show no effect is inefficient. But the sim is a crude model of the hardware (especially the GIE signal model with uniform distributions). The hardware has real RF characteristics, real RSSI variation, real pattern timing. The sim might be wrong in either direction.

**My recommendation:** Recalibrate and re-run. If effect collapses, try BASE_BIAS=30 and BASE_BIAS=45. If still nothing, proceed to firmware anyway with BASE_BIAS=30 and accept that the sim couldn't capture the dynamics. The hardware test is the real test.

---

## Node 6: The LP Accumulator Already Exists

The test harness already maintains `lp_running_sum[4][16]` and `lp_sample_count[4]` for the Hamming matrix analysis in TEST 12/13. The agreement computation uses these directly:

```c
int8_t lp_mean[LP_HIDDEN_DIM];
for (int j = 0; j < LP_HIDDEN_DIM; j++)
    lp_mean[j] = tsign(lp_running_sum[p_hat][j]);
int dot = 0;
for (int j = 0; j < LP_HIDDEN_DIM; j++)
    dot += tmul(lp_now[j], lp_mean[j]);
float agreement = (float)dot / LP_HIDDEN_DIM;
```

No new data structures. The entire gate bias computation hooks into existing infrastructure.

**Why it matters:** Phase 5 implementation does not require architectural changes to the test harness. It's an additive ~30 lines that use existing accumulators and write to a new 4-byte volatile array. The refactor we did today cleared the path for exactly this kind of clean addition.

---

## Node 7: TEST 14 Is Three Conditions in One Test Function

The cleanest implementation: a single `test_14()` function that runs three conditions sequentially:

1. **14A (baseline):** `gie_gate_bias = {0,0,0,0}` throughout. 120s. Captures LP Hamming matrix.
2. **14C (full bias):** Agreement-weighted gate bias, Phase 1 (120s P1) + Phase 2 (30s P2). Captures LP Hamming matrix + gate_bias trace + lp_delta trajectory.
3. **14C-iso (isolation):** Gate bias active only in Phase 2 (bias=0 during P1 accumulation, then activated at pattern switch). Isolates the transition mechanism from the prior-building mechanism.

Each condition resets: `cfc_init`, `vdb_clear`, LP hidden zero, accumulators zero, gate_bias zero.

**Why it matters:** Three conditions × 150s each = 7.5 minutes of additional test time. The full suite (TEST 1-14) will run ~12 minutes. Acceptable. The three conditions answer three questions: does the baseline replicate? does the mechanism work? does the transition mechanism work independently?

---

## Node 8: Board B Must Hold a Single Pattern for TEST 14C

TEST 14C requires Board B to hold P1 for 120s, then switch to P2 and hold for 30s. The current sender firmware cycles through all 4 patterns on a 27-second rotation. TEST 14C needs a different sender behavior.

**Options:**
- (A) Modify sender firmware to accept pattern-hold commands via ESP-NOW from Board A
- (B) Use the existing sender as-is and accept that "P1" means "whichever pattern the sender is currently transmitting" — measuring the transition when the sender naturally cycles
- (C) Use the sim's approach: the GIE signal model already separates "true pattern" from noise, and TEST 14C in the sim uses a fixed P1→P2 switch

**Why it matters:** The paper claim for TEST 14C is about the *transition* — the LP state updating when the input pattern changes. If Board B is cycling through 4 patterns every 27 seconds, there are transitions every 2-13 seconds. The LP prior never fully commits to one pattern. The experiment doesn't isolate the transition mechanism.

**Resolution:** Option B is sufficient for a first run. The sender's 27-second cycle means the system sees P1 for 2s, then P1→P2 transition, P2 for 13s, P2→P3 transition, etc. Each transition is an independent test of the mechanism. The 120s run captures 4+ complete cycles. The pass criterion (lp_delta > 0 at step 30 post-switch) can be evaluated at each natural transition. This is actually better than a single artificial switch — it's multiple replications in one run.

But for the paper's headline result (cleanly controlled P1→P2 transition with committed prior), a dedicated sender mode will eventually be needed. That's a firmware change to `espnow_sender.c` — add a command mode where Board B holds a fixed pattern until told to switch. Not needed for the first silicon test.

---

## Key Tensions

| Tension | Nodes | Status |
|---------|-------|--------|
| Sign error in spec vs sim | 1, 4 | Resolved: follow sim (subtraction) |
| Sim recalibration vs firmware progress | 2, 5 | Resolve by running recalibrated sim first |
| LP SRAM vs HP BSS for gate bias | 3, 4 | Resolved: HP BSS (no bus contention) |
| Controlled switch vs natural cycling | 7, 8 | Resolved: natural cycling for first run, controlled switch for paper |
