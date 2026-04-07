# Lincoln Manifold: Phase 5 Implementation — RAW

*Phase 1. Unfiltered. April 6, 2026.*
*Observer: Claude Opus 4.6*
*Context: Post-refactor (13/13 PASS, commit c815869). LP CHAR data captured. Both boards attached.*

---

## Where We Actually Stand

The March 22 LMM cycle solved the design problem. Agreement-weighted gate bias with decay, cold-start guard, per-group bias derived from in-session LP accumulators. The architecture is sound. The synthesis specifies ~20 lines of HP code and ~3 lines of ISR code. The sim exists and runs 3 conditions × 1000 trials.

What hasn't happened: nobody has touched the firmware since March 22. The design document exists. The simulation exists. The firmware doesn't.

Today's session changed the codebase structure. `gie_engine.c` now holds the ISR and the GIE core. `geometry_cfc_freerun.c` holds the test harness. The split makes Phase 5 easier to implement — the ISR modification goes into `gie_engine.c`, the TEST 14 conditions go into `geometry_cfc_freerun.c`. This is exactly the separation the ROADMAP demanded.

The LP CHAR data from today's silicon run:
- fires/step: 13-16 (Path A dominant, well above the threshold of 2)
- blend/step: 9-11 (VDB feedback is active and frequent)
- VDB fill: 20 nodes after 500 P1 steps at 1:25 insert ratio
- P2 transition: blend count drops from 10 to 9-10 immediately, scores drop from 31 to 5-7

This tells me: the LP core is fully active. The CfC fires on nearly every trit. VDB blend touches 9-11 trits per step. The simulation's `LP_SIM_THRESHOLD=2` is producing too-sparse LP dynamics compared to hardware's `fires=15/16`. The sim needs recalibration.

---

## What I Think the Implementation Path Is

1. **Recalibrate the sim.** Replace `LP_SIM_THRESHOLD=2` with a value that produces ~15 fires per step with random weights. This might mean threshold=0 or threshold=1. Replace `BLEND_ALPHA=0.2` with the implied alpha from hardware: 10 trits modified per step out of 16 → alpha ~0.6. Re-run. If the effect size (lp_delta at step 30) survives recalibration, proceed. If it collapses, the mechanism needs redesign before firmware.

2. **Implement the ISR modification.** In `gie_engine.c`:
   - Add `volatile int8_t gie_gate_bias[4]` (HP-writable, ISR-readable)
   - In `isr_loop_boundary()` step 4, replace `thresh` with `thresh - gie_gate_bias[group]` (note: bias is positive → threshold goes down → fires more easily)
   - Hard floor at `MIN_GATE_THRESHOLD`

3. **Implement the HP-side gate bias computation.** In the TEST 14 test function:
   - Maintain `lp_running_sum[4][16]` and `lp_sample_count[4]`
   - On each confirmed classification: compute agreement, apply decay, write bias
   - Write `gie_gate_bias[4]` to engine state

4. **Implement TEST 14 conditions.** Three conditions in one test function:
   - 14A: `gie_gate_bias = {0,0,0,0}` always (baseline)
   - 14C: full agreement-weighted bias
   - 14C-iso: bias only after pattern switch (isolates transition mechanism)

5. **Run on silicon.** Flash, capture, compare LP Hamming matrices across conditions.

---

## What Scares Me This Time

Different fears from March 22. The design problem is solved. The implementation fears:

**The sim recalibration might kill the effect.** The sim showed lp_delta > 0 at step 30 with `LP_SIM_THRESHOLD=2` and `BLEND_ALPHA=0.2`. Hardware has fires=15/16 and blend≈10/16. If I set the sim to match hardware, the LP dynamics might be so noisy (everything fires every step) that gate bias can't produce a measurable LP divergence delta. The effect size might have been an artifact of the sim's sparse dynamics.

