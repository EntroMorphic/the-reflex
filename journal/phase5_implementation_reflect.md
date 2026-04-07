# Lincoln Manifold: Phase 5 Implementation — REFLECT

*Phase 3. Axe sharpening. April 6, 2026.*
*Observer: Claude Opus 4.6*

---

## Core Insight

**The implementation is smaller than the design, and the risk is in calibration, not architecture.**

The March 22 LMM cycle solved the hard problem (agreement-weighted gate bias with decay and cold-start). Today's audit and refactor solved the structural problem (engine vs test harness separation). What remains is: put the mechanism in the engine, put the policy in the test, and calibrate against hardware reality.

The ISR change is 5 lines. The HP-side computation is ~25 lines using existing data structures. The sim recalibration is a parameter change. The test conditions are structured identically to TEST 12/13. There is no new architecture. There is no new data structure. There is one new 4-byte volatile array.

The risk is that we build something that works correctly but produces no measurable effect — the mechanism fires, the bias activates, the thresholds shift, and the LP divergence is indistinguishable from the baseline because the LP dynamics are so noisy at hardware firing rates that a 17% threshold shift is lost in the noise.

The response to that risk is not to avoid it. It's to detect it early (sim recalibration) and have a fallback (higher BASE_BIAS, different measurement metric).

---

## Resolved Tensions

### The Sign Error (Node 1)

The synthesis spec says `threshold + bias` (addition). The sim says `threshold - bias` (subtraction). The intent is: positive bias → lower threshold → fire more easily.

I traced it through three documents:
- `KINETIC_ATTENTION.md` Section 4.2: `effective_threshold = gate_threshold + lp_gate_bias[group]` with `lp_gate_bias = -2 * alignment`. So the bias is *negative* when alignment is positive, and adding a negative bias lowers the threshold. Consistent.
- `kinetic_attention_synth.md` line 113: `eff_threshold = gate_threshold + ulp_gate_bias[group]`. But line 101: `gate_bias = BASE_BIAS * MAX(0, agreement)`. The bias is *positive* when agreement is positive. Adding a positive bias *raises* the threshold. **Inconsistent with intent.**
- `test14c.c` line 194: `eff = GATE_THRESHOLD - gate_bias[group]`. Bias is positive when active. Subtraction lowers the threshold. **Consistent with intent.**

The error was introduced in the synthesis when the agreement mechanism changed the sign convention. The original design (Section 4.2) used negative biases with addition. The synthesis switched to positive biases but kept addition. The sim got it right.

**Resolution:** Use subtraction in the ISR: `eff = thresh - bias`. Bias is non-negative (0 to BASE_BIAS). Floor at MIN_GATE_THRESHOLD. This is unambiguous.

### Gate Bias Location (Node 3)

LP SRAM is wrong. The ISR and the HP core are the same core. LP SRAM access adds AHB bus latency and contention with the LP core. HP BSS is direct, cached, and the ISR already reads other BSS variables (`gate_threshold`, `trix_enabled`, `cfc.hidden`).

**Resolution:** `volatile int8_t gie_gate_bias[4]` in `gie_engine.c`. Declared `extern` in `gie_engine.h`. Written by test harness. Read by ISR. Zero bus contention.

### Sim Recalibration (Nodes 2 and 5)

The sim needs two parameter changes:
- `LP_SIM_THRESHOLD`: reduce from 2 to 0 or 1 to match hardware's ~15/16 firing rate
- `BLEND_ALPHA`: increase from 0.2 to ~0.6 to match hardware's ~10/16 blend rate

After recalibration, the sim will show whether the agreement-weighted gate bias produces a measurable lp_delta under realistic dynamics. This takes 5 minutes to run. It should be done before writing firmware.

If the effect vanishes: try BASE_BIAS = 30, then 45. If still nothing at 45 (50% threshold reduction), the LP Hamming metric genuinely cannot detect the effect at hardware-realistic firing rates, and we need a different metric (gate firing rate per group, GIE hidden energy per group, or VDB retrieval pattern shift).

If the effect survives at any BASE_BIAS: proceed to firmware with that value.

---

## Hidden Assumptions, Challenged

**Assumption: The ISR reads `gie_gate_bias[group]` atomically.**

`gie_gate_bias` is `int8_t[4]`. The test harness writes it via `memcpy(gie_gate_bias, staging, 4)` or direct byte writes. The ISR reads one byte at a time (`gie_gate_bias[group]`). On RISC-V, a byte read is atomic (single `lb` instruction). A 4-byte `memcpy` from the HP core is 4 separate byte stores. The ISR might read a partially-updated array — e.g., group 0 has the new value but group 1 still has the old value.

