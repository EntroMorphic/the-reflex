# The Reflex: Strategic Roadmap

*Last updated: April 11, 2026 — Two compounding bugs fixed (trix_enabled, sender enrollment starvation). Multi-seed TEST 14C re-run with corrected sender. Label-free 100% classification achieved (P2 payload redesigned). Bias release correctly described as geometric ×0.9/step. Test harness split into per-area files. 66 inactive C files archived.*

---

## Current State

As of commit `c7ef286`, the Reflex architecture has demonstrated:

- **GIE**: Peripheral-hardware ternary dot products at 430.8 Hz, ISR-driven. TriX classification accuracy: 100% on 4 well-separated patterns with distinct payloads, **100% label-free** (pattern_id trits masked from signatures, commit `c7ef286`). Prior P1-P2 confusion (71% label-free) was caused by near-identical P2 payload, not the classifier.
- **VDB**: 64-node NSW graph in LP SRAM, 48-trit vectors, recall@1=95%, 10–15ms round-trip.
- **LP CfC**: 16-trit hidden state, CMD 5 (CfC + VDB + feedback blend) running at ~100 Hz on the 16 MHz LP core (~30 µA).
- **Memory-modulated priors**: LP hidden state develops pattern-specific representations after 90s of live operation. VDB feedback is causally necessary — ablation (CMD 4) collapses P1 and P2 to Hamming=0 in 2 of 3 runs. Multi-seed validated (3 seeds, `data/apr9_2026/SUMMARY.md`).
- **Kinetic attention (Phase 5)**: Agreement-weighted gate bias with two release paths: soft geometric decay (×0.9/step, half-life ~6.6 steps) and hard disagree-count zero (≥4 trits, not exercised on clean seeds). LP feedback dispatched from TriX ISR. Integer bias state — no floating point in the mechanism path. `pred` flips at step +1 post-switch; bias fully released by ~step 20; new prior forms by ~step 15. Multi-seed TEST 14C verified (3 seeds × 3 conditions, `data/apr9_2026/`).
- **CLS stabilization**: VDB stabilization confirmed across Seeds A and C — ablation regression visible in alignment traces. Seed B shows a TriX@15 headwind (Full 8/15 < No-bias 12/15 < Ablation 14/15) that may be seed-intrinsic or procedural (single run, not yet replicated).
- **Three papers drafted**: Stratum 1 (engineering), Stratum 2 (CLS architecture), Stratum 3 (prior-signal separation). **Need rewriting** — cite crossover-step numbers from pre-April-9 broken data. See `DO_THIS_NEXT.md`.

The modulation loop is closed. The three pillars are next.

---

## Immediate Blockers (Before Paper Submission)

1. **UART-only verification.** Re-route console to GPIO 16/17, power from battery/dumb USB, direct current measurement. All runs to date use USB-JTAG. The "peripheral-autonomous" and "~30 µA" claims require this data.

2. **Full test suite validation.** 14/14 PASS achieved with `MASK_PATTERN_ID=1` and distinct P2 payload (commit `c7ef286`, `data/apr11_2026/r3a_p2_distinct_payload.log`). Tests 1-13 also pass post-refactor (test harness split validated). **Remaining:** multi-seed TEST 14C needs re-running with the new P2 payload to update the transition-experiment numbers in the papers.

---

## Phase 5: Kinetic Attention (Verified)

**Claim verified:** LP hidden state biases GIE gate thresholds via agreement-weighted gate bias. Two release paths: soft geometric decay (×0.9/step, half-life ~6.6 steps, runs unconditionally) and hard disagree-count zero (≥4 trits, safety gate, not exercised on clean seeds). LP feedback dispatched from TriX ISR. No float in the mechanism path.

**Multi-seed TEST 14C results (3 seeds × 3 conditions, `data/apr9_2026/SUMMARY.md`):** TriX@15 accuracy: A=15/15, B=8/15, C=15/15. VDB stabilization (alignment traces) holds across Seeds A and C; Seed B shows a headwind that may be seed-intrinsic or procedural (n=1, not yet replicated — see `DO_THIS_NEXT.md` R1/R2). `pred` flips at step +1 for clean seeds. Bias decays geometrically, fully released by ~step 20. **Note:** apr9 multi-seed data was collected with the OLD P2 payload (0xAA). The distinct P2 payload (commit `c7ef286`) improves label-free accuracy to 100% but has not yet been used for a multi-seed TEST 14C sweep.

**Why this was first:** Kinetic attention provides the behavioral measure that all three pillars require. It is now the foundation for forward work.

