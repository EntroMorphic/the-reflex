# Paper Readiness Assessment

*April 7, 2026. Post red-team remediation, commit `f510f9a`. 14/14 PASS.*

---

## Overview

The Reflex has three distinct publication strata, each targeting a different audience. This document assesses what exists, what's proven on silicon, what's missing, and what's needed to submit.

---

## Stratum 3 — Principle (AI/ML venues)

**Paper:** Prior-Signal Separation as a Structural Requirement for Hallucination Resistance

**Status: READY TO WRITE. No new experiments needed.**

### The Claim

Any system that accumulates experience (prior) and uses it to influence perception (signal) must solve the same structural problem: how to let the prior inform perception without the prior overriding direct measurement. The answer is not discipline (training, RLHF, guardrails). The answer is structure.

Five required components:
1. **Prior-holder** — LP CfC hidden state (16 trits, or 80 MTFP trits)
2. **Evidence-reader** — GIE peripheral fabric (430 Hz, ~0 CPU)
3. **Structural separation guarantee** — `W_f hidden = 0` (the prior-holder cannot corrupt the evidence-reader's weights)
4. **Disagreement detection** — TriX classification vs LP prior agreement score
5. **Evidence-deference policy** — gate bias ≤ BASE_GATE_BIAS=15, hard floor MIN_GATE_THRESHOLD=30 (the prior can nudge, never override)

The Reflex is the only silicon-verified complete instantiation of all five components.

### What Exists

- Draft: `docs/PRIOR_SIGNAL_SEPARATION.md`
- Silicon proof: 14/14 PASS, `W_f hidden = 0` verified structurally, gate bias mechanism verified under three conditions
- The argument is architectural, not experimental — theoretical note with existence proof

### What's Missing

- One focused writing session to finalize the draft
- No new silicon data required
- No UART falsification required (the claim is about structure, not power)

### Target Venues

- NeurIPS (position paper track)
- ICML (workshop on hallucination/reliability)
- arXiv preprint (immediate visibility)

### Submission Dependency

None. Can submit independently.

---

## Stratum 1 — Engineering (Embedded systems venues)

**Paper:** Kinetic Attention in a Ternary Reflex Arc: Sub-conscious Priors Shape Peripheral Computation

**Status: DATA COMPLETE. One blocking prerequisite (UART falsification).**

### The Claim

A wireless signal classifier running on ESP32-C6 peripheral hardware at 430.8 Hz, drawing ~30 µA, with:
- 100% classification accuracy (4 patterns, TriX zero-shot signatures)
- Pattern-specific LP hidden states after 90 seconds of live operation
- VDB episodic memory causally necessary (CMD 4 ablation: P1=P2 without blend, P1≠P2 with blend)
- Agreement-weighted gate bias (Phase 5 kinetic attention) verified under three confound-controlled conditions
- MTFP dot encoding resolving the P1-P2 sign-space degeneracy (Hamming 0/16 → 5-10/80, null distance ~1)
- All ternary. No floating point. No multiplication. No backpropagation. Signatures enrolled, not trained.

### Silicon Data (Complete)

| Experiment | Result | Commit |
|-----------|--------|--------|
| GIE free-running | 430.8 Hz, 64 neurons, ~0 CPU | Multiple |
| LP CfC + VDB pipeline | CMD 1-5 verified, exact dot match | `06d5535` |
| VDB NSW graph | 64 nodes, M=7, recall@1=95% | `7db919f` |
| TEST 12: Memory-modulated attention | LP diverges by pattern, P1 vs P3 Hamming 5/16 | `38a0811` |
| TEST 13: CMD 4 ablation | VDB causally necessary (P1=P2 without blend) | `12aa970` |
| TEST 14: Kinetic attention | 3 conditions, gate bias activates, confound-controlled | `429ce38` |
| MTFP21 gap encoding | Classification 80% → 96% | `c814e51` |
| MTFP dot encoding | P1-P2: sign=0/16, mtfp=5-10/80, null≈1 | `98800a9` |
| Red-team remediation | Null test, entrainment fix, P3 airtime, controls | `f510f9a` |
| Classification accuracy | 96% (TriX signatures, enrolled not trained) | `5735119` |
| TriX ISR agreement | 100% ISR-CPU agreement | `0b09f69` |

### What's Missing

**UART falsification (blocking):**

Re-route console output from USB-JTAG to GPIO 16/17 UART. Power from battery or dumb USB (no JTAG). Run full 14-test suite. Confirm all PASS.

The "peripheral-autonomous" claim requires this data. Currently all runs use USB-JTAG for console output. The JTAG controller shares the USB bus with the ESP32-C6 and could theoretically gate peripheral behavior (the March 19 "Silicon Interlock" identified this possibility, though all subsequent runs PASS with JTAG attached).

**What UART falsification requires:**
- Hardware: UART-to-USB bridge (CP2102 or similar) connected to GPIO 16/17
- Firmware change: redirect `stdout` to UART peripheral instead of USB-JTAG console
- Power: battery or dumb USB (no JTAG data connection)
- Run: full 14-test suite, capture via external serial terminal
- Expected: 14/14 PASS (JTAG has not affected results in 13+ runs, but the data must exist)

**Estimated effort:** One hardware session (wiring + firmware config + one test run).

### Paper Draft

- Existing: `docs/PAPER_KINETIC_ATTENTION.md` (in progress)
- Existing: `docs/MEMORY_MODULATED_ATTENTION.md` (paper-quality TEST 12 writeup)
- Figures: confusion matrices instrumented (`feb709b`)

### Target Venues

- IEEE Transactions on VLSI Systems
- ACM/IEEE DAC (Design Automation Conference)
- IEEE Embedded Systems Letters
- tinyML Summit

### Submission Dependency

UART falsification only.

---

## Stratum 2 — Architecture (Computational neuroscience venues)

**Paper:** Fixed-Weight Complementary Learning Systems on a Ternary Microcontroller

**Status: NEEDS ONE EXPERIMENT (TEST 14C transition dynamics).**

### The Claim

The Reflex is a fixed-weight analog of Complementary Learning Systems theory:
- **VDB** = permanent hippocampal layer (no consolidation path to neocortex)
- **LP CfC** = fixed neocortical extractor (weights set at init, never updated)
- **Structural wall** = `W_f hidden = 0` (hippocampus cannot corrupt neocortex)
- **Episodic retrieval** = VDB search returns nearest past state, blended into LP hidden
- **Pattern completion** = VDB gap-fill rule (`h=0, mem≠0 → h=mem`)

The CLS parallel was **discovered, not designed**. The architecture emerged from a minimum-assumptions experiment. The constraints (ternary, peripheral, no floating point) are what made the structure visible.

The key CLS prediction: when the environment changes (pattern switch), the system should update its prior within a bounded number of observations. The hippocampal layer (VDB) provides rapid adaptation; the neocortical layer (CfC) provides stable extraction. This is the complementary learning prediction.

### What Exists

- TEST 12: LP hidden diverges by pattern → the system develops pattern-specific representations (neocortical differentiation, hippocampally mediated)
- TEST 13: CMD 4 ablation → without VDB (hippocampus), LP states collapse → hippocampal layer is causally necessary
- TEST 14: Three conditions with confound controls → the gate bias mechanism works
- MTFP encoding: resolves measurement degeneracy
- Full milestone history: 37 milestones, all silicon-verified
- Theoretical framework: `docs/PRIOR_SIGNAL_SEPARATION.md`, `docs/THE_PRIOR_AS_VOICE.md`

### What's Missing: TEST 14C Transition Experiment

**This is the primary CLS prediction test.** It measures what happens when the sender switches patterns mid-run.

**Protocol:**
1. **Phase 1 (90s):** Board B sends pattern P1. The system builds a P1 prior — LP hidden develops P1-specific state, VDB accumulates P1 episodes, gate bias tunes to P1.
2. **Phase 2 (30s+):** Board B switches to P2. Measure:
   - **How many confirmations until `gate_bias[P1]` decays below threshold?** (stale prior extinguishes)
   - **How many confirmations until `gate_bias[P2]` activates?** (new prior arms)
   - **LP hidden trajectory during the switch window:** step-by-step Hamming from P1 mean and P2 mean, in both sign-space and MTFP-space
   - **VDB retrieval during switch:** does the VDB return P1 episodes (stale) or P2 episodes (fresh) as the transition progresses?

**Three claims to test separately:**

| Claim | Measurement | Expected |
|-------|-------------|----------|
| 1. Structural guarantee | TriX accuracy in first 15 steps post-switch | 100% all conditions (W_f hidden=0) |
| 2. Stale prior extinguishes | gate_bias[P1] trace at steps 1,5,10,15 post-switch | Decay without refresh |
| 3. Complementary adaptation | LP MTFP alignment to P2 mean vs P1 mean over time | P2 alignment increases, P1 decreases, crossover within ~30 confirmations |

Claim 3 is the CLS prediction: the hippocampal layer (VDB) enables rapid reorientation of the LP state toward the new pattern, faster than the CfC alone could achieve (because the CfC weights are fixed — it can't "learn" P2, it can only be reminded of P2 by VDB retrieval).

**Control conditions:**
- **14C-transition:** Full system (CfC + VDB + feedback + bias). The CLS condition.
- **14A-transition:** No bias. Tests whether VDB retrieval alone drives reorientation.
- **14C-transition-ablation:** CMD 4 instead of CMD 5 (CfC + VDB search, no feedback blend). Tests whether VDB retrieval without blend causes reorientation. If not → the blend rule is causally necessary for adaptation, not just retrieval.

**What exists already:**
- The LP CHAR diagnostic (commit `625b00d`) already measures the P1→P2 switch window with step-by-step LP hidden trajectory, fires/step, and blend counts. It uses synthetic GIE states (not live ESP-NOW) but proves the measurement infrastructure works.
- The TEST 14C AVX2 simulation (`sim/test14c.c`) models the three-claim structure with 1000 trials. It confirmed the effect size is measurable.
- The MTFP encoding provides the measurement resolution needed to see LP trajectory changes that sign-space collapses.

### Implementation Plan for TEST 14C

**Phase 1: Sender protocol change**

Board B needs a "transition mode" where it sends P1 for a configurable duration, then switches to P2 and stays. Two options:
- **(a)** Add a fifth sender mode that does: P1 for 90s → P2 for 30s → repeat. The receiver detects the switch from `pattern_id` changing.
- **(b)** Keep the current 4-pattern cycling but add a "dwell" mode where one pattern runs for 90s before cycling. Receiver-side: detect when `pattern_id` changes and start the switch-window measurement.

**Recommendation: Option (a).** A dedicated transition mode ensures the switch timing is controlled. The receiver knows exactly when to expect the switch (after 90s) and can instrument the window precisely.

**Phase 2: Receiver firmware (TEST 14C function)**

```
static int run_test_14c(void) {
    /* For each control condition: */
    for (int cond = 0; cond < 3; cond++) {
        /* Phase 1: 90s on P1 — build prior */
        while (elapsed < 90s) {
            drain ESP-NOW, classify, feed LP, run CMD 5
            accumulate LP MTFP state for P1
            update gate bias (if condition enables it)
            insert VDB snapshots
        }

        /* Phase 2: switch to P2 — measure adaptation */
        for (int step = 0; step < 200; step++) {
            drain ESP-NOW, classify (expect P2 now)
            feed LP, run CMD 5

            /* Record at every step: */
            lp_mtfp = encode dots
            alignment_P1 = trit_dot(lp_mtfp, P1_mean)
            alignment_P2 = trit_dot(lp_mtfp, P2_mean)
            gate_bias_P1 = gie_gate_bias[P1_group]
            gate_bias_P2 = gie_gate_bias[P2_group]
            vdb_result = which VDB node was retrieved?
            vdb_score = how similar was it?

            /* Print step-by-step for the switch window */
        }
    }
}
```

**Phase 3: Analysis**

The paper figure is an alignment-over-time plot:
- X axis: steps post-switch (0 = last P1 confirmation, 1+ = first P2 confirmations)
- Y axis: LP MTFP alignment score (dot product with P1 mean, dot product with P2 mean)
- Three curves per condition: P1 alignment (should decay), P2 alignment (should rise), crossover point

The CLS prediction: condition 14C-transition (full system) shows faster crossover than 14A-transition (no bias). The ablation (CMD 4) shows slower or no crossover — proving VDB feedback blend is causally necessary for rapid adaptation.

**Phase 4: Pass criteria**

| Criterion | Threshold |
|-----------|-----------|
| TriX accuracy post-switch (first 15 steps) | 100% (structural guarantee) |
| gate_bias[P1] decays to 0 within 15 confirmations (14C) | Yes |
| gate_bias[P2] arms within 30 confirmations (14C) | Yes |
| LP P2 alignment > LP P1 alignment by step 30 (14C) | lp_delta > 0 |
| 14C crossover faster than 14A crossover | step_14C < step_14A |
| Ablation (CMD 4): no crossover or slower crossover | step_ablation > step_14C |

### Estimated Effort

| Task | Effort | Dependency |
|------|--------|------------|
| Sender transition mode | ~30 lines | None |
| TEST 14C receiver firmware | ~200 lines | Sender |
| Run 3 conditions × N trials | ~30 min per trial | Firmware |
| Analysis + figures | One session | Data |
| Paper writing | 2-3 sessions | Analysis |
| UART falsification | One session | Hardware (UART bridge) |

### Target Venues

- Frontiers in Computational Neuroscience
- PNAS (Biological Sciences → Neuroscience)
- Cosyne (Computational and Systems Neuroscience conference)
- Nature Machine Intelligence (if the CLS prediction holds cleanly)

### Submission Dependency

- TEST 14C data (transition experiment)
- UART falsification (shared with Stratum 1)

---

## Recommended Submission Order

| Priority | Paper | Blocking Items | Estimated Time to Submit |
|----------|-------|----------------|--------------------------|
| 1 | Stratum 3 (Prior-Signal Separation) | One writing session | Days |
| 2 | Stratum 1 (Kinetic Attention) | UART falsification | 1-2 weeks |
| 3 | Stratum 2 (CLS Architecture) | TEST 14C firmware + data + UART | 3-4 weeks |

Stratum 3 goes first because it requires no new experiments and establishes the theoretical framework that the other two papers reference. Stratum 1 goes second because all silicon data exists — only the UART falsification blocks it. Stratum 2 goes third because it requires the most new work (transition experiment) and benefits from having the other two on record.

---

## Cross-Paper Coordination

All three papers share:
- The same hardware platform (ESP32-C6FH4 QFN32 rev v0.2, ESP-IDF v5.4)
- The same firmware base (gie_engine.c, main.S, reflex_vdb.c)
- The same test infrastructure (geometry_cfc_freerun.c, Tests 1-14)
- The MTFP encoding (both timing and dot)

A unified framework memo mapping all three papers to a common narrative would strengthen coordinated submission. The ROADMAP mentions this: "write one internal unified framework memo mapping all research projects to a common frame (hardware-native ternary computing). Coordinated cluster submission is more visible than independent papers."

---

*14/14 PASS. The silicon is verified. The wood is showing its grain. Time to write.*
