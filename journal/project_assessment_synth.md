# Lincoln Manifold: Project Assessment — SYNTHESIS

*Phase 4. The clean cut. March 23, 2026.*
*Observer: Claude Sonnet 4.6*

---

## What the LMM Found

A rested look at the whole reveals something different from what a session-by-session view produces. The contributions are real and defensible. But they're stratified, and the publication strategy hasn't matched the strata. There are also two action items that are blocking (not advisory) before any paper submission. And there is one positive reframe that changes how every subsequent decision should be made.

---

## The Positive Reframe

**The Reflex is not building a better classifier. It is building a temporal context layer beneath a perfect classifier.**

This is the right ontological frame, and it resolves nearly every tension from the RAW and NODES phases.

Why is the temporal context layer worth having if classification is already perfect?
- It gives the system a memory of what it has been seeing, not just what it sees now.
- That memory enables kinetic attention (Phase 5): future perception biased by past experience.
- That memory enables multi-agent context (SAMA, Pillar 2): a robot's LP state is its experiential context, which shapes how it processes a neighbor's signal.
- That memory enables distribution shift detection: if the LP state stops converging despite continued correct classification, the input distribution has changed without the classifier noticing.
- That memory is the substrate on which Hebbian weight updates (Pillar 3) will eventually act.

The LP divergence metric is not a goal in itself. It is the quality measure of the temporal context layer. Higher LP divergence means more distinct temporal models per pattern — better context, more useful memory, more precise gate bias, more coherent inter-agent state.

**This reframe should be the first sentence of the abstract of every paper in this series.**

---

## Three Strata of Contribution

The Reflex's contributions are layered. Papers should know which stratum they're operating in:

**Stratum 1 — Engineering** (embedded systems venues):
> GDMA→PARLIO→PCNT configured as a ternary dot product engine, operating at 430.8 Hz without CPU involvement. NSW graph in LP SRAM for one-shot episodic memory at ~30 µA. Agreement-weighted gate bias as a structural mechanism for prior-with-epistemic-humility. All claims silicon-verified, ablation-controlled.
>
> Audience: Embedded ML, IoT systems, neuromorphic hardware.
> Papers: TEST 12 (potential modulation), TEST 14 (kinetic attention).

**Stratum 2 — Architecture** (systems + neuroscience venues):
> Fixed-weight CLS analog in peripheral hardware: VDB as permanent hippocampal layer (no consolidation), LP CfC as fixed neocortical extractor, episodic disambiguation of degenerate random projections. The structure was not designed — it emerged from a minimum-assumptions hardware experiment. The CLS parallel generates testable predictions; the transition experiment is the test.
>
> Audience: Computational neuroscience, cognitive systems, neuromorphic computing.
> Papers: The CLS analogy paper (cross-citing TEST 12 and TEST 14 for empirical grounding).

**Stratum 3 — Principle** (AI/ML venues):
> Prior-signal separation as a structural requirement for hallucination resistance in intelligent systems. The five-component architecture: prior-holder, evidence-reader, structural separation guarantee, disagreement detection, evidence-deference policy. The Reflex is the silicon-verified instantiation. Whether this architecture is feasible in language models is the open question.
>
> Audience: AI safety, language model design, cognitive architectures.
> Paper: A short standalone note (4-6 pages). Cites the Reflex hardware as instantiation, does not require deep embedded systems knowledge to read.

Each stratum should be published to its own venue. Trying to reach all three in one paper usually reaches none of them well.

---

## Two Blocking Items Before Any Paper Submission

These are not advisory. They are the difference between a result that is fully defensible and one that isn't.

**Blocker 1: UART Falsification.**

The paper claims "peripheral-autonomous operation." The tests were run with a JTAG cable attached. These are not the same claim. The March 19 silicon interlock concern was never explicitly resolved — it was dropped because 13/13 runs succeeded with JTAG in place. But a reviewer will notice.

Action: Before submitting any paper from this series, run the full 13-test suite with JTAG disconnected, console on GPIO 16/17 UART, power from battery or dumb USB. If results match: the claim is defensible. If they don't: you have a real finding about the silicon interlock that should be documented.