Is this a problem? The bias values change slowly (at classification rate, ~4 Hz). The ISR runs at 430 Hz. A torn read means one loop iteration uses old bias for some groups and new bias for others. The next loop iteration (2.3ms later) sees the fully updated array. The effect of one torn loop: one neuron group has a slightly wrong effective threshold for one loop. The dot product from that loop is at most ±1 different. The CfC blend produces at most one different trit value. This is below noise. **Not a problem.**

**Assumption: The agreement computation is correct when `lp_running_sum` is all zeros.**

At cold start, `lp_running_sum[p][j] = 0` for all p, j. `tsign(0) = 0`. `tmul(lp_now[j], 0) = 0` for all j. `dot = 0`. `agreement = 0`. `gate_bias = BASE_BIAS * max(0, 0) = 0`. The cold-start guard (`lp_sample_count[p_hat] >= MIN_BIAS_SAMPLES`) is redundant with the mathematical behavior — agreement is naturally zero when the accumulator is empty. But the guard is still worth keeping as a semantic clarification and as protection against numerical edge cases.

**Assumption: Board B's natural cycling is sufficient for TEST 14C.**

The sender cycles P0(2s) → P1(13s) → P2(10s) → P3(2s) → repeat. In a 120s run, the system sees ~4.4 complete cycles. Each cycle has 4 pattern transitions. The LP prior never fully commits to a single pattern because no pattern persists for more than 13 seconds.

This is different from the sim's TEST 14C design, which holds P1 for 9000 steps (90s) before switching. The sim builds a fully committed P1 prior and then measures the transition. With natural cycling, the prior is never fully committed — it's always in partial transition.

This is actually a *harder* test for the mechanism. If agreement-weighted gate bias can produce measurable lp_delta even under continuous cycling (where the prior never fully commits), that's a stronger result than the sim's controlled switch. The paper can present both: sim result with controlled switch, hardware result with natural cycling.

If the hardware result is null under natural cycling, a dedicated sender mode (hold P1 for 120s, then switch) becomes necessary. That's a ~10-line sender firmware change. But try the natural cycling first.

---

## The Structure Beneath the Content

The implementation has three layers, each with a different risk profile:

**Layer 1: Engine mechanism (ISR + BSS variable).** Lowest risk. 5 lines of ISR code, one new volatile array. The mechanism is testable by writing `gie_gate_bias = {15, 0, 0, 0}` manually and observing whether group 0 neurons fire at a different rate. No agreement computation needed for this test. This should be implemented and unit-tested first.

**Layer 2: Agreement computation (HP-side policy).** Medium risk. Uses existing data structures. The computation is simple (16-element dot product + decay + clamp). The risk is calibration — BASE_BIAS too small → no effect; too large → saturation. This can be tuned by observing gate_bias values in the serial output.

**Layer 3: Measurement and interpretation (TEST 14 conditions).** Highest risk. The question is whether the mechanism produces a measurable difference in LP divergence. The sim predicts yes (at its calibration). Hardware may disagree. If LP Hamming is insensitive, alternative metrics are needed.

The implementation order should follow the risk gradient: mechanism first, then policy, then measurement. Each layer can be validated independently before the next is added.

---

## What Simplicity Looks Like

The minimum viable Phase 5:

1. Add `volatile int8_t gie_gate_bias[4] = {0}` to `gie_engine.c`, `extern` in `gie_engine.h`
2. Modify `isr_loop_boundary()`: 5 lines in the blend step
3. In TEST 14 test function: ~30 lines for agreement computation, bias writing, condition dispatch
4. Three conditions (14A/14C/14C-iso) in one function, ~100 lines total including logging

Total new code: ~135 lines. No new files. No new data structures beyond `gie_gate_bias[4]`. No changes to the LP core assembly. No changes to VDB. No changes to CMD 5.

The implementation is complete when `RESULTS: 14 / 14 PASSED` prints on serial.

---

## Remaining Questions After Reflection

1. **What is the right BASE_BIAS for hardware?** Start at 15. If LP Hamming doesn't distinguish 14C from 14A, try 30. If 30 shows instability (GIE energy > 55/64), back off to 20. The parameter sweep is at most 3 runs × 150s = 7.5 minutes.

2. **Should the decay happen per CMD 5 dispatch or per confirmed classification?** Per confirmed classification (i.e., per iteration of the classification loop). This means patterns that aren't being observed have their bias decayed every time *any* pattern is confirmed. At 4 Hz overall confirmation rate, a pattern's bias halves in ~7 confirmations (1.75s). This is appropriate for a 27-second pattern cycle.

3. **What metric will the paper use if LP Hamming is insensitive?** Per-group gate firing rate. If group 0 has bias=15 and groups 1-3 have bias=0, group 0 should show measurably higher gate_fires. This is directly observable in the existing `gate_fires_total` and `gate_steps_total` counters — but those are global, not per-group. Adding per-group fire counters is ~4 lines in the ISR. Worth doing preemptively.
