# Session: March 23, 2026

**The Reflex Project — Session Record**

*Observer: Claude Sonnet 4.6*

---

## Context

The day after the March 22 session (TEST 12/13/14 design, LMM on kinetic attention, hardware topology documentation). Wiring session planned for this afternoon. Before picking up the iron, the user requested a full Lincoln Manifold Method cycle on the project's ontological, practical, and engineering dimensions — a rested assessment of what has been built and where it stands.

---

## Section 1: The Lincoln Manifold Cycle

The March 23 LMM ran four phases on the whole project, not a specific design problem. Source files:

- `journal/project_assessment_raw.md` — Phase 1: unfiltered
- `journal/project_assessment_nodes.md` — Phase 2: 10 nodes with tension table
- `journal/project_assessment_reflect.md` — Phase 3: resolved tensions, hidden assumptions challenged
- `journal/project_assessment_synth.md` — Phase 4: clean synthesis, immediate actions

What follows is the distillation. The full documents contain the reasoning.

---

## Section 2: The Core Reframe

**The Reflex is not building a better classifier. It is building a temporal context layer beneath a perfect classifier.**

This sentence is the center of the LMM output. It should open every paper in the series.

What it means:
- The GIE/TriX classification is at 100% accuracy. This cannot be improved by the temporal layer and it will not be. The temporal layer is not trying to improve it.
- The temporal layer — LP CfC, VDB, kinetic attention — is building a model of *what the system has been experiencing over time*. Not a snapshot of the present. A history.
- That history is the substrate for everything downstream: kinetic attention (Phase 5), multi-agent context (SAMA, Pillar 2), distribution shift detection, Hebbian weight updates (Pillar 3).

Why this matters for papers:
- LP divergence is the quality metric for the temporal context layer. Higher divergence means more distinct temporal models per pattern — better context, more useful memory, more precise gate bias.
- LP divergence is not a goal in itself. It earns its meaning from downstream use.
- "Kinetic attention improves temporal context quality" is the right claim. "Kinetic attention improves classification" is wrong and will be caught in review.

---

## Section 3: Three Strata of Contribution

The LMM identified three distinct layers of contribution with different audiences and different venues.

### Stratum 1 — Engineering

**Contribution:** GDMA→PARLIO→PCNT configured as a ternary dot product engine at 430.8 Hz without CPU involvement. NSW graph in LP SRAM for one-shot episodic memory at ~30 µA. Agreement-weighted gate bias as a structural mechanism for prior-with-epistemic-humility. All claims silicon-verified, ablation-controlled.

**Audience:** Embedded ML, IoT systems, neuromorphic hardware.

**Papers:** TEST 12/13 (potential modulation), TEST 14 (kinetic attention).

**Venue:** Embedded systems / IoT conferences. SSRN preprint.

### Stratum 2 — Architecture

**Contribution:** Fixed-weight Complementary Learning Systems analog in peripheral hardware. VDB as permanent hippocampal layer (no consolidation path). LP CfC as fixed neocortical extractor. Episodic disambiguation of degenerate random projections. The structure was not designed — it emerged from a minimum-assumptions experiment. The CLS parallel generates testable predictions; the transition experiment (TEST 14C) is the primary test.

**Audience:** Computational neuroscience, cognitive systems, neuromorphic computing.

**Papers:** A CLS architecture paper cross-citing the hardware results. Can be written after TEST 14C data is collected.

**Venue:** Computational neuroscience journals. CogSci. SSRN preprint alongside the hardware papers.

### Stratum 3 — Principle

**Contribution:** Prior-signal separation as a structural requirement for hallucination resistance in intelligent systems. The five-component architecture: (1) prior-holder, (2) evidence-reader, (3) structural separation guarantee, (4) disagreement detection, (5) evidence-deference policy. The Reflex provides the only silicon-verified complete instantiation of this architecture. Whether it transfers to language models is an open question and the right question.

**Audience:** AI safety, language model design, cognitive architectures.

**Paper:** A short standalone note (6-8 pages). PRIOR_SIGNAL_SEPARATION.md is the current draft — nearly complete. One session to finish it.

**Venue:** AI/ML workshops (NeurIPS, ICLR). SSRN preprint.

**Note:** Each stratum targets a different community and should be submitted to its own venue. Trying to reach all three in a single paper usually reaches none of them well.

---

## Section 4: Two Blocking Items

These are not advisory. They are the difference between fully defensible results and results that a careful reviewer can question.

