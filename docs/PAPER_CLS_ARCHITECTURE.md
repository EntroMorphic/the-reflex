# Fixed-Weight Complementary Learning Systems on a Ternary Microcontroller: The Hippocampus Is the Model

**Tripp Josserand-Austin**
EntroMorphic Research

*Rewritten: April 12, 2026. The original title was "The Hippocampus Stabilizes, Not Accelerates." The April 9-12 session tested the consolidation path (Hebbian weight learning) and found it produces no improvement. The hippocampus is not a stabilizer of neocortical learning. It IS the temporal model. The title and framing have been revised to reflect this empirical finding.*

*Data: `data/apr11_2026/SUMMARY.md` (authoritative label-free dataset). `data/apr11_2026/hebbian_3reps_label_free.log` (Hebbian replication). Firmware commit `ebc65a4`. ESP32-C6FH4, ESP-IDF v5.4.*

---

## Abstract

We present a fixed-weight analog of Complementary Learning Systems (CLS) theory running on a $0.50 microcontroller. A ternary Closed-form Continuous-time neural network (CfC) with random, non-updating weights serves as the neocortical extractor. A 64-node Navigable Small World graph serves as the hippocampal episodic store. The system classifies 4 wireless signal patterns at 100% label-free accuracy in peripheral hardware at 430 Hz, drawing approximately 30 microamps.

The hippocampal layer is permanently load-bearing. We know this not because we chose not to implement consolidation, but because **we implemented it and it produced no effect.** Three iterations of Hebbian weight learning — with VDB-mismatch targets, TriX-accumulator targets, and diagnosed f-vs-g pathway selection — were tested under ablation-controlled, replicated conditions. Result: +0.1 ± 1.1 Hamming at n=3. The neocortex does not learn from the hippocampus.

And yet the hippocampus works. The VDB episodic retrieval and CfC feedback blend, with no weight learning, produces 8.5-9.7/80 MTFP divergence across 4 patterns — rich, pattern-discriminative temporal representations from episodic memory alone. The VDB is causally necessary: ablation (CMD 4, CfC without VDB blend) collapses pattern pairs that CMD 5 (CfC + VDB blend) separates.

This is the sharpest departure from biological CLS: there is no consolidation, not because we omitted it, but because the system doesn't need it. The hippocampus is not a staging area. It is not a scaffold. It is the temporal model itself.

All ternary. No floating point. No multiplication. No backpropagation. Signatures enrolled, not trained. 30 microamps.

---

## 1. Introduction

