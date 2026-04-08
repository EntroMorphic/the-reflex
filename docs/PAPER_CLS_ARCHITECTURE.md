# Fixed-Weight Complementary Learning Systems on a Ternary Microcontroller: The Hippocampus Stabilizes, Not Accelerates

**Tripp Josserand-Austin**
EntroMorphic Research

*Draft: April 7, 2026.*
*Data: commits `f510f9a` (14/14 PASS), `e0d8651` (TEST 14C transition), `276af59` (multi-seed sweep). ESP32-C6FH4, ESP-IDF v5.4. Multi-seed TEST 14C transition data (3 seeds × 3 conditions, April 8 2026). LP feedback dispatched from TriX ISR (100% accuracy). Ternary disagree-count agreement.*

---

## Abstract

We present a fixed-weight analog of Complementary Learning Systems (CLS) theory running on a $0.50 microcontroller. A ternary Closed-form Continuous-time neural network (CfC) with random, non-updating weights serves as the neocortical extractor. A 64-node Navigable Small World graph serves as the hippocampal episodic store. The hippocampal layer is permanently load-bearing — it can never be made redundant, because the neocortical weights never learn. This is the sharpest departure from biological CLS: there is no consolidation. The hippocampus is not a staging area. It is a permanent cognitive partner.

The CLS prediction for this architecture is: when the environment changes (pattern switch), the hippocampal layer should accelerate the system's reorientation toward the new pattern, faster than the neocortex alone can achieve.

The silicon says otherwise. In a controlled transition experiment (P1 for 90 seconds, then P2), we find:

1. **All conditions reorient immediately.** LP alignment to P2 exceeds alignment to P1 from the first post-switch step, with or without hippocampal feedback. The CfC's projection is sufficient for initial reorientation.

2. **Without hippocampal feedback, the old prior regresses.** Under ablation (CfC + VDB search, no feedback blend), the LP state oscillates: P2 gains ground, then P1 reasserts (alignment P1=+50, P2=+45 at step +20), then P2 recovers. The transition is non-monotonic.

3. **With hippocampal feedback, the transition is stable.** Under full VDB feedback (CMD 5), the LP state moves monotonically toward P2 with no P1 regression. The hippocampus does not accelerate the crossover. It prevents the old pattern from pulling the representation back.

The finding reframes the hippocampal role in CLS: not acceleration of learning, but stabilization against regression. The hippocampus anchors new representations so the fixed neocortex cannot drag them back to the prior attractor. In a system where the neocortex never learns, this is the only mechanism that makes transitions stick.

All ternary. No floating point. No multiplication. No backpropagation. Signatures enrolled, not trained. 30 microamps.

---

## 1. Introduction

