# Nodes: TEST 14C Open Risks

*Phase 2. March 23, 2026.*
*Observer: Claude Sonnet 4.6*

---

## Node 1: Single Root Cause

The three open items — BLEND_ALPHA unknown, noise floor too small, HOLD-dominated null result
— are not three independent problems. They are three symptoms of one unknown: **LP dynamics
in the hardware's actual operating regime.**

Everything downstream of that unknown is speculation calibrated on wrong priors. The simulation
used LP_SIM_THRESHOLD=2 to make LP fire visibly. Hardware uses GATE_THRESHOLD=90 with settled
weights, which was described as HOLD-dominated. These are different regimes. The simulation
results are internally consistent but may not predict hardware behavior.

Why it matters: all three open items close when LP dynamics are characterized. Until then,
no amount of additional simulation closes them — we'd just be parameterizing the unknown
differently.

---

## Node 2: Two Distinct LP Update Paths

LP state evolves through two structurally separate paths:

**Path A — CfC firing:** LP neuron computes trit_dot(weights, gie_state), fires if |dot| >=
threshold. Produces lp_hidden update. Feeds lp_running_sum. Rate controlled by gate_threshold.
In HOLD-dominated regime: fires rarely. Rate → 0 as threshold → ∞.

**Path B — VDB blend:** At each LP step, VDB is queried with current snapshot. Nearest-neighbor
node's LP portion is blended into lp_hidden. Rate controlled by BLEND_ALPHA (or equivalent).
**Independent of CfC firing rate.** Operates even when CfC is in full HOLD.

The simulation in v2 models both paths. But the dominant path depends on the regime:
- If CfC fires frequently: Path A dominates, LP_SIM_THRESHOLD is the key parameter.
- If HOLD-dominated: Path B dominates, BLEND_ALPHA is the key parameter.

The simulation calibrated LP_SIM_THRESHOLD=2 (Path A dominant), but hardware may be in the
Path B dominant regime.

Tension: if Path B dominates on hardware, BLEND_ALPHA is more important than LP_SIM_THRESHOLD,
and BLEND_ALPHA is the parameter with no firmware grounding.

---

## Node 3: Gate Bias Operates on Both Paths

Gate_bias affects gie_state (Path B input quality), not CfC firing rate (Path A directly).
But gie_state feeds both paths:

- Path A: lp_cfc_step uses gie_state as input → more active gie_state → larger dots → more
  firing. Gate_bias helps Path A indirectly.
- Path B: VDB snapshot includes gie_state → better gie_state → more distinctive VDB snapshots →
  VDB nearest-neighbor is more accurately P2-representative → better blend. Gate_bias helps
  Path B directly.

The mechanism survives regime change. Gate_bias is beneficial in BOTH regimes. What changes
is the magnitude of the effect: in Path B dominant regime, effect is measured through VDB
recall quality (larger Hamming distance from P1 nodes, faster P2 node insertion), not through
lp_running_sum growth rate.

The metric (LP alignment score from lp_running_sum) may be the wrong metric in a Path B
dominant regime.

---

## Node 4: The Metric Mismatch

`lp_delta = LP_align_P2 − LP_align_P1` is computed from `lp_running_sum`, which is the
cumulative LP hidden state history. This metric is appropriate when:
- LP fires frequently (lp_running_sum accumulates quickly)
- The quantity of LP observations per step is meaningful

In a HOLD-dominated regime where LP evolves primarily via VDB blend:
- lp_running_sum accumulates slowly (few CfC firings)
- lp_hidden tracks current VDB recall quality, not cumulative history
- The CURRENT lp_hidden (raw, not cumulative) is the meaningful signal
- LP alignment should be measured as `trit_dot(lp_hidden_now, P2_template)` not via
  lp_running_sum

This is the delta at the Laundry Method bucket boundary: the metric is a symptom of the
regime assumption, not a regime-independent measure.

If Path B dominates: the right metric is VDB P2 recall rate (what fraction of VDB queries
return a P2 node) or raw lp_hidden P2 alignment (how P2-representative is lp_hidden RIGHT NOW).

---

## Node 5: The BLEND_ALPHA Sensitivity Question

BLEND_ALPHA is the probability per neuron per step that LP adopts the VDB match's value.
Range [0, 1]. No firmware grounding. The simulation used 0.2.

At BLEND_ALPHA=0.0: VDB blend does nothing. LP evolves only via CfC firing.
At BLEND_ALPHA=1.0: LP snaps to VDB match every step. Convergence in ~1 step.
At BLEND_ALPHA=0.2: 20% adoption per step. Half-life ~3 steps.
At BLEND_ALPHA=0.01: 1% adoption per step. Half-life ~70 steps.

The effect size of gate_bias (Claim 3) scales with BLEND_ALPHA:
- High BLEND_ALPHA: large effect, fast convergence, gate_bias advantage visible in few steps
- Low BLEND_ALPHA: small effect, slow convergence, gate_bias advantage buried in noise