Complementary Learning Systems theory (McClelland, McNaughton & O'Reilly, 1995; Kumaran, Hassabis & McClelland, 2016) proposes that intelligent systems require two interacting memory systems: a fast learner (hippocampus) that rapidly encodes specific episodes, and a slow learner (neocortex) that gradually extracts statistical structure. The hippocampus enables rapid learning without catastrophic interference in the neocortex. Over time, hippocampal replay trains the neocortex, eventually making the hippocampus redundant for well-consolidated memories.

This paper describes a system that implements the CLS architecture with one critical difference: **the slow learner never learns — and we tested what happens when it does.**

The CfC weights are random, set at initialization, and never updated during normal operation. There is no replay. There is no consolidation. The hippocampal layer (VDB) is permanently load-bearing.

Unlike prior fixed-weight CLS demonstrations, we do not merely omit consolidation and argue theoretically that the hippocampus is necessary. We implemented the consolidation path — Hebbian LP weight updates from VDB mismatch and TriX-classifier targets — and measured its contribution under controlled, replicated conditions. The result: no measurable improvement over the hippocampus-only baseline. The zero learning rate is not a design choice. It is the empirically correct operating point.

### 1.1 The Architecture

Three layers, operating on genuine wireless signals from an ESP-NOW sender:

- **Layer 1 (GIE):** A Geometry Intersection Engine running ternary dot products at 430 Hz on peripheral hardware (GDMA → PARLIO → PCNT loopback). Classifies wireless signals at 100% label-free accuracy (TriX ISR, structural guarantee: `W_f hidden = 0`). The CPU computes zero dot products after initialization.

- **Layer 2 (LP core):** A 16 MHz RISC-V ultra-low-power core (~30 µA) running a 16-neuron ternary CfC with fixed random weights, plus a 64-node NSW vector database storing 48-trit episodic snapshots `[gie_hidden(32) | lp_hidden(16)]`. Five command modes execute in a single 10 ms wake cycle.

- **Layer 3 (HP core):** The 160 MHz application core handles initialization, ESP-NOW reception, classification arbitration, and test orchestration.

Label-free operation: the pattern_id field from the sender's ESP-NOW payload is masked from both the TriX enrollment signatures and the GIE runtime input (`MASK_PATTERN_ID=1`, `MASK_PATTERN_ID_INPUT=1`). Classification uses only payload bytes and inter-packet timing features. The GIE hidden state, VDB nodes, and LP CfC input contain no label information.

### 1.2 The CLS Mapping

| CLS Component | Biological | The Reflex |
|---|---|---|
| Neocortex | Slow learner, statistical extraction | LP CfC: fixed random projection, never updates |
| Hippocampus | Fast learner, episodic encoding | VDB: 64-node NSW graph, stores [GIE\|LP] snapshots |
| Consolidation | Hippocampal replay trains neocortex | **Tested. No effect.** Three Hebbian iterations: +0.1 ± 1.1 (n=3) |
| Separation | Hippocampus encodes without neocortical interference | `W_f hidden = 0`: classification structurally immune to LP prior |
| Complementarity | Hippocampus compensates for neocortical limitations | VDB routes around CfC projection degeneracies |

### 1.3 The Prediction and What We Found

Standard CLS predicts: the hippocampus enables rapid adaptation to environmental change, and consolidation (hippocampal replay → neocortical weight updates) eventually makes the hippocampus redundant.

For our system: the hippocampus does enable pattern-discriminative LP states (8.5-9.7/80 MTFP divergence, VDB causally necessary). But consolidation — Hebbian LP weight updates from VDB and TriX signals — does not improve on the hippocampus-only baseline. The hippocampus is not a scaffold for learned representations. It IS the representation.

---

## 2. The VDB Is the Temporal Model

### 2.1 The Measurement: MTFP Divergence

The LP CfC computes 16 integer dot products per step, one per neuron. These dot products are the LP's representation of the current input. The sign-quantized version (`lp_hidden[16]`, taking sign of each dot) is a 16-trit ternary vector. The MTFP-encoded version (5 trits per dot: sign + 2 exponent + 2 mantissa) is an 80-trit vector that preserves magnitude information.

The distinction matters. Sign-space collapses the magnitudes, producing LP divergence of 1.2-2.3/16 across 4 patterns. MTFP-space preserves them, producing 8.5-9.7/80 — a 4-5× richer measurement that correctly identifies effects that sign-space hides or misreports (see Section 4.2).

All divergence measurements in this paper use MTFP-space unless otherwise noted.

### 2.2 Causal Necessity (TEST 13)

CMD 4 (CfC step + VDB search, NO feedback blend): the LP CfC runs its projection but does not receive VDB retrieval results. CMD 5 (CfC step + VDB search + feedback blend): the LP CfC receives and blends the nearest VDB match into `lp_hidden`.

In paired 120-second runs on the authoritative label-free dataset (`data/apr11_2026/full_suite_label_free_final.log`):

- CMD 4 (neocortex only): P1-P2 sign Hamming = 1. The CfC alone cannot reliably separate these patterns.
- CMD 5 (neocortex + hippocampus): P1-P2 sign Hamming = 2, MTFP Hamming = 9. The VDB feedback blend enables the separation.

VDB feedback contribution: +1 trit P1-P2 in sign-space. This is the distillation test. The VDB is causally necessary for LP divergence.

### 2.3 The Hippocampal Representation (TEST 12)

With VDB feedback (CMD 5), 120 seconds of live wireless input from 4 cycling patterns:

MTFP divergence matrix (/80):
```
     P0  P1  P2  P3
P0:   0   5   6   8
P1:   5   0   9  13
P2:   6   9   0  10
P3:   8  13  10   0
```

Mean MTFP divergence: 8.5/80. All 6 pairs separated. P3 (ramp pattern: 10 Hz, incrementing payload) is the most distinctive (Hamming 8-13 vs other patterns). P0-P1 (both 10 Hz, similar payloads) is the least distinctive but still measurable (Hamming 5).

This is the hippocampal representation: 80-dimensional ternary temporal states that accumulate from episodic memory retrieval over 120 seconds of live operation. No weight learning. No labels. No training. Just store, retrieve, blend.

---

## 3. Consolidation Tested: Three Iterations of Hebbian Learning

### 3.1 The Consolidation Hypothesis

CLS theory predicts that hippocampal replay should train the neocortex, improving its projection over time. In our system, this translates to: VDB retrieval results should drive LP weight updates (Hebbian learning), improving LP divergence beyond what VDB feedback blend alone provides.

We tested this across three iterations, each addressing a limitation of the previous one:

### 3.2 Iteration 1: VDB Mismatch Target, f-Pathway Only

**Error signal:** Per-trit disagreement between VDB best match's LP portion and current `lp_hidden`.
**Update rule:** For each LP neuron with error, flip one W_f weight that contributed to the current f_dot direction.
**Gating:** Retrieval stability (same top-1 for K=5 consecutive steps) + TriX agreement + rate limiting (100ms).

**Result (ablation-controlled, single run):**
- With label in input: +2.5 Hamming over control (label-dependent — the VDB error signal was exploiting pattern_id leaked through the GIE hidden state)
- With label removed (`MASK_PATTERN_ID_INPUT=1`): -1.7 Hamming (harmful)

**Diagnosis:** The VDB mismatch target was label-informed. When the label leak was closed, the error signal was too noisy for directed learning.

### 3.3 Iteration 2: TriX Accumulator Target, f-Pathway Only

**Error signal:** Per-trit disagreement between TriX-labeled accumulator (population mean of LP states per pattern, 100% accurate structural guarantee) and current `lp_hidden`.

**Result (genuinely label-free):** -1.0 Hamming. Less harmful than VDB mismatch but still negative.

**Diagnosis:** The target was clean (TriX labels are structurally guaranteed). But ~50% of errors were in the g-pathway (candidate direction), and flipping W_f for a g-pathway error was counterproductive.

### 3.4 Iteration 3: TriX Accumulator Target, Diagnosed f+g Pathway

**Diagnosis added:** Per neuron, compare |f_dot| vs |g_dot|. Fix the pathway with the smaller dot (cheaper to reverse). If f_dot = 0 (gate held, should have fired): push f_dot toward the direction that would produce the correct output given current g.

**Result (single run, genuinely label-free):** +1.3 Hamming. Promising.

**Result (3 repetitions, genuinely label-free):**
```
Control (CMD5 only):   1.0 ± 1.3 /16 (sign)    9.7 ± 0.6 /80 (MTFP)
Hebbian (CMD5+learn):  1.1 ± 0.8 /16 (sign)    9.7 ± 1.0 /80 (MTFP)
Contribution:          +0.1 ± 1.2 /16           +0.1 ± 1.1 /80
```

**+0.1 ± 1.1 at n=3. Indistinguishable from zero.** The diagnosis fixed the direction (harmful → neutral) but the mechanism produces no improvement over VDB-only.

### 3.5 Why Consolidation Doesn't Help

The LP CfC has 16 neurons with 48-trit random ternary weights. Each neuron's sign output is determined by the sign of a dot product over ~29 non-zero weights. The output space is 2^16 = 65,536 possible sign vectors. The MTFP space has richer structure (80 trits), but the underlying information is still 16 dot products.

The Hebbian rule flips one weight per neuron per update, changing the dot product by ±2. With ~29 non-zero weights per neuron and only a sign-quantized (or 5-trit MTFP) output, many weight configurations produce the same output. The optimization landscape is flat — single-trit flips are random walks in a space with vast equivalence classes.

The VDB feedback blend, by contrast, directly injects episodic content into `lp_hidden`. It doesn't need to find the right weights — it bypasses the weights entirely, writing pattern-specific state into the hidden vector. The blend is a more direct mechanism than weight learning for producing pattern-discriminative LP states when the weights are random and the output space is coarsely quantized.

**This is the empirical finding: at 16 neurons with random ternary weights, direct episodic injection (VDB blend) is more effective than weight learning (Hebbian) for producing pattern-discriminative temporal states.**

---

## 4. Honest Negatives

### 4.1 Kinetic Attention Is Harmful at MTFP Resolution

Agreement-weighted gate bias (Phase 5: LP prior biases GIE gate thresholds) was tested under label-free conditions. Three runs of Test 14:

| Run | 14A (no bias) MTFP | 14C (full bias) MTFP | MTFP improvement |
|---|---|---|---|
| 1 | 9.8/80 | 10.2/80 | +0.4 |
| 2 | 15.5/80 | 8.5/80 | -7.0 |
| 3 | 15.5/80 | 5.7/80 | -9.8 |
| **Mean** | **13.6** | **8.1** | **-5.5** |

The gate bias consistently reduces MTFP divergence. Root cause: lowering a neuron group's gate threshold fires more GIE neurons, saturating the GIE hidden state and making the LP input more uniform across patterns. The bias makes the GIE LESS discriminative for the LP.

The sign-space metric (+1.3/16 mean) incorrectly showed kinetic attention as helpful. The sign-space improvement was an artifact: the bias traded magnitude diversity (which MTFP captures) for sign diversity (which sign-space captures). A net information loss visible only at the richer MTFP resolution.

This finding is reported honestly. The mechanism fires (per-group fire rate shift >10% every run). The effect on LP representation is negative.

### 4.2 The Sign-Space Metric Can Mislead

The kinetic attention finding illustrates a general measurement issue. Sign-space LP divergence (Hamming over 16 sign trits) and MTFP LP divergence (Hamming over 80 MTFP trits) can give opposite results for the same data. In Run 2 of the kinetic attention experiment:

- Sign-space 14A: 0.0/16 (all patterns identical in sign-space)
- Sign-space 14C: 4.2/16 (bias "created divergence" from nothing)
- MTFP 14A: 15.5/80 (patterns well-separated in magnitude)
- MTFP 14C: 8.5/80 (bias crushed the magnitude diversity)

The LP states had identical signs but different magnitudes. Sign-space saw them as identical. MTFP saw them as well-separated. The bias shuffled the signs (creating sign-space divergence) while crushing the magnitudes (destroying MTFP divergence).

The MTFP metric is both richer (80 dimensions vs 16) and more stable (coefficient of variation 6% vs 39%). All divergence claims in this paper use MTFP.

---

## 5. The CLS Reframe

### 5.1 The Permanent Hippocampus

The Reflex does not implement CLS as described in the literature. It implements a variant — permanent-hippocampus CLS — with empirically distinct properties:

| CLS Property | Standard Theory | Permanent-Hippocampus CLS (The Reflex) |
|---|---|---|
| Hippocampal role | Fast learning, later consolidated | **Permanent temporal model** |
| Neocortical learning | Slow, from replay | **None. Tested: +0.1 ± 1.1 (noise)** |
| Consolidation | Hippocampus → neocortex transfer | **Tested. Does not occur at 16 neurons.** |
| Hippocampal redundancy | Eventually redundant | **Never redundant. VDB IS the model.** |
| Temporal representation | Neocortical (learned) | **Hippocampal (episodic, 9.7/80 MTFP)** |

### 5.2 When Is the Hippocampus Sufficient?

The hippocampus-only path works when:
- The neocortical projection (CfC with random weights) provides a ROUGH representation — not perfect separation, but enough for the VDB to route around degeneracies
- The VDB capacity (64 nodes in 2KB) is sufficient for the number of patterns (4 in this experiment)
- The VDB feedback blend can directly inject pattern-specific content without needing the weights to be correct

It may not be sufficient when:
- The number of patterns exceeds VDB capacity
- The neocortical projection is SO degenerate that even VDB retrieval returns wrong matches
- The task requires generalization beyond stored episodes (abstraction, not just retrieval)

### 5.3 Implications for CLS Theory

The "consolidation makes the hippocampus redundant" prediction assumes that the neocortex can eventually learn what the hippocampus knows. In our system, this assumption fails: 16 ternary neurons with ~29 non-zero weights each do not have enough degrees of freedom for Hebbian learning to find useful structure. The hippocampus produces 9.7/80 MTFP divergence through direct episodic injection; the best the Hebbian rule can do with weight updates is 9.7/80 (identical to the control).

This suggests a condition under which consolidation fails: **when the neocortical substrate has insufficient representational capacity for the task, consolidation cannot transfer hippocampal knowledge into weights, and the hippocampus remains permanently necessary.**

This is not exotic. It is the normal condition for small embedded systems with fixed-point arithmetic and limited parameters. The CLS framework assumes the neocortex has arbitrary capacity. The Reflex demonstrates what happens when it doesn't.

---

## 6. The Transition Experiment (Pending Re-Validation)

### 6.1 Status

The original paper (April 7-8) presented a transition experiment (TEST 14C: P1 for 90s → P2 for 30s, three conditions, three seeds). This data was collected before two compounding bugs were fixed:
1. Sender enrollment starvation: Board A's enrollment only saw P1, making TriX signatures for P0/P2/P3 zero
2. `trix_enabled` not set: the ISR never ran TriX classification, so the bias mechanism was inactive

All transition data from April 8 is deprecated (`data/apr8_2026/DEPRECATED.md`). The transition experiment has been re-run under label-free conditions for Test 14 (cycling sender, kinetic attention comparison) but NOT for Test 14C (transition sender). The VDB stabilization finding from the April 8 data — that VDB feedback prevents P1 regression during P1→P2 transitions — is a PREDICTION awaiting re-validation, not a confirmed result.

### 6.2 What We Expect

The VDB stabilization mechanism is independent of the bugs that were fixed:
- The VDB stores episodic snapshots regardless of TriX accuracy
- The LP blend retrieves and injects regardless of the bias mechanism
- The stabilization (preventing regression to the old attractor) should hold because it depends on VDB content, not on classification or bias

But we cannot cite specific alignment traces, crossover steps, or regression magnitudes until the experiment is re-run with: (a) the corrected sender (enrollment cycling window), (b) label-free input (`MASK_PATTERN_ID_INPUT=1`), (c) the distinct P2 payload. This is listed as the next experimental step in `DO_THIS_NEXT.md`.

---

## 7. Limitations

1. **Transition experiment not yet re-validated.** The April 8 data is deprecated. The stabilization finding awaits re-collection under label-free conditions. The qualitative claim ("hippocampus stabilizes") is a prediction based on mechanism analysis, not a confirmed silicon measurement.

2. **Single seed for Hebbian replication.** The +0.1 ± 1.1 finding is from 3 repetitions of a single seed (0xCAFE1234). Different seeds may show different Hebbian effects due to different random projections. However, the VDB-only baseline (9.7 ± 0.6) is stable, suggesting the learning difficulty is not seed-specific.

3. **16 neurons may be too few for consolidation.** The flat Hebbian landscape (16 quantized outputs, ~29 weights each) may be specific to this dimensionality. Wider LP (32 neurons) or MTFP-targeted learning (80-trit error signals) might enable consolidation. This is an open question, not a tested claim.

4. **Four patterns, one sender.** The system classifies 4 patterns from a single ESP-NOW sender. Scaling to more patterns or multiple senders may require larger VDB capacity or different encoding.

5. **JTAG attached.** All runs use USB-JTAG for serial output. UART-only verification is pending.

6. **MTFP metric is nonlinear.** MTFP Hamming (the primary divergence metric) counts differing trits in a nonlinear encoding. A sign-trit disagreement is more significant than a mantissa-trit disagreement. The 9.7/80 baseline should be interpreted as a proxy for dot-product diversity, not a calibrated distance. See `data/apr11_2026/SUMMARY.md` for interpretation notes.

---

## 8. Conclusion

A ternary microcontroller drawing 30 microamps implements Complementary Learning Systems with a twist: the neocortex never learns — and when we made it try, it couldn't improve on the hippocampus.

Three iterations of Hebbian weight learning, each addressing a limitation of the previous one, each tested under ablation-controlled replicated conditions, produced +0.1 ± 1.1 Hamming improvement over the hippocampus-only baseline. The consolidation path does not work. Not because we omitted it. Because the hippocampus is already doing what consolidation is supposed to achieve.

The VDB episodic memory layer — 64 nodes in 2KB of LP SRAM, searched at 100 Hz by a 16 MHz RISC-V core — stores, retrieves, and blends episodic snapshots of the system's perceptual state. That blend alone produces 8.5-9.7/80 MTFP divergence across 4 wireless signal patterns. No weight learning. No labels. No training loop. No gradient. No floating-point number.

The hippocampus is not a staging area. It is not a scaffold for learned representations. It is the temporal model itself. The system's accumulated experience is in its memories, not in its weights.

Classification is structurally separated from the temporal model by a zero-weight wall (`W_f hidden = 0`). The classifier reports the current pattern at 100% accuracy from the first packet. The temporal model takes 120 seconds to build its representation. The gap between immediate perception and gradual understanding — bridged by the hippocampus — is the system's epistemic architecture in action.

The prior should be a voice, not a verdict. And the voice comes from memory, not from learned weights.

---

## Appendix: Hardware and Firmware

| Parameter | Value |
|---|---|
| Chip | ESP32-C6FH4 (QFN32) rev v0.2 |
| GIE rate | 430.8 Hz (peripheral fabric) |
| LP core | 16 MHz RISC-V, ~30 µA |
| LP CfC | 16 neurons, 48-trit input, fixed random weights |
| VDB | 64 nodes, NSW graph (M=7), 48-trit vectors |
| LP MTFP | 80 trits (5 per neuron: sign + 2 exp + 2 mant) |
| Build flags | `MASK_PATTERN_ID=1`, `MASK_PATTERN_ID_INPUT=1` |
| Authoritative data | `data/apr11_2026/SUMMARY.md` |
| Firmware | commit `ebc65a4` (TEST 15 with MTFP measurement) |

## Companion Papers

- **Stratum 1 (Engineering):** Ternary peripheral-fabric neural computation with VDB temporal context. See `PAPER_KINETIC_ATTENTION.md` (undergoing rewrite — kinetic attention found harmful at MTFP; paper reframing around VDB temporal context).
- **Stratum 2 (this paper):** Fixed-weight CLS architecture with permanent hippocampus.
- **Stratum 3 (Principle):** Prior-signal separation as structural hallucination resistance. See `PRIOR_SIGNAL_SEPARATION.md`.

---

*The hippocampus is not a staging area. It is the temporal model. The prior should be a voice, not a verdict. And the voice comes from memory, not from learned weights.*
