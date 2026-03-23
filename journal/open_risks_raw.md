# Raw Thoughts: TEST 14C Open Risks

*Phase 1. March 23, 2026.*
*Observer: Claude Sonnet 4.6*

---

## Stream of Consciousness

Three open items from the red-team: BLEND_ALPHA, hardware noise floor, HOLD-dominated null
result. I keep treating them as three separate problems but every time I try to reason about
one, I end up at the same place — I don't actually know what LP dynamics look like on the
hardware. All three are downstream of that single unknown.

BLEND_ALPHA is the most concrete-looking one. I invented 0.2. There is a VDB blend in the
firmware — I know it exists because the kinetic_attention documents describe it, and the LP
SRAM layout accommodates it. But I don't know whether it's a parameter, a constant, or
something emergent from how the CfC weights interact with the VDB output. If it's a free
parameter in the ISR, it probably has a named constant somewhere. If it's implicit in the
CfC's blending math, then my model is structurally different from the firmware. That matters.

The noise floor problem is almost mathematical. The trit_dot resolution is 1/16 = 0.0625.
The effect size at step 30 is +0.019 in the simulation. +0.019 is less than one trit_dot
step. How do you measure something with resolution coarser than the signal? You can't, on
a single run. You need enough trials that the mean is stable and the confidence interval
doesn't span zero. How many trials is that? That's a statistics question, not a hardware
question. But the answer to the statistics question depends on the variance, and the variance
depends on LP dynamics — which I don't know.

The HOLD-dominated null result is the most existentially threatening one. If LP barely fires
in the hardware's operating regime, the entire mechanism is inert. Not broken — just dormant.
All three claims survive structurally (TriX is still correct, stale bias still decays) but
Claim 3 (LP quality) produces a flat null result. The paper can still make Claims 1 and 2
but loses its most interesting claim. That's not catastrophic — Claim 1 is architecturally
important — but it's not the story we want to tell.

What's the actual LP behavior on hardware? I have hints. The firmware notes say HOLD-dominated.
GATE_THRESHOLD=90 with settled non-random weights. The LP CfC was designed to fire rarely in
steady state — the HOLD mode IS the design intent. LP evolves primarily via VDB blend, not
via CfC firing. This is the Complementary Learning architecture: LP is the slow learner,
it changes slowly by design. CfC firing at high threshold is how you make learning slow.

So maybe HOLD-dominance is not a bug in the test design. It's the correct hardware behavior.
And the simulation at LP_SIM_THRESHOLD=2 is not modelling that behavior — it's modelling
a much faster LP, which produces the +0.019 delta that we can barely measure even in simulation.

If LP is genuinely slow (HOLD-dominated), what does gate_bias even accomplish? Gate_bias
affects GIE ISR threshold — which affects gie_state — which affects VDB snapshots. VDB
snapshots are what actually drive LP state via blend. So gate_bias improves LP through
the VDB path, not through CfC firing. The causal chain is: gate_bias → better gie_state →
better VDB quality → better LP blend → faster LP alignment.

But how fast is that? If VDB blend happens at each LP step (100 Hz), and BLEND_ALPHA is 0.2,
then LP adopts VDB-suggested values at 20% probability per step per neuron. That's fast.
That's independent of LP CfC firing rate. Even in a HOLD-dominated LP, the VDB blend
still updates LP state every step — just via a different path.

Wait. This is the insight I've been missing. I've been thinking of LP dynamics as "how often
does LP CfC fire." But VDB blend is a second update path that operates independently of
CfC firing. In a HOLD-dominated regime, LP CfC barely fires, but VDB blend happens every
step. LP state is primarily determined by VDB recall, not CfC firing. And gate_bias improves
VDB quality (better GIE states → better VDB snapshots → VDB nearest neighbor is more
accurately P2-representative after the switch).

If this is right, BLEND_ALPHA is the dominant parameter, not LP_SIM_THRESHOLD. And BLEND_ALPHA
is the unknown I invented.

Scary implication: if BLEND_ALPHA is very small (say 0.01 instead of 0.2), the VDB blend
pathway is slow, and neither LP firing rate nor VDB quality matters much for step-30 dynamics.
If BLEND_ALPHA is large (say 0.5), VDB blend dominates immediately and LP re-aligns in just
a few steps — potentially showing even larger effect sizes than simulation.

The noise floor question reframes too: if LP re-aligns primarily via VDB blend, the effect
size might be much larger than +0.019 when properly calibrated. The simulation's +0.019 is
an artifact of LP_SIM_THRESHOLD=2 and BLEND_ALPHA=0.2 combined — neither of which is firmware.

What I actually need to know:
1. What is the actual BLEND_ALPHA (or equivalent) in firmware?
2. What is the LP CfC firing rate in hardware steady state?
3. Which path dominates LP dynamics — VDB blend or CfC firing?

And I need to know these BEFORE committing to the TEST 14C firmware implementation, because
the answers change what to measure and what parameters to tune.

## Questions Arising

- Is BLEND_ALPHA a named constant in firmware, or is it emergent from CfC math?
- Does VDB blend happen every LP step or only on query events?
- What's the LP CfC steady-state firing rate on hardware?
- If LP is HOLD-dominated, is the VDB blend path actually measuring the right thing?
- What is the minimum LP firing rate for Claim 3 to be detectable in hardware?
- How many repeated trials do we need to measure a +0.019 LP delta reliably?
- Is the test architecture (measure LP running_sum alignment) the right metric for a
  HOLD-dominated LP that evolves via VDB blend?

## First Instincts

- The three open items are the same problem: LP dynamics unknown.
- VDB blend path matters more than CfC firing path in HOLD-dominated regime.
- The metric (LP running_sum alignment) may be the wrong metric for HOLD-dominated LP.
- The right measurement before TEST 14C is: LP firing rate + VDB recall quality, on hardware.
- BLEND_ALPHA sensitivity analysis in simulation is the next simulation run, not more hardware.