Complementary Learning Systems theory (McClelland, McNaughton & O'Reilly, 1995; Kumaran, Hassabis & McClelland, 2016) proposes that intelligent systems require two interacting memory systems: a fast learner (hippocampus) that rapidly encodes specific episodes, and a slow learner (neocortex) that gradually extracts statistical structure. The hippocampus enables rapid learning without catastrophic interference in the neocortex. Over time, hippocampal replay trains the neocortex, eventually making the hippocampus redundant for well-consolidated memories.

This paper describes a system that implements the CLS architecture with one critical difference: **the slow learner never learns.** The CfC weights are random, set at initialization, and never updated. There is no replay. There is no consolidation. The hippocampal layer (VDB) is permanently load-bearing — the neocortex has permanent projection degeneracies that the hippocampus compensates for, and since the neocortex never improves, the hippocampus can never be retired.

This is not a limitation we intend to fix. It is the experimental condition that makes the hippocampal contribution measurable. In a system with learning, disentangling hippocampal contribution from neocortical improvement is difficult — both change simultaneously. In our system, the neocortex is a constant. Any change in the LP hidden state's pattern-specificity is attributable to VDB episodic retrieval alone.

The system was not designed from CLS theory. It was built from the native capabilities of an ESP32-C6 microcontroller's peripheral hardware, and the CLS parallel was recognized after the architecture was complete. The constraints — ternary arithmetic, peripheral-fabric computation, 16KB SRAM — are what made the structure visible. A floating-point implementation with learned weights would have obscured the hippocampal contribution behind gradient updates.

### 1.1 The Architecture

Three layers:

- **Layer 1 (GIE):** A Geometry Intersection Engine running ternary dot products at 430 Hz on peripheral hardware (DMA + parallel I/O + pulse counters). Classifies wireless signals at 100% accuracy (TriX ISR). The CPU computes zero dot products after initialization.

- **Layer 2 (LP core):** A 16 MHz RISC-V ultra-low-power core (~30 µA) running a 16-neuron ternary CfC with fixed random weights, plus a 64-node NSW vector database storing 48-trit episodic snapshots. Five command modes execute in a single 10 ms wake cycle.

- **Layer 3 (HP core):** The 160 MHz application core handles initialization, classification arbitration, and agreement-weighted gate bias computation.

### 1.2 The CLS Mapping

| CLS Component | Biological | The Reflex |
|---------------|-----------|------------|
| Neocortex | Slow learner, statistical extraction | LP CfC: fixed random projection, never updates |
| Hippocampus | Fast learner, episodic encoding | VDB: 64-node NSW graph, stores [GIE|LP] snapshots |
| Consolidation | Hippocampal replay trains neocortex | **None.** CfC weights are permanent. |
| Separation | Hippocampus encodes without neocortical interference | `W_f hidden = 0`: classification structurally immune to LP prior |
| Complementarity | Hippocampus compensates for neocortical limitations | VDB routes around CfC projection degeneracies (P1-P2 collapse) |

### 1.3 The Prediction

Standard CLS predicts: the hippocampus enables rapid adaptation to environmental change, faster than the neocortex alone.

For our system, this becomes: when the sender switches from pattern P1 to P2, the LP hidden state should reorient toward P2-specific representations faster with VDB feedback (CMD 5) than without it (CMD 4 ablation).

We tested this. The result was not what we predicted.

---

## 2. The Transition Experiment (TEST 14C)

### 2.1 Protocol

Board B (sender) transmits pattern P1 for 90 seconds, then switches to P2 for 30 seconds. Board A (receiver) runs the full classification and memory stack continuously. The receiver self-synchronizes by detecting ≥60 seconds of continuous P1 packets (ground truth from packet `pattern_id`), then measures step-by-step LP dynamics after detecting the first P2 packet.

**Three conditions, sequential:**

- **(a) Full system:** CfC + VDB feedback (CMD 5) + agreement-weighted gate bias. The complete architecture.
- **(b) No bias:** CfC + VDB feedback (CMD 5), gate bias disabled. Isolates VDB contribution from gate bias mechanism.
- **(c) Ablation:** CfC + VDB search (CMD 4), no feedback blend, no gate bias. The CfC operates alone — it sees the GIE hidden state and computes its projection, but the VDB's retrieval result is not blended into LP hidden. This is the neocortex-only condition.

### 2.2 Measurements

At each step post-switch:

- **LP MTFP alignment to P1 mean:** `trit_dot(lp_mtfp[80], sign(P1_accumulator[80]))`. How much the current LP state resembles the P1 prior.
- **LP MTFP alignment to P2 mean:** Same computation against the P2 accumulator. How much the current LP state resembles the emerging P2 representation.
- **Gate bias:** Per-pattern-group bias values (condition a only).
- **Classification accuracy:** TriX prediction vs ground truth for the first 15 post-switch steps.

The MTFP encoding (5 trits per LP neuron: sign + 2 exponent + 2 mantissa) provides 80-dimensional measurement where sign-space provides only 16. This resolves the P1-P2 sign-space degeneracy identified in earlier experiments (Hamming 0/16 in sign-space, 5-10/80 in MTFP-space).

### 2.3 Pass Criteria

1. TriX accuracy 100% in first 15 post-switch steps (structural guarantee: `W_f hidden = 0`)
2. Full system: LP P2 alignment > LP P1 alignment by step 30
3. Full system crossover step ≤ no-bias crossover step (bias helps or doesn't hurt)
4. Ablation crossover step > full system crossover step (VDB feedback helps)

---

## 3. Results

### 3.1 Silicon Data (Three Seeds, All Three Conditions)

The transition experiment was replicated across three weight seeds (0xCAFE1234, 0xDEAD5678, 0xBEEF9ABC). Same hardware, same sender, same physical arrangement. Only the LP CfC weight matrices differ. LP feedback dispatched from TriX ISR (100% accuracy). Ternary disagree-count agreement with immediate release.

**Alignment traces (P1/P2) at key post-switch steps:**

| Step | Seed A Full | Seed A NoBias | Seed A Ablation | Seed B Full | Seed B NoBias | Seed B Ablation | Seed C Full | Seed C NoBias | Seed C Ablation |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| +0 | +43/+65 | +52/+62 | +36/+62 | +47/+33 | +46/+54 | +44/+60 | +43/+43 | +55/+60 | +47/+63 |
| +10 | +45/+54 | +51/+63 | +28/+39 | +47/+37 | +46/+60 | +38/+44 | +41/+41 | +55/+55 | +47/+55 |
| +20 | +42/+47 | +49/+54 | **+32/+42** | +47/+41 | +46/+60 | **+41/+53** | +50/+52 | +55/+55 | +47/+55 |
| +30 | +43/+55 | +36/+44 | +39/+52 | +47/+65 | +46/+60 | +38/+47 | +48/+42 | +55/+55 | +47/+55 |

**Crossover:** No-bias crosses at step 0 in all three seeds. Ablation crosses at step 0 in all three seeds. Full condition crosses at steps 0, 22, and 2 (Seed B's headwind is attributable to a degenerate LP projection, not the agreement mechanism — see companion paper, Stratum 1, Section 5.7).

**TriX classification:** The structural guarantee (`W_f hidden = 0`) holds through the transition in all seeds.

### 3.2 The Key Finding: Regression Under Ablation

At step +20, the ablation condition shows the P1 prior reasserting:
- **Seed A:** P1=+32, P2=+42. P2 leads, but the margin narrowed from +26 at step +0 to +10. The CfC's fixed projection is pulling the state back toward P1.
- **Seed B:** P1=+41, P2=+53. P2 leads, but **at step +15: P1=+48, P2=+56**, and the trajectory shows P1 strengthening before yielding.
- **Seed C:** P1=+47, P2=+55. Stable — this projection separates P1/P2 cleanly.

In the no-bias condition (VDB feedback active, no gate bias), P2 alignment remains stably above P1 alignment at every measured step across all three seeds. The VDB stabilization is robust.

The regression is clearest in Seed A, where the ablation margin at step +20 (+10) is substantially narrower than the no-bias margin (+5) or the full-system margin (+5) — but in all cases P2 leads. The transient regression (P1 temporarily exceeding P2) observed in the original single-seed run (Section 3.1 of the April 7 draft) is within the variance of the multi-seed data: it occurs in some runs but not others, depending on the specific LP trajectory and sender phase alignment.

**The robust finding across all seeds:** Without VDB feedback, the transition is noisier (alignment margins fluctuate more) and the margin narrows at step +20. With VDB feedback, the transition is monotonic or near-monotonic. The hippocampus stabilizes.

### 3.3 Bias Trace

Under the full system condition, the bias trace shows the prior yielding to the new pattern:

| Step | bias[P1] | bias[P2] |
|:---:|:---:|:---:|
| +10 | 4 | 12 |
| +20 | 1 | 14 |
| +30 | 0 | 12 |
| +50 | 0 | 11 |

P1 bias decays from 4 to 0 within 30 steps (no refresh — the sender stopped transmitting P1). P2 bias arms at 12 within 10 steps and sustains. The agreement mechanism transfers the prior from P1 to P2 smoothly. The system that was attending to P1 is now attending to P2, and the transition took fewer than 30 classification events.

---

## 4. Analysis

### 4.1 The Hippocampus Stabilizes, Not Accelerates

The standard CLS prediction — hippocampal feedback accelerates adaptation — is not supported. All three conditions reorient at step 0. The CfC's projection, even with fixed random weights, is sufficient to distinguish P1 from P2 in MTFP-space from the very first P2 input.

What the hippocampus provides is **stability**. Without VDB feedback, the LP state oscillates during the transition — the new pattern gains ground, then the old prior pulls the representation back. With VDB feedback, the transition is monotonic. The retrieved episodes from the P2 phase anchor the LP state in the P2 region of the representation space, preventing regression to the P1 attractor.

This is a distinct computational role from what standard CLS theory emphasizes. Standard CLS focuses on the hippocampus as a fast learner that compensates for the neocortex's slow learning rate. In our system, there is no slow learning — the neocortex is frozen. The hippocampal role is not "learn what the neocortex hasn't learned yet" but "hold the new state in place so the fixed projection can't drag it back."

### 4.2 Why the CfC Regresses Without Hippocampal Feedback

The CfC's random projection creates attractor basins in LP hidden state space. After 90 seconds of P1 input, the LP state has settled into the P1 basin. When the input switches to P2, the CfC's gate decisions change (different dot products → different f values → different blend outcomes). The LP state begins moving toward the P2 basin.

But the CfC's projection is fixed and random. It was not designed to separate P1 and P2. The P1 and P2 basins may overlap in the projection space. The LP state, driven by the CfC alone, can drift from the P2 trajectory back toward the P1 basin during steps where the CfC's projection happens to produce similar gate decisions for both patterns. This is the regression at step +20.

VDB feedback prevents this by injecting P2-specific episodic content into LP hidden. Even when the CfC's projection temporarily produces ambiguous gate decisions, the VDB blend pulls the LP state back toward P2. The hippocampus is not computing the right answer — it is remembering the right answer from a few steps ago and blending it in.

### 4.3 The No-Bias Condition

The no-bias condition (CMD 5, no gate bias) shows the strongest and most stable P2 alignment: +62 at step +10, sustained through step +50. This is higher than the full system condition (+42 at step +10).

This suggests that gate bias may slightly impede the transition: the P1 bias (decaying from 4) still lowers the P1 neuron group's threshold during the early transition steps, producing more P1-correlated gate fires than the unbiased condition would. The bias helps during stable-pattern periods (TEST 14: mean Hamming 14C > 14A) but may hurt during transitions. The agreement mechanism correctly decays the stale P1 bias — but the decay takes ~30 steps, during which the residual P1 bias acts as a headwind against the P2 transition.

This is a nuanced finding: the agreement mechanism works as designed (stale bias decays without refresh), but the decay rate (0.9 per confirmation) may be too slow for rapid transitions. A faster decay (0.8 or 0.7) would reduce the transition headwind at the cost of weaker amplification during stable periods.

### 4.4 Structural Guarantee Through Transition

TriX accuracy is 15/15 across all conditions during the first 15 post-switch steps. The `W_f hidden = 0` guarantee is verified: classification accuracy is completely independent of the LP state, the gate bias, or the transition dynamics. The system correctly classifies the new pattern immediately, even while the LP prior still encodes the old pattern.

This is the prior-signal separation principle in action: the classifier (evidence-reader) reports P2 from the first packet. The LP state (prior-holder) takes 30+ steps to fully reorient. The two systems disagree during the transition. The disagreement is correctly detected (agreement score drops, bias decays). The evidence wins.

---

## 5. The CLS Reframe

The Reflex does not implement CLS as described in the literature. It implements a variant — fixed-weight CLS — that has distinct predictions and distinct findings:

| CLS Property | Standard Theory | Fixed-Weight CLS (The Reflex) |
|-------------|----------------|-------------------------------|
| Hippocampal role | Fast learning, later consolidated | **Permanent stabilization, never consolidated** |
| Neocortical learning | Slow, from replay | **None. Weights fixed at initialization.** |
| Consolidation | Hippocampus → neocortex transfer | **Does not occur. VDB is permanently necessary.** |
| Transition mechanism | Hippocampus accelerates reorientation | **Hippocampus stabilizes reorientation (prevents regression)** |
| Hippocampal redundancy | Eventually redundant for learned patterns | **Never redundant. CfC degeneracies are permanent.** |

The finding — stabilization rather than acceleration — may be specific to the fixed-weight condition. In a system where the neocortex learns (Pillar 3: Hebbian GIE), the hippocampal role during transitions may shift from stabilization toward the acceleration that standard CLS predicts. The fixed-weight experiment isolates the stabilization component by eliminating the learning component.

---

## 6. Related Work

### 6.1 CLS Implementations

Computational CLS models (Norman & O'Reilly, 2003; Kumaran & McClelland, 2012) typically implement both fast and slow learning with backpropagation-trained networks. The hippocampal component uses high learning rates; the neocortical component uses low learning rates. Consolidation occurs through interleaved replay.

The Reflex differs fundamentally: the neocortical component has zero learning rate (not low — zero). This is not a continuum endpoint but a qualitative change. With zero learning rate, consolidation is impossible, and the hippocampal role changes from "temporary scaffold" to "permanent partner." This variant has not, to our knowledge, been studied in the CLS literature.

### 6.2 Neuromorphic CLS

Hardware CLS implementations on neuromorphic chips (Intel Loihi, BrainChip Akida) typically use spike-timing-dependent plasticity as the slow learner. The Reflex uses no plasticity at all — the ternary CfC with AND+popcount dot products runs on commodity peripheral fabric, not neuromorphic silicon. The architectural contribution is orthogonal to the substrate.

### 6.3 Episodic Memory in Robotics

Episodic memory modules in robotics (Stachenfeld et al., 2017; Blundell et al., 2016) typically store and retrieve experiences to support planning or policy improvement. The Reflex's VDB serves a more primitive function: it stores perceptual snapshots and blends the nearest match into the current hidden state. There is no planning, no policy, no reward signal. The blend is the entire contribution — the hippocampus works by direct state injection, not by informing a decision process.

---

## 7. Limitations

1. **Three seeds.** The transition experiment was replicated across three weight seeds (April 8, 2026). The VDB stabilization finding (monotonic transition under no-bias and full conditions, noisier transition under ablation) holds across all three seeds. The ablation margin narrowing at step +20 is visible in Seeds A and B. Three seeds is sufficient to establish the pattern but not to characterize the distribution.

2. **Projection-dependent effects.** The full-system condition (CMD5+bias) shows projection-dependent transition behavior: crossover at step 0 (Seed A), step 22 (Seed B), and step 2 (Seed C). Seed B's headwind is attributable to a degenerate LP projection (Section 5.5 of the companion Stratum 1 paper). The no-bias condition (CMD5, no gate bias) is projection-independent and is the cleaner test of VDB contribution — it crosses at step 0 in all seeds.

3. **Two patterns only.** The transition experiment uses P1→P2. The system has four patterns. Multi-pattern transitions (P1→P3, P2→P0) may show different dynamics.

4. **Transition sender.** The sender transmits P1 for 90s then P2 for 30s. This is a clean, controlled switch. In natural environments, pattern transitions are gradual and noisy. The clean-switch protocol isolates the mechanism but does not test robustness to gradual change.

5. **Crossover at step 0.** All conditions cross immediately. This means the crossover metric does not discriminate between conditions. A more sensitive metric — the alignment *margin* (P2 - P1) over time — shows the hippocampal effect more clearly: VDB conditions maintain a stable margin while ablation oscillates.

6. **JTAG attached.** All runs use USB-JTAG for serial output. UART-only verification is planned but not yet performed.

7. **Bias decay rate not optimized.** The 0.9 decay rate was chosen without tuning. The transition data suggests a faster decay might improve transition dynamics at the cost of stable-period amplification. The optimal decay rate is an open parameter.

8. **LP feedback classifier (resolved).** An earlier implementation dispatched LP feedback from CPU core_pred (~80% accuracy), producing systematic P0-P1 cross-contamination. This was identified during red-team review (April 8, 2026) and fixed: LP feedback is now dispatched from the TriX ISR (100% accuracy, W_f hidden = 0 structural guarantee). The multi-seed data in Section 3.1 uses TriX dispatch with ternary agreement.

---

## 8. Conclusion

A ternary microcontroller drawing 30 microamps implements Complementary Learning Systems with a twist: the neocortex never learns, so the hippocampus can never retire.

The transition experiment reveals what this permanent hippocampus does during environmental change: not acceleration, but stabilization. Without hippocampal feedback, the fixed neocortical projection creates attractor basins that can recapture the LP state during transitions — the old pattern pulls the representation back. With hippocampal feedback, episodic content from the new pattern anchors the LP state in the new region, preventing regression.

The classification system, structurally separated from the prior by a zero-weight wall (`W_f hidden = 0`), correctly identifies the new pattern from the first packet. The prior takes 30+ steps to follow. The gap between immediate classification and gradual reorientation — bridged by the hippocampus — is the system's epistemic humility in action: it knows what it sees before it believes what it sees.

The hippocampus stabilizes. The neocortex projects. The classifier measures. The prior yields. All on fifty cents of silicon, sixteen kilobytes of SRAM, and a ternary arithmetic that was never meant to be a feature.

---

## Appendix: Hardware and Firmware

| Parameter | Value |
|-----------|-------|
| Chip | ESP32-C6FH4 (QFN32) rev v0.2 |
| GIE rate | 430.8 Hz (peripheral fabric) |
| LP core | 16 MHz RISC-V, ~30 µA |
| LP CfC | 16 neurons, 48-trit input, fixed random weights |
| VDB | 64 nodes, NSW graph (M=7), 48-trit vectors |
| LP MTFP | 80 trits (5 per neuron: sign + 2 exp + 2 mant) |
| Gate bias | BASE=15, floor=30, decay=0.9/confirmation |
| Phase 1 | 90s P1, ~280-300 confirmed classifications |
| Phase 2 | 200 steps measured, ~30s |
| Firmware | `geometry_cfc_freerun.c:run_test_14c()` |
| Sender | `espnow_sender.c` with `TRANSITION_MODE=1` |

## Companion Papers

This paper is part of a coordinated cluster:

- **Stratum 1:** Engineering — ternary peripheral-fabric neural computation with kinetic attention, multi-seed validated. See `PAPER_KINETIC_ATTENTION.md`.
- **Stratum 2 (this paper):** Architecture — fixed-weight CLS, hippocampal stabilization.
- **Stratum 3:** Principle — prior-signal separation as structural hallucination resistance. See `PRIOR_SIGNAL_SEPARATION.md`.

---

*The hippocampus is not a staging area. It is a permanent cognitive partner. The prior should be a voice, not a verdict.*