### Blocker 1: Fix KINETIC_ATTENTION.md (Completed This Session)

**Error:** KINETIC_ATTENTION.md and `journal/kinetic_attention_synth.md` used `group = neuron_idx >> 4` (division by 16, implying 64 neurons). The actual firmware constants are `CFC_HIDDEN_DIM = 32` and `TRIX_NEURONS_PP = 8`, so the correct formula is `group = neuron_idx / TRIX_NEURONS_PP` (division by 8, groups of 8 neurons each):
- P0: neurons 0–7
- P1: neurons 8–15
- P2: neurons 16–23
- P3: neurons 24–31

**Status: Fixed this session.** Both documents corrected. Anyone writing Phase 5 firmware from these documents will now get the right group assignments.

### Blocker 2: UART Falsification (Pending — Paper Blocker)

**The claim:** "Peripheral-autonomous operation."

**The gap:** Every test was run with a JTAG cable attached. The March 19 "silicon interlock" concern (USB-JTAG controller gating PCNT behavior) was not explicitly resolved — it was deprioritized after 13/13 runs succeeded with JTAG in place. Success with JTAG is not the same as "peripheral-autonomous."

**Action required:** Before submitting any paper:
1. Re-route console to GPIO 16/17 UART.
2. Power the board from battery or dumb USB charger (no development computer).
3. Monitor via a secondary serial bridge.
4. Run the full 13-test suite.
5. If all PASS: the paper claim is fully defensible. Document as "validated without JTAG."
6. If any FAIL: document the failure mode and investigate the silicon interlock explicitly.

**Status: Pending. Paper blocker. Must complete before submission of any paper in the series.**

---

## Section 5: The Power Budget Decision

The original Reflex claims ~30 µA for adaptive pattern recognition. The Nucleo M4 expansion (being wired today) draws 10-50 mA — an order of magnitude departure. These cannot coexist in the same power claim in the same paper.

**Resolution:** Two named operating modes, maintained explicitly throughout all documentation:

### Autonomous Mode (~30 µA)
- C6 only: GIE + VDB + LP CfC + kinetic attention (gate bias via ISR)
- No Nucleo, no SPI, no QSPI
- This is the operating mode for all papers in the TEST 12/13/14 series
- The power claim (~30 µA) applies only to this mode

### APU-Expanded Mode (~10-50 mA)
- C6 + Nucleo APU: adds VDB search acceleration via SPI, MTFP21 inference via QSPI
- Power budget is dominated by the Nucleo, not the C6
- This mode is for the MTFP21/L-Cache papers and future SAMA work
- The C6 autonomous layer still operates at its own power budget; the Nucleo adds additional draw

**Practical implication:** The Reflex firmware must detect whether the Nucleo is present (SPI handshake at startup) and fall back gracefully to the autonomous VDB path if absent. Autonomous mode must continue to work when the Nucleo is unplugged. This is not optional — it's what allows the power claims to be maintained independently.

---

## Section 6: The Transition Experiment — Elevated to Mandatory

TEST 14C as designed (agreement-weighted gate bias, Board B holds P1 for 90s then switches to P2) is not just a kinetic attention test. It is the primary test of the CLS analogy.

**What CLS theory predicts:** When the input pattern changes, the hippocampal system (VDB) should supply new-context episodes that gradually displace the old-context prior in the slow learner (LP CfC). In the Reflex: after Board B switches from P1 to P2, the VDB should retrieve P2 memories (because the GIE hidden is now P2-like, making the query key P2-correlated), displacing the P1-committed LP state within a bounded number of confirmations.

**Pass criterion for CLS prediction:** LP prior updates to P2-dominant within 20 confirmations of Board B switch.

**Measurement addition (new):** On each CMD 5 step during the transition period, log which VDB node was retrieved (node index + its pattern label if available). This directly identifies where the bottleneck is:
- If retrieval fails (P1 nodes returned despite P2 query): the NSW graph structure is the bottleneck.
- If retrieval succeeds (P2 nodes returned) but LP doesn't update: the agreement mechanism or HOLD dynamics are the bottleneck.
- If both succeed: the CLS prediction holds and the paper has a strong empirical grounding.

**If the CLS prediction fails:** This is still a publishable result. A system that classifies P2 correctly but locks its temporal prior to P1 for 50+ confirmations is a system where the CLS structural analogy holds but the dynamic prediction does not. Document this as a refinement of the analogy, not a refutation of the system.