**Design:** See `docs/KINETIC_ATTENTION.md` for full specification. Summary:

1. Add `lp_gate_bias[4]` to LP SRAM layout (HP-writable, ISR-readable).
2. HP core projects lp_hidden onto pre-computed LP-space signatures (from TEST 12 means) to produce per-pattern-group gate bias values.
3. ISR applies: `effective_threshold = gate_threshold + lp_gate_bias[neuron_group]`, with a hard floor.
4. TEST 14 (three conditions: scalar bias, per-group bias, bias=0 baseline) measures whether LP prior amplifies LP divergence above TEST 12 baseline and whether the system remains stable through pattern switches.

**Pass criteria for TEST 14:**
- Classification accuracy remains 100%
- LP Hamming matrix under per-group bias ≥ TEST 12 on ≥ 4 of 6 pairs
- System updates LP prior within 15 confirmations of a Board B pattern switch (no lock-in)
- GIE hidden state does not saturate (energy < 60/64 on average)

**The paper:** "Kinetic Attention in a Ternary Reflex Arc: Sub-conscious Priors Shape Peripheral Computation"

---

## Pillar 1: Dynamic Scaffolding (Memory as Sliding Window)

**The problem:** LP SRAM is 16KB. The VDB is capped at 64 nodes. With 4 patterns and ~8 inserts per pattern per 90s run, the VDB fills in ~3 minutes at the current insert rate. Adding a fifth pattern immediately compresses headroom.

**The step-change:** The LP core monitors VDB node utility and prunes "dissolved" nodes autonomously, freeing space for new high-novelty states.

**Pruning criterion (revised from original roadmap):** The original criterion was "prune when the CfC can represent the state without the memory." With fixed CfC weights, the CfC may permanently be unable to represent certain states — the VDB is not a temporary scaffold but a permanent load-bearing component (see `docs/KINETIC_ATTENTION.md`, Section 6.3). Revised criterion:

- **Prune if redundant**: node's LP-hidden portion is within Hamming 1 of the current LP mean for its pattern. The memory is captured by the accumulator; it adds no information to retrieval.
- **Retain if distinctive**: node's LP-hidden portion is an outlier (Hamming ≥ 3 from pattern mean). This memory encodes a rare state the accumulator doesn't represent.
- **Retain if load-bearing**: node is on a short path in the NSW graph (high betweenness). Pruning it would disconnect the graph; routing quality matters more than redundancy.

**Mechanism:** LP core runs a background pass (CMD 6, new command) comparing each node's LP-hidden portion against the current LP mean vector. Nodes below the Hamming threshold are marked for deletion. NSW graph edges are reconnected to maintain M=7 degree before the node is zeroed.

**Impact:** The 64-node limit becomes a sliding window on the "frontier" of experience. Stable, well-represented patterns are compressed; novel states retain their slots. Effective memory capacity scales with experience diversity rather than absolute pattern count.

**Prerequisite:** Phase 5 kinetic attention. The pruning criterion requires knowing which memories are load-bearing — which requires understanding the kinetic effect of VDB content on GIE behavior. Pruning a "redundant" memory that actually provides kinetic attention to a rare sub-pattern would be a regression.

---

## Pillar 2: SAMA — Substrate-Aware Multi-Agent

**The problem:** Robots currently coordinate through the Wi-Fi/UDP stack. Even with ESP-NOW, packets traverse the full LWIP stack on the receiving end before reaching application code.

**The step-change:** Treat incoming ESP-NOW packets as GIE inputs without OS involvement. Robot A's classification event triggers an immediate classification event on Robot B.

**Mechanism:**

1. Board B (the transmitter) sends a structured "Reflex Packet" encoding its current GIE hidden state as the ESP-NOW payload rather than a raw sensor reading.
2. Board A receives the packet. Instead of routing it through the existing encode path, it decodes the GIE-hidden payload directly into Board A's GIE input vector.
3. Board A's GIE immediately classifies the incoming state using its own signature weights — not Board B's. The result is: Board A perceives Board B's GIE state through its own representational lens.
4. The LP core's VDB accumulates snapshots of "when I received state X from my neighbor" — a cross-agent episodic memory.

**Why this requires Phase 5:** A robot whose LP prior doesn't influence its GIE will process all incoming states with the same threshold. A robot with kinetic attention will process states that match its current prior at lower threshold — it notices familiar neighbor states more easily. This is context-sensitive inter-agent attention, not just passive state relay.

