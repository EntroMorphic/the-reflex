# The Reflex: Strategic Roadmap

*Last updated: March 22, 2026 — post TEST 12/13, 13/13 PASS.*

---

## Current State

As of commit `12aa970`, the Reflex architecture has demonstrated:

- **GIE**: Peripheral-hardware ternary dot products at 430.8 Hz, ISR-driven, 100% classification accuracy on 4 known patterns from live wireless input (ESP-NOW).
- **VDB**: 64-node NSW graph in LP SRAM, 48-trit vectors, recall@1=95%, 10–15ms round-trip.
- **LP CfC**: 16-trit hidden state, CMD 5 (CfC + VDB + feedback blend) running at ~100 Hz on the 16 MHz LP core (~30 µA).
- **Memory-modulated priors**: LP hidden state develops pattern-specific representations after 90s of live operation. All cross-pattern pairs diverge Hamming ≥ 1. VDB feedback is causally necessary — ablation (CMD 4) collapses P1 and P2 to Hamming=0 in 2 of 3 runs.
- **Claim verified**: The sub-conscious layer reflects classification history. Modulation is **potential** — the LP state contains pattern information, but does not yet shape GIE behavior.

The modulation loop is half-closed. What remains is making it kinetic.

---

## Phase 5: Kinetic Attention (Immediate Priority)

**The claim to be tested:** LP hidden state biases GIE gate thresholds, making peripheral hardware compute differently based on accumulated experience.

**Why this is first:** Every other step-change becomes more interesting and more rigorous once the LP state actively shapes perception. SAMA (Pillar 2) without kinetic attention is a passive relay. Hebbian learning (Pillar 3) without kinetic attention changes weights that may or may not influence behavior — kinetic attention gives you a direct behavioral measure. Dynamic scaffolding (Pillar 1) pruning criteria require knowing which memories the CfC can represent independently — which requires kinetic attention experiments to establish.

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

## Pillar 3: Silicon Learning — Hebbian GIE

**The problem:** The GIE weights are fixed at init time (sign of mean over 30s observation). They never update. If the input distribution shifts — new sender, different environment, channel degradation — the signatures become stale and accuracy degrades.

**The step-change:** The LP core generates a weight-update signal based on VDB mismatch and applies it to the GDMA descriptor chain in-situ.

**Mechanism:**

1. After each VDB search (CMD 4/5), the LP core compares the retrieved memory against the current GIE hidden state. The mismatch (Hamming distance × sign of disagreement per trit) is the error signal.
2. A ternary Hebbian rule: for each GIE neuron i, if the neuron fired (h_new ≠ h_old) and the VDB mismatch indicates the current prediction was wrong, flip the sign of that neuron's weight for the current input trit. ("Neurons that fire together, wire together" — in ternary.)
3. The LP core re-encodes the affected GDMA descriptor with the updated weight vector. The GDMA circular chain naturally picks up the new descriptor on its next pass.

**Why this is last:** Hebbian weight updates are persistent and non-reversible within a session. A bad update during a noisy period could corrupt a signature and degrade accuracy. All preceding pillars (kinetic attention, dynamic scaffolding) provide the diagnostic infrastructure needed to detect when a weight update is beneficial vs. harmful. Gate bias (Phase 5) should be validated first because it provides a reversible, lower-risk path to experience-dependent behavior change.

**The biological analog:** This is where CLS consolidation would happen. The VDB (hippocampus) trains the GIE weights (neocortex) through repeated retrieval under consistent conditions. Once the weights are trained, the VDB becomes less necessary for P1/P2 discrimination — the CfC projection will no longer collapse them. The VDB then shifts to encoding novel states rather than compensating for known degeneracies.

**Impact:** The system that "learns to walk" without a training loop, a GPU, or a floating-point number. Weights that reflect accumulated live experience rather than a 30-second initialization window.

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
    └── Pillar 3: Hebbian GIE
            (needs kinetic attention as the behavioral signal for weight updates)

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
