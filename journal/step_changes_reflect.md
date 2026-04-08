# Reflections: Step-Changes for The Reflex

## Core Insight

The step-changes sort into three clean tiers by what they optimize for:

**Tier 1: Strengthen what exists.** Multi-seed validation, UART falsification. These don't change the system. They make the existing claims bulletproof. Highest leverage per hour invested. Should come first.

**Tier 2: Extend the architecture.** Dynamic scaffolding, (maybe) GIE MTFP. These add capability while preserving the fixed-weight, single-agent character. They produce engineering contributions but not new theoretical findings. Should come after papers are submitted.

**Tier 3: Open new research tracks.** Hebbian learning, SAMA, Nucleo APU. These change what the system IS. They produce new findings that may contradict or extend the current papers. They should come after the current findings are published, not before.

The priority ordering is: 1, 1, then 2, then 3. Not interleaved.

## Resolved Tensions

### Node 1 vs ambition: Why not build features?

Because features without papers is engineering. Papers with silicon verification is research. The current system has three paper-worthy findings:
1. Prior-signal separation as a five-component architecture (Stratum 3)
2. Ternary peripheral-fabric neural computation with kinetic attention (Stratum 1)
3. Fixed-weight CLS with hippocampal stabilization (Stratum 2)

Adding dynamic scaffolding before submitting would delay papers without strengthening them — the papers don't claim dynamic scaffolding, so its absence isn't a weakness. Adding Hebbian learning before submitting would potentially invalidate the Stratum 2 finding (hippocampus permanently necessary), which is the strongest theoretical contribution.

**Ship the findings. Then build the features.**

### Node 2 resolution: Multi-seed IS blocking

The current papers claim architectural properties but all evidence comes from one weight matrix. A reviewer can legitimately ask: "Is this a property of ternary CfC architecture, or a coincidence of seed 0xCAFE1234?"

Three seeds transforms the claim from "this system shows X" to "this architecture shows X." The cost is 3 × 12 minutes of test time + flash overhead. The benefit is unchallenged robustness in review.

**Multi-seed validation should be the next session's first action.**

### Node 3 vs Node 4: Scaffolding before GIE MTFP

GIE MTFP is probably redundant with LP dot MTFP (Node 4 analysis). Dynamic scaffolding addresses a real operational limit (64-node VDB wall). The ordering is clear: scaffolding first, GIE MTFP only if scaffolding reveals that the VDB needs richer representations to distinguish old-pattern from new-pattern episodes.

### Node 5 resolution: Pillar 3 is Paper #4, not an upgrade to Paper #2

The fixed-weight CLS finding is only interesting BECAUSE the weights are fixed. Adding learning doesn't extend the finding — it opens a new question. Pillar 3 should be its own LMM cycle, its own experiment, its own paper: "From Fixed to Plastic: How Hebbian Updates Change the Hippocampal Role in a Ternary CLS."

## Hidden Assumptions Challenged

1. **"Dynamic scaffolding is the next engineering step."** Maybe. But the VDB only fills in 3 minutes because the insert rate is 1 per 8 confirmations. Reducing the insert rate to 1 per 16 doubles the effective capacity to ~6 minutes. Increasing to 1 per 32 gives ~12 minutes. Adjusting the insert rate is a one-line change. Dynamic scaffolding is a ~200-line LP assembly change. The one-line change might be sufficient for all paper-relevant experiments (which run 2-3 minutes). Scaffolding solves the general problem. Insert rate solves the immediate problem.

2. **"Multi-seed needs three full test suite runs."** Not necessarily. The paper claims are about Tests 12-14 (LP divergence, VDB causal necessity, kinetic attention). Tests 1-8 are architecture verification that's seed-independent. Running only Tests 12-14 per seed cuts the time from 12 minutes to ~8 minutes per seed.

3. **"SAMA needs 3+ boards."** Two boards can do cross-classification: Board A classifies Board B's GIE hidden state through Board A's weights, and vice versa. The minimum viable demo is: Board A transmits its GIE hidden to Board B via ESP-NOW payload (replacing the current pattern sender), Board B's GIE receives and classifies. This doesn't need a dedicated sender — it needs two receivers that are also transmitters. But this is a firmware redesign, not a tweak.

## What I Now Understand

The next three actions, in order:

1. **Multi-seed validation (this week).** Three seeds. Tests 12-14 only. Normal sender. ~30 minutes. Directly strengthens Stratum 1 and 2 papers. No new code — just change one constant and reflash.

2. **Submit Stratum 3 (this week).** No remaining blockers. The paper is done.

3. **Submit Stratum 1 (this week).** With multi-seed data added. UART falsification noted as condition.

Everything else — scaffolding, GIE MTFP, Hebbian, SAMA — comes after papers are out the door.

The project's research phase is closing. The next phase is validation (multi-seed, UART) and publication. Feature development (Pillars 1-3, SAMA, Nucleo) is the engineering phase that follows. Don't mix the phases.