**Impact:** A cluster of C6 chips that shares state at the GIE level. One chip's classification event propagates to others at radio speed, bypassing OS networking. The LP state of each chip reflects not just its own experience but the experience of its neighbors.

**Open question:** How do you prevent runaway synchronization? If all robots in a cluster develop the same LP prior (they all see the same signal), they all lower the same gate thresholds, amplifying the same pattern in unison. This is biological entrainment — useful in some contexts (synchronized response), catastrophic in others (all robots locked to P1 while P2 goes unobserved). A diversity mechanism — deliberately staggering VDB insert timing or introducing node-local noise — may be needed.

---

## Pillar 3: Silicon Learning — LP Hebbian (Corrected April 11)

**Original proposal (WRONG):** Update GIE W_f weights via VDB mismatch. This was wrong — GIE W_f IS the TriX classifier. Modifying it breaks the structural wall (W_f hidden = 0) and the 100% classification guarantee.

**Corrected target:** LP core weights (W_f_lp, W_g_lp). The LP temporal context layer learns; the GIE perceptual layer stays frozen. Learning improves what the system *makes of* what it sees, not what it sees.

**Implemented (commit `32fb061`):** `lp_hebbian_step()` in gie_engine.c. HP-side Hebbian update that flips LP W_f weights based on VDB mismatch, gated by retrieval stability (K=5), TriX agreement, and rate limiting (100ms).

**Tested (commit `4343447`):** Ablation-controlled TEST 15 with clean 3-phase design.
  - Control (CMD 5 only): post mean = 0.7/16
  - Hebbian (CMD 5 + learn): post mean = 3.2/16
  - Contribution: +2.5 Hamming — attributed to weight learning, not VDB maturation

**BUT: label-dependent (commit `a0d3a36`).** H2 experiment with `MASK_PATTERN_ID_INPUT=1` (genuinely label-free GIE input):
  - Control (label-free): 3.3/16
  - Hebbian (label-free): 1.7/16
  - Contribution: **-1.7 Hamming** (Hebbian HURTS without the label)

The VDB mismatch error signal is label-informed through the GIE hidden state. When the label leak is closed, the error signal is too noisy and the learning is harmful.

**Critical secondary finding:** Removing pattern_id from the GIE input **improved** VDB-only LP divergence from 0.7 to 3.3/16. The label was drowning out discriminative payload/timing features. The recommended operating mode is now `MASK_PATTERN_ID_INPUT=1`.

**Resolved: Diagnosed Hebbian v3 (commit `427fea3`).** Three iterations:
1. v1 (VDB mismatch, f-only): +2.5 with label, -1.7 without → label-dependent
2. v2 (TriX accumulator, f-only): -1.0 label-free → better target, wrong pathway
3. **v3 (TriX accumulator, diagnosed f+g): +1.3 label-free → the diagnosis fixed it**

The missing atomic was DIAGNOSIS: when the output is wrong, ask "is the error in the gate (f) or the candidate (g)?" before flipping. v1-v2 always flipped W_f. ~50% of errors were in g, making half the flips counterproductive. v3 compares |f_dot| vs |g_dot| and fixes the cheaper pathway.

Result (genuinely label-free, `MASK_PATTERN_ID=1 + MASK_PATTERN_ID_INPUT=1`):
  Control (CMD 5 only): 0.7/16
  Diagnosed Hebbian:    2.0/16
  Contribution: **+1.3 Hamming**
  P0-P1: 0→4. 161 flips across 53 updates.

**The biological analog (revised):** The TriX classifier (sensory cortex, structurally guaranteed) provides the label that organizes the LP consolidation. The VDB (hippocampus) stores and retrieves. The LP weights (neocortex) learn to represent each pattern's temporal signature through Hebbian flips gated by TriX confidence + retrieval stability. Both f-pathway (gate) and g-pathway (candidate) weights update, with per-neuron diagnosis selecting the correct target.

**Impact:** The system learns on silicon, without labels, without gradients, without floats. The structural wall is intact. The prior gets wiser through experience. Pillar 3 is operational.

---

## Physical Prerequisite: UART Falsification

*Originally Milestone 38 (March 19 roadmap). Status: pending.*

The current test setup uses USB-JTAG for console output. The March 19 session identified a potential "Silicon Interlock" where the USB-JTAG controller gates PCNT behavior — though the March 22 session ran 13/13 PASS with USB-JTAG in use, so the interlock either does not affect the current test suite or was resolved implicitly by the peripheral reset sequence.