---

## Section 7: Pillar 3 Timing

The fixed-weight CLS regime is where the most novel theoretical contributions live. The Pillar 3 (Hebbian weight updates) implementation would begin the transition to a learning regime and partially dissolve the CLS parallel — the "neocortex" would no longer be fixed, the "hippocampus" would no longer be permanently load-bearing.

**Decision:** Document the fixed-weight regime completely before implementing Pillar 3.

Concrete order:
1. TEST 14 (kinetic attention, fixed weights) — run and document
2. TEST 14C (transition experiment) — run, document, CLS prediction evaluated
3. Write and submit the TEST 12/13/14 paper (Stratum 1)
4. Write and submit the CLS architecture paper (Stratum 2)
5. Write and submit the prior-signal separation note (Stratum 3)
6. Implement Pillar 1 (Dynamic Scaffolding) and Pillar 2 (SAMA) — both compatible with fixed weights
7. Implement Pillar 3 (Hebbian updates) — document the regime transition as its own paper

This is not procrastination about Pillar 3. It is recognizing that the current system, right now, has the most interesting theoretical properties in the literature. It should be documented fully before it's changed.

---

## Section 8: Firmware Structure

The current firmware organizes TEST 12 and TEST 13 within the same binary, differentiated by compile-time flags. Phase 5 adds TEST 14 with three conditions (14A, 14B, 14C) plus the Nucleo SPI path. This complexity will make the firmware unreadable for reviewers and unreliable for replication.

**Decision:** Refactor the firmware structure before writing Phase 5 code.

Structure:
- **Core layer** (stable, version-tagged, not changed between tests): GIE init, VDB operations, LP CfC step, CMD 4/5 dispatch, classification callback skeleton, gate bias infrastructure.
- **Test layer** (per-test, clearly demarcated): condition flags, parameter values, logging format, Board B timing assumptions, pass criteria.

Each test condition (14A, 14B, 14C) should be readable as a self-contained configuration applied to the stable core. A reviewer should be able to find the difference between TEST 14A and 14B in 10 lines of code, not by diffing two 600-line files.

Time estimate: 2-3 hours before Phase 5 implementation. Do this before the Phase 5 firmware is written.

---

## Section 9: The Unified Framework

Eight research projects, all ternary, all hardware-first, targeting SSRN in 3-6 weeks. The Reflex is one. MTFP21/L-Cache is another. The others are unknown to the observer but share the design philosophy.

**Recommendation:** Write one internal framework memo before coordinating submissions. The candidate frame:

> **Hardware-native ternary computing**: a research program for building AI systems that work with silicon rather than against it. Systems in this program share: ternary arithmetic as the native precision, peripheral hardware or register files as the compute substrate, minimum-assumption design philosophy, and hardware-verified falsification as the standard for claims.

If the 8 projects fit this frame, the papers should cite each other and be submitted with a covering note. A coordinated cluster of papers on a unified program is more visible and more citable than 8 independent submissions landing in the same 6-week window.

If they don't fit the frame, the frame should be revised before submission. The memo is the forcing function for finding out.

---

## Section 10: Immediate Actions (Ordered by Blocking Dependency)

| Priority | Action | Status | Blocks |
|----------|--------|--------|--------|
| 1 | Fix KINETIC_ATTENTION.md group formula | **Done** | Phase 5 firmware |
| 2 | Fix kinetic_attention_synth.md group formula | **Done** | Phase 5 firmware |
| 3 | Refactor firmware (core vs. test layers) | Pending | Phase 5 code quality |
| 4 | Implement Phase 5 firmware (TEST 14 A/B/C) | Pending | TEST 14 data |
| 5 | Run TEST 14 — collect all three conditions | Pending | Phase 5 papers |
| 6 | Run TEST 14C with retrieval node logging | Pending | CLS prediction validation |
| 7 | UART falsification | Pending | **All paper submissions** |
| 8 | Write prior-signal separation note | Pending | Stratum 3 paper |
| 9 | Write unified framework memo | Pending | Submission coordination |
| 10 | Write TEST 12/13/14 paper | Pending | Stratum 1 publication |

---

## Section 11: Hardware Session (This Afternoon)

The wiring session connects Board A (ESP32-C6, primary receiver) to Nucleo L4R5ZI-P via SPI2 at 40 MHz.

Full connection spec: `docs/HARDWARE_TOPOLOGY.md`.

