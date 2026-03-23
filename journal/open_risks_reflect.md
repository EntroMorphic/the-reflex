# Reflections: TEST 14C Open Risks

*Phase 3. March 23, 2026.*
*Observer: Claude Sonnet 4.6*

---

## Core Insight

The three open items are not measurement problems. They are a framing problem.

The simulation was built to demonstrate a mechanism. To make the mechanism visible in
simulation, LP was tuned to fire frequently (LP_SIM_THRESHOLD=2). This produced a visible
+0.019 delta — but it produced it in the wrong regime. The hardware's LP is designed to fire
rarely. LP is the slow learner. Slow learning is not a bug to route around; it is the
design intent. A prior that updates too quickly is not a prior — it is a running average.

When the simulation was tuned to match the design intent (higher threshold, HOLD-dominated),
the delta disappeared. The naive interpretation is: the mechanism doesn't work. The correct
interpretation is: the mechanism operates through a different pathway (VDB blend, Path B) that
the simulation was not correctly modelling as dominant.

The deeper question is: **what does "LP alignment" mean in a HOLD-dominated system?**

In a Path A dominant system (LP fires frequently), LP alignment means: lp_running_sum captures
the pattern's statistics. The cumulative history is the prior. Alignment with that history is
the measure of convergence.

In a Path B dominant system (LP evolves via VDB blend), LP alignment means: lp_hidden, right
now, reflects what the VDB nearest-neighbor looked like. The prior IS the VDB recall. The
lp_running_sum is a secondary artifact — it accumulates slowly and has low statistical weight
per step.

The simulation measured the wrong thing for the hardware's actual regime. lp_delta based on
lp_running_sum is the right metric for Path A. For Path B, the right metric is VDB recall
quality: does the nearest-neighbor VDB node represent P2 or P1, and how Hamming-close is it?

---

## Resolved Tensions

**Tension: Simulation calibrated to wrong regime (Nodes 2 vs 5)**

Resolution: Not a tension — a sequencing problem. The simulation was correctly calibrated
to demonstrate the mechanism exists. It was not calibrated to predict hardware behavior in
the hardware's operating regime. These are different purposes. The simulation result says:
"in a regime where LP fires, the mechanism produces measurable LP quality improvement." The
hardware question is: "in the hardware's actual regime, does the mechanism produce measurable
improvement through the VDB path?" That question requires a different simulation setup OR a
hardware measurement. One does not substitute for the other.

**Tension: Metric doesn't match dominant update path (Nodes 3 vs 4)**

Resolution: The metric needs to change before hardware measurement. Not after. The current
metric (lp_running_sum alignment) was inherited from the Path A framing. In Path B dominant
hardware, the meaningful metric is VDB recall accuracy: what fraction of VDB queries in Phase 2
return a P2-class node vs P1-class node, and does 14C show a higher P2 fraction than 14A?

This is measurable in firmware without requiring many repeated trials. VDB recall is a binary
per-step event (P2 node retrieved or not). Over 500 Phase 2 steps, the fraction is stable with
much lower statistical variance than an LP alignment score.

**Tension: Effect size appears small but may be measurement artifact (Nodes 5 vs 6)**

Resolution: The effect size question is only answerable after LP dynamics are characterized.
The simulation showed +0.019 in a Path A dominant regime. In a Path B dominant regime, the
effect operates through VDB quality, not lp_running_sum. The "effect size" for VDB recall
quality is not comparable to the lp_running_sum delta — they're different units. Measure the
right thing rather than conclude from a measurement that doesn't fit the regime.

**Tension: Claims 1/2 robust, Claim 3 risky (Node 10)**

Resolution: This is a feature of the paper structure, not a flaw. Structure the paper so
Claims 1 and 2 carry the paper independently. Claim 3 is presented as a forward-looking result
with caveats about the LP dynamics regime. This is honest science: "we observe the structural
effect (Claim 1) and the bias decay effect (Claim 2); the LP quality improvement (Claim 3) is
predicted by simulation and requires hardware characterization for confirmation."

---

## Hidden Assumptions Challenged

**Assumption: "HOLD-dominated = mechanism inert"**

False. HOLD-dominated means CfC Path A is suppressed. Gate_bias still operates on VDB Path B.
In a HOLD-dominated regime, gate_bias improves VDB snapshot quality (more P2 neurons fire →
more distinctive P2 snapshots → VDB finds P2 nodes faster). LP state tracks VDB recall. The
mechanism is not inert — it operates through a different pathway. The simulation wasn't
measuring that pathway's effect.

**Assumption: "lp_running_sum alignment is the universal LP quality metric"**

False in Path B dominant regime. lp_running_sum is the right metric when LP firing is the
primary update mechanism. When VDB blend dominates, lp_hidden's trajectory (tracked via raw
trit_dot against a known P2 template) is more meaningful than the slow-accumulating running_sum.
The right metric depends on the dominant regime.

**Assumption: "LP characterization is extra work before the real test"**

False. LP characterization IS the real test. Without it, the paper's Claim 3 is based on
simulation of the wrong regime. LP characterization both calibrates the simulation AND provides
the hardware baseline that the paper needs. It's not overhead. It's scope that was omitted.

**Assumption: "The +0.019 delta is the minimum observable effect"**

Uncertain. In a Path B dominant regime, the gate_bias effect might be much larger (operating
through VDB quality which has faster dynamics than lp_running_sum accumulation), or it might
be near zero (if BLEND_ALPHA equivalent is small). We genuinely don't know the direction.
The +0.019 is an artifact of simulation calibration, not a physics-grounded floor.

---

## What I Now Understand

The three open items collapse into a decision tree with one root node:

```
Q: What is the dominant LP update path on hardware (Path A vs Path B)?
│
├─ Path A dominant (LP fires frequently):
│    - Simulation calibration at LP_SIM_THRESHOLD=2 is approximately correct
│    - lp_running_sum metric is appropriate
│    - Effect size ~+0.019 at step 30
│    - Need ~50+ repeated switch trials for detection
│    - BLEND_ALPHA matters but is secondary
│
└─ Path B dominant (HOLD-dominated, VDB blend primary):
     - Simulation calibration is wrong for this regime
     - lp_hidden raw alignment is the right metric
     - Effect size unknown (could be larger OR near zero depending on BLEND_ALPHA equivalent)
     - Need BLEND_ALPHA equivalent characterized from hardware
     - Right metric: VDB P2 recall fraction over Phase 2 steps
     - Need fewer repeated trials (binary per-step, stable fraction)
```

This is the Laundry Method in action. Three socks that look different, same pile. Find the
pile, sort once.

The action is: **LP characterization test before TEST 14C firmware.** Single targeted run
with LP state logging. Answers the root question. All three open items close downstream.

---

## Remaining Questions

After LP characterization:

**If Path A dominant:** How many repeated trials for statistical detection? Compute from
observed variance in LP alignment score across trials. Likely 50–200 trials for a 90%
confidence interval that excludes zero.

**If Path B dominant:** What is the implied BLEND_ALPHA (or equivalent)? Compute from the
observed LP state change rate per step minus the CfC firing contribution. With that value,
re-run simulation at correct calibration. Does the VDB-path effect size survive?

**In both cases:** Is the VDB recall quality metric (P2 fraction over Phase 2 steps) tractable
in firmware? Does it require node labeling (knowing which VDB nodes are P1-class vs P2-class)?
If so, does adding node labels to VDB change the VDB's behavior?

The firmware question — how does VDB recall work exactly? — is the deepest unresolved question.
Reading the firmware source is the answer.