**Action:** Re-route console to GPIO 16/17 UART. Power via battery or dumb USB. Monitor via secondary serial bridge. Run full 13-test suite and confirm all PASS.

**Why this matters:** Any paper claim that the GIE is "peripheral-autonomous" needs to be verifiable without a development tool physically attached. The current "ISR-driven, peripheral-autonomous between interrupts" language is precise — but reviewers will ask whether results replicate without JTAG. The answer should be yes, and the data should exist.

**Priority:** Low-risk to attempt at any point. Does not block Phase 5 implementation. Should be completed before paper submission.

---

## Dependency Graph

```
Phase 5: Kinetic Attention (TEST 14)
    │
    ├── Pillar 1: Dynamic Scaffolding
    │       (needs kinetic attention to know what's load-bearing)
    │
    ├── Pillar 2: SAMA
    │       (needs kinetic attention for context-sensitive inter-agent response)
    │
    └── Pillar 3: LP Hebbian (TriX-output-based)
            (needs kinetic attention as the behavioral signal
             + TriX classifier as the structurally guaranteed label source)

UART Falsification (independent, can run at any time)
```

---

## Operating Modes

The Reflex has two distinct operating modes with different power claims. All documentation and papers must specify which mode they describe.

**Autonomous Mode (~30 µA):**
C6 only. GIE + VDB + LP CfC + kinetic attention (gate bias via ISR). No Nucleo, no SPI, no QSPI. This is the operating mode for the TEST 12/13/14 paper series. The ~30 µA power claim applies only to this mode.

**APU-Expanded Mode (~10-50 mA):**
C6 + Nucleo APU (L4R5ZI-P or L4A6ZG). Adds VDB search acceleration via SPI at 40 MHz, MTFP21 inference via QSPI at 160 Mbps. Power budget dominated by the Nucleo. This mode is for MTFP21/L-Cache papers and future SAMA work.

The firmware must detect Nucleo presence at startup (SPI handshake) and fall back to autonomous VDB path if absent. Autonomous mode must work when the Nucleo is unplugged.

---

## Publication Strategy

The Reflex's contributions are stratified across three levels. Each targets a different audience and should be submitted to its own venue:

**Stratum 1 — Engineering** (embedded systems venues):
TEST 12/13/14 papers. GDMA→PARLIO→PCNT as ternary neural substrate. NSW graph in LP SRAM at ~30 µA. Agreement-weighted gate bias. All claims silicon-verified, ablation-controlled.

**Stratum 2 — Architecture** (computational neuroscience / neuromorphic venues):
CLS architecture paper. Fixed-weight CLS analog: VDB as permanent hippocampal layer, LP CfC as fixed neocortical extractor. CLS predictions tested empirically via the transition experiment (TEST 14C).

**Stratum 3 — Principle** (AI/ML venues):
Prior-signal separation note. Five-component architecture for structural hallucination resistance: prior-holder, evidence-reader, structural separation guarantee, disagreement detection, evidence-deference policy. The Reflex is the silicon-verified instantiation. Draft: `docs/PRIOR_SIGNAL_SEPARATION.md`.

Before coordinating submissions: write one internal unified framework memo mapping all 8 research projects to a common frame (hardware-native ternary computing). Coordinated cluster submission is more visible than 8 independent papers.

---

## Blocking Prerequisites (Must Complete Before Any Paper Submission)

1. **UART Falsification:** Re-route console to GPIO 16/17, power from battery/dumb USB, run full test suite without JTAG. The "peripheral-autonomous" claim requires this data, not inference from JTAG-attached runs.

2. **Firmware refactor:** Separate core layer (stable GIE, VDB, LP, CMD dispatch) from test layer (condition flags, parameters, logging) before Phase 5 code lands. Reviewers must be able to find the difference between TEST 14A and 14B in under 10 lines.

---

## Philosophy

*The hardware is the teacher. The signal is the lesson. Abstraction is the enemy.*

The path forward is not adding more software. It is finding more computation already in the silicon — more peripheral behavior that can be wired into the loop without CPU instruction cycles. The gate bias (Phase 5) uses the ISR that already exists. The VDB pruning (Pillar 1) uses LP core wake cycles that already exist. The Hebbian update (Pillar 3) uses the GDMA chain that already exists. Each step-change is not a new system; it is new use of the same substrate.

**The one-sentence description of what this system is:**

> A wireless signal classifier that draws ~30 µA and accumulates a temporal model of what it has been perceiving, using a structure that mirrors Complementary Learning Systems theory — where the memory layer cannot corrupt the classifier, but the classifier's accumulated history actively biases future perception.