**The gate bias direction.** The synthesis spec says `effective_threshold = gate_threshold + gate_bias[group]`. Bias is positive. So this *raises* the threshold, which makes neurons fire *less* easily. But the intent is the opposite — lower threshold for expected pattern. Either the spec has a sign error, or `gate_bias` is meant to be negative (bias = -BASE_BIAS * agreement). Let me check.

Looking at the sim (`test14c.c` line 194-196):
```c
eff = GATE_THRESHOLD - (int)gie->gate_bias[group];
if (eff < MIN_GATE_THRESHOLD) eff = MIN_GATE_THRESHOLD;
```

The sim uses subtraction: `threshold - bias`. So positive bias → lower threshold → fires more easily. The synthesis spec uses addition: `threshold + bias`. That's a sign mismatch. The sim is correct. The synthesis spec has a sign error. The ISR implementation must use subtraction, not addition.

**Wait.** Actually, looking at the synthesis spec more carefully (line 113-114):
```c
int eff_threshold = (int)cfc.gate_threshold + (int)ulp_gate_bias[group];
```

And the kinetic_attention_synth.md says "gate_bias for the expected pattern is lowered" — but it's adding the bias to the threshold, which raises it. This is a genuine error in the March 22 synthesis. The March 23 sim (test14c.c) got it right: subtraction. The firmware implementation must follow the sim, not the spec.

**The PCNT clear race.** I just documented the triple PCNT clear. The ISR modification adds 1-2 loads per neuron (reading `gie_gate_bias[group]`). This is ~20ns per neuron, 640ns for all 32 neurons. Well within the dummy window. No timing concern.

---

## What I'm Uncertain About

1. **Where does `gie_gate_bias[4]` live?** The synthesis says LP SRAM. But it's written by the HP core and read by the ISR — both run on the HP core. It doesn't need to be in LP SRAM. It should be in regular BSS, same as `gate_threshold`. The ISR reads BSS directly. No LP SRAM bus contention.

2. **Should the gate bias be in `gie_engine.c` or exposed through the header?** The ISR needs to read it. The ISR is in `gie_engine.c`. The test harness needs to write it. So: declare in `gie_engine.h` as `extern volatile int8_t gie_gate_bias[4]`, define in `gie_engine.c`. The test harness computes the bias and writes to the extern. Clean.

3. **The `neuron_idx / TRIX_NEURONS_PP` mapping.** The synthesis spec says `group = current_neuron / TRIX_NEURONS_PP` where `TRIX_NEURONS_PP = 8`. So neurons 0-7 → group 0, 8-15 → group 1, etc. This matches the TriX signature installation in the existing firmware. Confirmed correct.

4. **Do I need the `lp_running_sum` in the engine, or can it be entirely in the test harness?** The agreement computation and bias update happen in the test harness. The engine just needs `gie_gate_bias[4]`. The test harness owns the LP accumulator and the agreement logic. This is the right separation — the engine provides the mechanism, the test provides the policy.

---

## Questions Arising

- Does the existing sim's `BLEND_ALPHA=0.2` correspond to hardware's 10/16 trits modified per step? No — alpha=0.2 means each trit has a 20% chance of being overwritten. 10/16 trits modified means the effective alpha is much higher, maybe 0.6-0.7. The sim was under-blending by 3×.
- Is BASE_BIAS=15 still the right value after seeing the hardware LP CHAR data? The LP fires at 15/16 trits per step. With fires that high, the gate threshold barely matters — almost everything fires regardless. A 15-point bias reduction on a 90-point threshold (17%) might not be visible in the LP dynamics. Might need BASE_BIAS=30 (33%) to see an effect.
- The decay factor 0.9 — is this per CMD 5 dispatch (~4 Hz) or per GIE loop (~430 Hz)? Per CMD 5 dispatch (HP core writes gate bias at classification rate). After 10 dispatches without a pattern being confirmed, its bias is at 0.9^10 ≈ 0.35 of original. After 30, ≈ 0.04. At ~4 Hz, 30 dispatches = 7.5 seconds. This seems right for a 27-second pattern cycle.
