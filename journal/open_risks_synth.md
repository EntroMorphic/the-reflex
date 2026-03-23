# Synthesis: TEST 14C Open Risks

*Phase 4. The clean cut. March 23, 2026.*
*Observer: Claude Sonnet 4.6*

---

## What Emerged

Three open items. One root cause. One decision tree. One action before anything else.

The three open items — BLEND_ALPHA unknown, effect size below noise floor, HOLD-dominated null
result — are all consequences of not knowing the dominant LP update path on hardware. The
simulation was calibrated to a Path A dominant regime (frequent LP firing) to make the
mechanism visible. Hardware is designed for a Path B dominant regime (HOLD-dominated, VDB
blend primary). These are different operating points. The simulation result is real but
it may not describe the hardware.

The resolution is a single targeted hardware measurement — LP characterization — that answers
the root question and closes all three items at once. This measurement was not in the original
TEST 14C plan. It belongs there.

---

## Architecture: Two Regimes, Two Metrics, One Root Question

**Path A dominant (LP fires frequently):**

| Property | Value |
|----------|-------|
| Primary update mechanism | CfC firing: `lp_hidden = f(trit_dot(W, gie_state))` |
| Gate_bias effect path | gie_state quality → larger CfC dots → more LP firing |
| Right metric | `lp_delta = LP_align_P2 − LP_align_P1` (lp_running_sum based) |
| Effect size (sim) | +0.019 at step 30, +0.041 at step 200 |
| Detection requirement | ~50–200 repeated switch trials |
| BLEND_ALPHA sensitivity | Secondary — CfC dominates |

**Path B dominant (HOLD-dominated, VDB blend primary):**

| Property | Value |
|----------|-------|
| Primary update mechanism | VDB blend: LP adopts nearest-neighbor node's LP portion |
| Gate_bias effect path | gie_state quality → better VDB snapshots → better recall → better blend |
| Right metric | VDB P2 recall fraction: how often does VDB query return P2-class node |
| Effect size | Unknown — BLEND_ALPHA equivalent needed |
| Detection requirement | Fewer trials needed (binary per-step event, stable fraction) |
| BLEND_ALPHA sensitivity | Primary — VDB blend rate governs convergence speed |

---

## LP Characterization Test Specification

A single targeted run, before any TEST 14C firmware implementation. Requires adding LP state
logging to the firmware — which is necessary for paper defensibility regardless.

**What to log (per LP step, 100 Hz):**
- `lp_hidden[16]` — raw LP neuron state
- `vdb_hamming_dist` — Hamming distance to VDB nearest-neighbor
- `vdb_match_pattern` — which pattern class the nearest-neighbor belongs to (requires VDB
  node labeling: tag each inserted node with the p_hat at insertion time)
- `gate_bias[4]` — current gate bias values
- `lp_cfc_fired` — count of LP neurons that changed state this step via CfC computation
- `lp_blend_changed` — count of LP neurons that changed state this step via VDB blend

**Run protocol:**
- Phase 1: Hold P1 for 30s (3000 LP steps). Log continuously.
- Phase 2: Switch to P2. Log for 5s (500 LP steps).
- No gate bias (14A condition). Baseline characterization only.
- Repeat 3× to check consistency.

**What to compute from the log:**

1. **Path A firing rate:** mean `lp_cfc_fired` per step during Phase 1 steady state.
   - > 2 neurons/step: Path A active, lp_running_sum metric is valid
   - < 1 neuron/step: Path A suppressed, lp_running_sum accumulates too slowly

2. **Path B blend rate:** mean `lp_blend_changed` per step. Divide by LP_HIDDEN_DIM to
   estimate implied BLEND_ALPHA.
   - This is the firmware-grounded value to replace the invented 0.2 in simulation.

3. **VDB recall quality at switch:** in Phase 2 steps 1–15, what fraction of VDB queries
   return a P2-tagged node? This is the right observable for Claim 3 in Path B dominant regime.
   - > 50% P2 recall within 15 steps: Path B convergence is fast
   - < 10% P2 recall at step 15: VDB blend is slow or P2 nodes aren't distinctive enough

4. **LP state change profile:** plot `lp_hidden` component trajectories over Phase 2.
   - Sharp step change within 5 steps: Path B dominant with high BLEND_ALPHA
   - Gradual drift over 50+ steps: Path A dominant or low BLEND_ALPHA

---

## Decision Tree After Characterization