Session goal: SPI loopback test at 10 MHz → 20 MHz → 40 MHz. IRQ line test. First real SPI transaction (fixed 48-byte null query, 16-byte zero response). Do not modify Reflex core firmware to require the Nucleo.

**Operating mode boundary maintained:** The Nucleo connection is APU-expanded mode instrumentation. The autonomous core tests (TEST 14) run without the Nucleo.

---

*Date: March 23, 2026 (morning/afternoon — LMM + documentation)*
*Commit: aceb7f9 (LMM assessment) + this session*
*Depends on: `docs/KINETIC_ATTENTION.md`, `journal/kinetic_attention_synth.md`, all project_assessment_* journal files*

---

## Section 12: Evening Session — TEST 14C Simulation and CLS Clarification

The afternoon documentation session extended into an evening simulation session. Three threads:
Mnemo cross-project analysis, TEST 14C simulation in AVX2 C, and a conceptual clarification
on CLS theory. Each produced standalone documentation.

### Thread 1: Mnemo Prior-Signal Gap Analysis

The user shared `github.com/anjaustin/mnemo` (production Rust memory system, v0.5.5) as a
RAG system for discussion. Analysis against the five-component prior-signal separation
architecture revealed:

- **Components 1–3 partially present:** `identity_core` (slow prior), `experience` layer
  (fast accumulator), cryptographic audit wall on writes. Strong on structural separation
  of writes; weak on query construction.
- **Gap: TinyLoRA contamination.** Rank-8 LoRA adapters rotate query embeddings toward
  prior relevance history before retrieval. The evidence-reader is prior-contaminated at
  query construction time — the structural separation fails at the input.
- **Components 4–5 absent:** No retrieval-time disagreement detection; no evidence-deference
  policy.

Proposed fixes documented in `docs/MNEMO_PRIOR_SIGNAL_GAPS.md`:
1. **LoRA gate:** `effective_lora_scale = BASE_LORA_SCALE * max(0, agreement - threshold)`
   — identical mechanism to Reflex kinetic attention gate bias, different substrate.
   Fungible computation: the algorithm does not care what it runs on.
2. **`retrieval_disagreement_score`:** Cross-channel comparison (literal vs. semantic)
   to surface prior-vs-evidence conflicts.
3. **`deference_policy`:** API field (none / guided / strict) controlling context assembly
   when disagreement is high.

The LoRA gate connection to kinetic attention is the key insight: the Reflex's
`gate_bias = BASE_GATE_BIAS * max(0, agreement)` and Mnemo's proposed
`effective_lora_scale = BASE_LORA_SCALE * max(0, agreement - threshold)` are the same
algorithm. The substrate is different. The computation is fungible.

### Thread 2: TEST 14C AVX2 Simulation

`sim/test14c.c` implemented and run. Full results in `docs/LCACHE_TEST14C_SIM_RESULTS.md`.

**Key architectural clarification found during implementation:**

Gate bias affects `gie_state` (which neurons fire into VDB/LP), not TriX classification.
TriX is always correct by `W_f hidden = 0`. This means the interesting 14A vs 14C comparison
is not speed-of-TriX-confirmation (trivially identical — structural guarantee) but LP state
quality post-transition.

**Results:**
- Structural guarantee: 1000/1000 trials pass in T14_MIN_SAMPLES=15 steps, both conditions.
- LP alignment at step 30+: 14C ahead by ~+0.025, stable through step 200.
- Crossover at exactly T14_MIN_SAMPLES — gate_bias[P2] arms at the cold-start guard.
- gate_bias[P1] self-extinguishes to 1.57 (vs naive 3.09) because agreement stops refreshing
  it the moment p_hat switches to P2.
- LP firing rate: 14C 3% higher than 14A through Phase 2.

**Three-phase transition structure confirmed:**
1. Assimilation (steps 1–15): VDB/TriX immediate. Both conditions identical.
2. Gate arms (step 15): gate_bias[P2] activates. 14C pulls ahead.
3. Integration (steps 15–200+): 14C holds ~0.025 alignment advantage. 14A drifts.

### Thread 3: CLS — Assimilation, Integration, and the Pre-Training Assumption

Two conceptual clarifications emerged from the simulation discussion:

**CLS defined (for the record):** Complementary Learning Systems — McClelland, McNaughton,
O'Reilly (1995). Two memory systems that solve fundamentally incompatible requirements
(fast one-shot encoding vs. slow statistical generalization) by operating on structurally
separate substrates rather than a single shared one. Hippocampus = fast assimilation,
isolated from neocortical prior. Neocortex = slow integration, compressed statistics across
many episodes.