The +0.019 delta at step 30 was computed at BLEND_ALPHA=0.2. At 0.01, this would be ~0.001.
Unmeasurable. At 0.5, it would be ~0.05. Marginally above trit_dot resolution.

The firmware value of BLEND_ALPHA (or the mechanism that determines it) is therefore the
most critical unknown for determining whether Claim 3 is measurable on hardware.

---

## Node 6: The Noise Floor Problem Reframes

At LP_SIM_THRESHOLD=2, BLEND_ALPHA=0.2: delta = +0.019 at step 30. Smaller than trit_dot
resolution (0.0625). Measurement requires many trials.

But: if hardware LP regime is Path B dominant with large BLEND_ALPHA, the actual hardware
delta may be much larger than +0.019. The simulation underestimates the effect in this regime
because it doesn't correctly weight the VDB blend path (it modeled both paths, but the wrong
CfC threshold means Path A is too active, reducing the relative contribution of Path B).

Reframing: the noise floor problem may be an artifact of the wrong simulation calibration,
not an intrinsic property of the mechanism. Characterize LP dynamics on hardware before
concluding the effect is unmeasurable.

---

## Node 7: The Hardware Measurement Protocol

Before TEST 14C firmware, one targeted measurement answers all three open items simultaneously:

**LP characterization test:**
1. Hold a single known pattern for 30s (3000 LP steps)
2. Log: LP hidden state at each step, VDB nearest-neighbor distance, gate_bias values
3. Measure: LP CfC firing rate (Path A) and LP state change rate per step (both paths combined)
4. Observe: does LP state drift toward VDB recall? How fast?

From this measurement:
- LP CfC firing rate → whether HOLD-dominated or not
- LP state change rate − CfC contribution → implied BLEND_ALPHA (or equivalent blend rate)
- VDB recall quality → whether VDB nearest-neighbor is P2-representative after P1→P2 switch

This requires adding LP state logging to firmware — which is a non-trivial change but
necessary for the paper anyway (UART falsification is blocking all submissions).

---

## Node 8: The Minimum Viable Hardware Test

If LP characterization reveals HOLD-dominated with Path B dominant and large implied
BLEND_ALPHA: proceed to TEST 14C as designed. Claim 3 effect size will be larger than
simulation predicts. Use lp_hidden raw alignment (not lp_running_sum) as primary metric.

If LP characterization reveals HOLD-dominated with Path B dominant and small implied
BLEND_ALPHA: adjust. Either increase BLEND_ALPHA equivalent in firmware, or lower
BASE_GATE_BIAS threshold, or reframe the paper to focus on Claims 1 and 2 (which are
robust regardless of LP firing rate).

If LP characterization reveals active LP firing (not HOLD-dominated): simulation is correctly
calibrated. lp_running_sum is the right metric. Proceed with current parameters. Need ~50+
repeated switch trials for statistical detection of +0.019 delta.

---

## Node 9: The Falsification Sequence

Three blockers remain before TEST 14C hardware:
1. UART falsification (blocking all paper submissions)
2. Firmware refactor (core vs. test layer separation)
3. LP characterization test (informing TEST 14C parameters)

These are ordered by dependency:
- UART falsification can happen in parallel with firmware refactor
- LP characterization requires firmware refactor (need clean test layer to add logging)
- TEST 14C firmware requires LP characterization results (to know what to measure)

The LP characterization test is not an additional overhead — it is required for the paper
claims to be defensible. Without knowing the LP dynamics, Claim 3 cannot be properly
scoped in the paper.

---

## Node 10: The Claim Hierarchy

Reviewing what each claim requires from hardware:

**Claim 1 (structural guarantee):** Requires any hardware run with a pattern switch. One run.
No LP measurement needed. Not sensitive to HOLD regime, BLEND_ALPHA, or LP dynamics.

**Claim 2 (stale prior extinguishes):** Requires gate_bias logging in firmware. One or a few
runs. Robust to LP dynamics — depends only on agreement computation math, which is
well-specified.

**Claim 3 (LP quality):** Requires LP state logging, repeated switch trials, and calibrated
knowledge of which metric (lp_running_sum vs lp_hidden_raw) reflects the dominant update path.
Sensitive to HOLD regime, BLEND_ALPHA, LP firing rate. Most work. Most risk.

Claims 1 and 2 are publishable without Claim 3. Claim 3 strengthens the paper but it is not
the paper. The paper's core is the CLS architecture instantiation (Claims 1 and 2 are
sufficient). Claim 3 is the "kinetic attention improves temporal context quality" result,
which is the Phase 5 paper's headline — it should be solid or not claimed.

---

## Tensions

| Tension | Nodes |
|---------|-------|
| Simulation calibrated to wrong regime | 2 vs 5 |
| Metric doesn't match dominant update path | 3 vs 4 |
| Effect size appears small but may be measurement artifact | 6 vs 5 |
| Claims 1/2 robust, Claim 3 risky | 10 vs (paper coherence) |
| LP characterization adds to firmware scope | 7 vs 9 |