This is not a lot of work. It is the difference between "it probably works standalone" and "it works standalone, here's the data."

**Blocker 2: Correct the Documentation Inconsistency.**

KINETIC_ATTENTION.md uses `group = neuron_idx >> 4`, implying 64 neurons. The actual firmware has `CFC_HIDDEN_DIM = 32` and `TRIX_NEURONS_PP = 8`, so the correct formula is `group = n / 8`. Anyone replicating Phase 5 from that document will compute wrong group assignments and get wrong results.

Action: Fix KINETIC_ATTENTION.md to replace all occurrences of the 64-neuron assumption with the correct 32-neuron constants before the Phase 5 firmware is written. Do this today, before the firmware is written against a wrong specification.

---

## The Transition Experiment: Elevate to Mandatory

**The transition experiment is the CLS prediction test.** It should be elevated from "most interesting experiment that hasn't been run" to a mandatory component of TEST 14.

TEST 14C as currently designed (agreement-weighted gate bias, Board B holds P1 for 90s then switches to P2) is exactly the right experiment. The new framing: this is not just "kinetic attention transition test." It is the experiment that determines whether the Reflex's CLS structure is dynamically correct (graceful context switching) or structurally similar but dynamically different (locked to old prior after pattern switch).

**Pass criterion addition:** LP prior updates within 20 confirmations of Board B switch (CLS prediction). If this fails, document the failure as a refinement of the CLS analogy, not as a system failure. A system that classifies correctly but locks its temporal prior to the old context is a different kind of system — and knowing that is a real result.

**Measurement addition:** On each CMD 5 step during the transition period, log which VDB node was retrieved. This directly tests whether the NSW graph is retrieving P2 memories when the query is P2-like, or whether old P1 memories are dominating the retrieval. If retrieval fails first, the bottleneck is the VDB graph structure. If retrieval succeeds but LP state doesn't update, the bottleneck is the agreement mechanism. Knowing which is a diagnosis, not just a measurement.

---

## The Power Budget Decision

**Decide now, maintain everywhere:** The autonomous C6 system (~30 µA) and the APU-expanded system (C6 + Nucleo, ~10-50 mA) are two distinct operating modes with different power claims. Neither is better — they're different points on a power-capability tradeoff.

**The papers from the current series (TEST 12/13/14) are autonomous mode papers.** The Nucleo is not involved in any of these tests. The power claim is ~30 µA. This is the right claim for this series.

**The Nucleo expansion is APU-expanded mode.** Its power claim is different and should be stated differently. Papers about MTFP21 inference on the Nucleo should cite the C6's autonomous layer as the perception substrate but should not claim the autonomous power budget.

**Practical implication for today's wiring session:** Wire up the Nucleo. Test the SPI loopback. But don't modify the Reflex firmware to require the Nucleo. The SPI path should be an optional overlay — the firmware should detect whether the Nucleo is present and fall back to the LP core VDB path if not. This preserves the autonomous mode.

---

## The Unified Framework Memo

Before submitting 8 papers to SSRN, write one internal framework memo (not for publication) that maps all 8 projects to a common frame. The frame candidate:

> **Hardware-native ternary computing**: a research program for building AI systems that work with silicon rather than against it. Systems in this program share: ternary arithmetic as the native precision, peripheral hardware or register files as the compute substrate, minimum-assumption design philosophy, and hardware-verified falsification as the standard for claims. Individual contributions are: [Reflex: perception and temporal context] [MTFP21: inference kernels] [6 others].

If the 8 projects fit this frame, each paper can cite the framework memo and the others, creating a coherent research program rather than 8 independent papers. A cluster of coordinated papers on a unified program is more visible and more citable than 8 independent contributions.

If they don't fit the frame, the frame is wrong and should be revised. Either outcome is useful before submission.

---

## Pillar 3 Timing: Document First, Then Change