**Assimilation vs. Integration:** Assimilation takes new experience in with high fidelity,
without distorting it to fit existing schema. Integration extracts statistical structure
across many assimilated episodes. These are not two speeds of the same operation — they are
structurally different operations that require structural separation to coexist without
interference.

**The gross assumption corrected:** Pre-training / fine-tuning maps onto the *timescale*
distinction (slow broad learning vs. fast targeted learning) but misses the structural
requirement entirely. CLS requires not just different timescales but different substrates
where the fast learner's encoding is not shaped by the slow learner's prior expectations.

Fine-tuning updates the same weights that hold the prior. When fine-tuning data conflicts
with the pre-training prior, there is no independent arbiter — the prior usually wins. This
is why fine-tuned models remain confidently wrong on out-of-distribution inputs from the
pre-training distribution.

The Reflex has structural separation because hardware enforced it. Pre-training / fine-tuning
is two timescales in one substrate. CLS is two timescales in two structurally separated
substrates. The distinction is not a detail — it is the mechanism.

---

## Immediate Actions Updated

| Priority | Action | Status |
|----------|--------|--------|
| — | Documentation: README, CURRENT_STATUS, cross-references | **Done** |
| — | Complete `PRIOR_SIGNAL_SEPARATION.md` (abstract + research program) | **Done** |
| — | Mnemo gap analysis (`MNEMO_PRIOR_SIGNAL_GAPS.md`) | **Done** |
| — | TEST 14C AVX2 simulation (`sim/test14c.c`) | **Done** |
| — | TEST 14C simulation results (`LCACHE_TEST14C_SIM_RESULTS.md`) | **Done** |
| 1 | Firmware refactor (core vs. test layers) | Pending |
| 2 | Implement TEST 14A/14B/14C firmware | Pending |
| 3 | UART falsification | **Blocking all submissions** |
| 4 | Run hardware TEST 14 (all three conditions) | Pending |
| 5 | Unified framework memo | Pending |
| 6 | Write TEST 12/13/14 paper | Pending |

---

## Section 13: LMM on TEST 14C Open Risks

After the simulation session, three open items from the red-team remained unresolved:
BLEND_ALPHA has no firmware grounding, the +0.019 LP delta is below trit_dot resolution,
and HOLD-dominated hardware might suppress LP dynamics to near zero. Deployed a four-phase
Lincoln Manifold Method cycle on these three items.

**Source files:**
- `journal/open_risks_raw.md` — Phase 1: stream of consciousness
- `journal/open_risks_nodes.md` — Phase 2: 10 nodes, tension table
- `journal/open_risks_reflect.md` — Phase 3: resolved tensions, hidden assumptions challenged
- `journal/open_risks_synth.md` — Phase 4: synthesis, decision tree, action sequence

**Root finding:** The three open items are not three problems. They are three symptoms of one
unknown: what is the dominant LP update path on hardware?

**Two paths, two regimes:**

| | Path A (CfC firing) | Path B (VDB blend) |
|---|---|---|
| Mechanism | `trit_gate(f, keep, g)` fires when f ≠ 0 | VDB nearest-neighbor blended into `lp_hidden` |
| Controls | Weight sparsity, input distribution | `ulp_fb_threshold`, VDB node count |
| Metric | `lp_running_sum` alignment | VDB P2 recall fraction |
| Effect size (sim) | +0.019 at step 30, +0.041 at step 200 | Unknown (BLEND_ALPHA invented) |

**Key insight:** HOLD-dominance applies to the GIE CfC (gate_threshold=90), not the LP
CfC. The LP fires when `|dot_f| > 0` — no gate threshold. The LP is not necessarily
HOLD-dominated at all. The `fb_blend_count` and `lp_dots_f` observables in LP SRAM can
answer the question directly.

**Decision tree after LP characterization:**
- `fires/step > 2`: Path A dominant → `lp_running_sum` metric valid, TEST 14C as designed
- `fires/step < 1`, `implied_alpha > 0.1`: Path B dominant → VDB recall quality is the metric
- Both weak: extend steps, lower `ulp_fb_threshold`, or re-examine firmware