```
After LP characterization run:

IF Path A dominant (CfC firing > 2/step):
    - lp_running_sum metric is valid
    - Re-run simulation with correct LP_SIM_THRESHOLD (set to match observed firing rate)
    - Effect size at step 30 should match simulation prediction
    - TEST 14C firmware: measure lp_running_sum alignment delta
    - Need ~50–200 repeated switch trials for Claim 3 detection
    - Paper: all three claims, Claim 3 with statistical uncertainty

IF Path B dominant (CfC firing < 1/step) AND BLEND_ALPHA > 0.1:
    - VDB recall quality is the right metric
    - Re-run simulation with BLEND_ALPHA = implied_value, LP_SIM_THRESHOLD >> CfC_range
    - Effect size likely larger than +0.019 (fast VDB blend path)
    - TEST 14C firmware: measure VDB P2 recall fraction
    - Need fewer repeated trials (binary event, stable fraction)
    - Paper: Claims 1 and 2 primary; Claim 3 as VDB quality result (not lp_running_sum)
    - Reframe: "kinetic attention improves VDB recall quality" not "LP alignment score"

IF Path B dominant (CfC firing < 1/step) AND BLEND_ALPHA < 0.05:
    - LP dynamics too slow for step-30 measurement
    - Re-calibrate: adjust BLEND_ALPHA equivalent in firmware, or extend measurement window
    - Alternative: measure at step 200+ (simulation shows +0.041 at step 200)
    - Paper: Claims 1 and 2 primary; Claim 3 deferred to extended test
    - Do not inflate: report null result at step 30 if that's what hardware shows
```

---

## Claim Hierarchy for the Paper

Claim 1 and Claim 2 carry the paper independently of LP regime. They should be the primary
claims. Claim 3 is the Phase 5 headline but requires hardware calibration to scope correctly.

**Claim 1 (W_f hidden=0 structural guarantee):**
- Hardware evidence: any single run with pattern switch
- Statement: "TriX classification is correct from the first packet after switch, regardless
  of LP prior state, in all tested conditions."
- Paper position: central. This is the architecture paper's core claim.

**Claim 2 (stale prior self-extinguishes):**
- Hardware evidence: gate_bias log from single 14C run
- Statement: "gate_bias[P1] decays to ~50% of its end-of-phase-1 value within T14_MIN_SAMPLES
  steps because the agreement mechanism stops refreshing a pattern whose TriX classification
  has changed."
- Paper position: supporting. Demonstrates the mechanism is active and correctly targeted.

**Claim 3 (LP quality improves):**
- Hardware evidence: LP characterization run + regime-appropriate metric
- Statement (Path A): "LP alignment score at t+30s is higher in 14C than 14A across N trials."
- Statement (Path B): "VDB P2 recall fraction within T14_MIN_SAMPLES steps is higher in 14C
  than 14A."
- Paper position: forward result. Present with correct scoping and regime identification.
  If effect is small: present with honest confidence intervals. Do not inflate.

---

## Immediate Actions (Ordered)

| # | Action | Unblocks |
|---|--------|---------|
| 1 | Read firmware source: locate VDB blend mechanism and BLEND_ALPHA equivalent | Sim recalibration, LP char spec |
| 2 | Add LP state logging to firmware (lp_hidden, vdb_dist, vdb_match_pattern, fire counts) | LP characterization run |
| 3 | Run LP characterization test (30s P1 + 5s P2, 3×, no gate bias) | Decision tree |
| 4 | Determine dominant path from characterization data | TEST 14C parameter choice |
| 5 | Recalibrate simulation with firmware-grounded BLEND_ALPHA | Validated simulation |
| 6 | Re-run TEST 14C simulation with correct calibration | Hardware prediction |
| 7 | Implement TEST 14C firmware with regime-appropriate metric | Hardware test |
| 8 | Run hardware TEST 14C (50+ repeated switch trials if Path A; 3–5 if Path B) | Claim 3 |

**Parallel:** UART falsification can proceed immediately alongside Steps 1–3. It does not
depend on LP characterization.

---

## The Clean Statement

The mechanism is not broken. The mechanism is real and confirmed in two independent simulation
regimes (14C and 14C-iso). The open items are a calibration gap between simulation and hardware,
plus a metric that was designed for the wrong regime.

Closing the gap requires one targeted measurement before the main test. That measurement is
not overhead — it is the foundation that makes the main test's claims defensible.

**Before TEST 14C firmware:** read the VDB blend source, add LP logging, run characterization.

After that: the wood cuts itself.

---

*Date: March 23, 2026*
*Subject: TEST 14C open risks (BLEND_ALPHA, noise floor, HOLD-dominated null result)*
*Method: Lincoln Manifold Method (4 phases)*
*Depends on: `docs/LCACHE_TEST14C_SIM_RESULTS.md`, `docs/KINETIC_ATTENTION.md`,
  `journal/kinetic_attention_synth.md`, firmware source*
*Output files: `open_risks_raw.md`, `open_risks_nodes.md`, `open_risks_reflect.md` (this file)*