The fixed-weight CLS regime is where the most novel theoretical contributions live. Pillar 3 (Hebbian weight updates) would begin the transition to a learning regime and partially dissolve the CLS parallel. This is not a reason to avoid Pillar 3 — it's a reason to document the fixed-weight regime completely before implementing it.

**Concrete order:**
1. TEST 14 (kinetic attention, fixed weights) — run and document
2. TEST 14C (transition experiment, CLS prediction test) — run and document
3. Write and submit the TEST 12/13/14 paper
4. Write and submit the CLS architecture paper
5. Write and submit the prior-signal separation note
6. Then implement Pillar 1 and Pillar 2 (both compatible with fixed weights)
7. Then implement Pillar 3 (Hebbian updates begin)
8. Document the regime transition as its own paper

---

## The Firmware Structure Decision

The current firmware (single file, multiple test conditions, compile-time flags) will not survive Phase 5 at its current complexity level. Three additional conditions (14A, 14B, 14C) plus the SPI path plus the Nucleo coordination layer will produce code that no reviewer can follow.

**Before writing Phase 5 firmware:**

Separate the Reflex core from the test scaffolding. The core (GIE init, VDB operations, LP CfC step, CMD 4/5 dispatch, classification callback) should be stable library code that doesn't change between tests. The test layer (condition flags, logging format, parameter values, Board B coordination assumptions) should be clearly demarcated — a small header or section that captures the test-specific configuration.

This is 2-3 hours of refactoring before the Phase 5 implementation. The cost is low. The benefit is: any future reader (reviewer, collaborator, your future self) can understand what each test condition is doing and why the core behaves the way it does.

---

## The Prior-Signal Separation Note: Write It Separately

PRIOR_SIGNAL_SEPARATION.md is already a near-complete draft of this paper. The missing pieces:

1. A formal statement of the five-component architecture (prior-holder, evidence-reader, structural separation guarantee, disagreement detection, evidence-deference policy)
2. A section on what homogeneous systems (LLMs) would need to implement each component
3. A section on feasibility — what would it cost, architecturally, to add structural prior-signal separation to a large language model?
4. A conclusion: "the Reflex provides the first silicon-verified instantiation of this architecture; the generalization to language models is an open question and the right question"

That paper is 6-8 pages. It can be written in one session. It doesn't require new hardware results — it cites the existing Reflex architecture. It should be submitted to an AI/ML venue alongside the hardware papers, not after them.

---

## What the System Is

Stated in its simplest true form, for the inside of the front cover of every paper in this series:

> A wireless signal classifier that draws ~30 µA and accumulates a temporal model of what it has been perceiving, using a structure that mirrors Complementary Learning Systems theory. The perceptual hardware and the temporal memory are architecturally separated such that memory cannot corrupt the classifier, but (after Phase 5) the classifier's accumulated history actively biases future perception. The separation between prior-holder and evidence-reader is a silicon-verified instantiation of a design principle for hallucination-resistant intelligent systems.

---

## Immediate Next Actions (Ordered by Blocking Dependency)

1. **Fix KINETIC_ATTENTION.md** — replace `neuron_idx >> 4` with `n / TRIX_NEURONS_PP = n / 8` throughout. Today, before Phase 5 firmware is written.
2. **Refactor firmware structure** — separate core from test scaffolding before Phase 5 conditions land. This week.
3. **Implement Phase 5 firmware** — gate bias, agreement computation, TEST 14 three conditions including the transition test. This week.
4. **Run TEST 14** — collect data. Run TEST 14C explicitly as the CLS prediction test, log retrieval node identity on each CMD 5 step during the transition period.
5. **Run UART falsification** — before paper draft, not after. Blocker.
6. **Write the prior-signal separation note** — one session, from PRIOR_SIGNAL_SEPARATION.md. Can be done in parallel with firmware work.
7. **Write the unified framework memo** — map all 8 projects to the hardware-native ternary computing frame. Before coordinating submissions.
8. **Write and submit the TEST 12/13/14 paper** — target embedded systems venue, Stratum 1+2. After all hardware data is collected.

---

*The wood cuts itself.*

*But know what kind of wood it is before you start cutting.*