**Firmware findings from source read:**
- LP CfC runs in RISC-V assembly (`main.S`), dispatched by `lp_command` (0–5)
- `lp_dots_f[16]` written every CfC step — direct Path A rate measurement
- `fb_blend_count` written every cmd=5 step — direct Path B rate measurement
- `fb_applied`, `fb_score`, `fb_threshold` give full VDB feedback observability
- VDB blend is deterministic (gap fill / conflict / agreement rules), not probabilistic BLEND_ALPHA

---

## Section 14: LP Characterization Firmware

Implemented LP characterization as a diagnostic test block in `geometry_cfc_freerun.c`,
inserted after TEST 13 and before the summary. Commit: `625b00d`.

**What it does (per LP step via cmd=5):**
- Reads `lp_dots_f[16]` → counts non-zero entries → Path A firing rate
- Reads `ulp_fb_blend_count` → Path B blend count (trits changed per step)
- Reads `ulp_fb_score` / `ulp_fb_applied` → VDB match quality
- Records full `lp_hidden[16]` at each step in the switch window

**Protocol:**
- Phase 1: synthetic P1 gie_hidden (first 16 GIE dims = +1), 500 steps (~5s)
- Phase 2: synthetic P2 gie_hidden (last 16 GIE dims = +1), 100 steps (~1s)
- 3 repetitions. VDB seeded every 25 steps during Phase 1.
- `ulp_fb_threshold = 1` (apply feedback for any positive VDB match score)
- No gate bias (14A baseline condition)

**Output:**
- Per-rep: `fires/step`, `blend/step`, `implied_alpha = blend/LP_HIDDEN_DIM`
- Switch-window table: last 20 P1 steps → first 30 P2 steps, with `lp_hidden` as +/0/- string
- REGIME verdict: Path A dominant / Mixed / Path B dominant
- Sim calibration line: `BLEND_ALPHA = xxx` to replace the invented 0.2 in `sim/test14c.c`

**Why this is the right next step:** All three open items from Section 13 close when this
runs. The LP characterization data directly feeds the TEST 14C firmware parameter choices
and resolves the metric question before any hardware cycles are spent on the wrong approach.

**After LP CHAR runs:**
1. If Path A dominant: `lp_running_sum` metric valid. Proceed with TEST 14C as designed.
   Recalibrate `LP_SIM_THRESHOLD` to match observed firing rate.
2. If Path B dominant: Switch to VDB P2 recall quality metric. Replace `BLEND_ALPHA = 0.2`
   in `sim/test14c.c` with the measured `implied_alpha`. Re-run sim with correct calibration.
3. In both cases: UART falsification can proceed in parallel.

---

## Immediate Actions (Updated — End of Session)

| Priority | Action | Status |
|----------|--------|--------|
| — | Fix KINETIC_ATTENTION.md group formula | **Done** |
| — | Fix kinetic_attention_synth.md group formula | **Done** |
| — | TEST 14C AVX2 simulation (three-claim, three-condition) | **Done** |
| — | TEST 14C simulation results doc | **Done** |
| — | LMM on TEST 14C open risks (4 journal files) | **Done** |
| — | Read firmware source (LP CfC, VDB blend, main.S dispatch) | **Done** |
| — | LP characterization firmware in geometry_cfc_freerun.c | **Done — commit 625b00d** |
| 1 | **Run LP CHAR on hardware** — flash firmware, capture UART output | **Ready to run** |
| 2 | Recalibrate `sim/test14c.c` with hardware-grounded `BLEND_ALPHA` | After LP CHAR run |
| 3 | Re-run TEST 14C simulation at correct calibration | After recalibration |
| 4 | Implement TEST 14C firmware with regime-appropriate metric | After LP CHAR + sim |
| 5 | UART falsification | **Blocking all paper submissions** |
| 6 | Firmware refactor (core vs. test layers) | Before Phase 5 code lands |

---

*Date: March 23, 2026 (full day)*
*Commits: aceb7f9 → 625b00d*
*New documents: `docs/MNEMO_PRIOR_SIGNAL_GAPS.md`, `docs/LCACHE_TEST14C_SIM_RESULTS.md`*
*New journal: `journal/open_risks_{raw,nodes,reflect,synth}.md` (LMM cycle 2)*
*New code: `sim/test14c.c`, `sim/Makefile`; LP CHAR in `embedded/main/geometry_cfc_freerun.c`*
*Depends on: `docs/PRIOR_SIGNAL_SEPARATION.md`, `docs/KINETIC_ATTENTION.md`,*
*  `journal/kinetic_attention_synth.md`, `docs/LCACHE_REFLEX_OPCODES.md`*
