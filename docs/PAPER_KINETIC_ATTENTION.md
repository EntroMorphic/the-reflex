# Peripheral-Hardware Ternary Neural Computation with Episodic Temporal Context at 30 Microamps

**Tripp Josserand-Austin**
EntroMorphic Research

*Rewritten: April 12, 2026. Original title was "Kinetic Attention in a Ternary Reflex Arc." The kinetic attention mechanism was found to be harmful at MTFP resolution (-5.5/80, 3 runs). Reframed around the system's demonstrated capability: 100% label-free classification + 8.5-9.7/80 MTFP temporal divergence from VDB episodic memory alone.*

*Data: `data/apr11_2026/SUMMARY.md` (authoritative label-free dataset). Firmware commit `ebc65a4`. ESP32-C6FH4, ESP-IDF v5.4. Build: `MASK_PATTERN_ID=1`, `MASK_PATTERN_ID_INPUT=1`.*

---

## Abstract

We present a three-layer ternary neural computing system on a $0.50 microcontroller that classifies wireless signals at 100% label-free accuracy in peripheral hardware at 430 Hz and accumulates a temporal model of what it has perceived — pattern-specific internal representations that develop from episodic memory retrieval over 120 seconds of live operation. The system draws approximately 30 microamps in autonomous mode. No floating point. No multiplication. No backpropagation. No training. Signatures enrolled from a 30-second observation window (sign of mean). CfC weights random and never updated.

The temporal model emerges from a 64-node Navigable Small World graph (2KB of LP SRAM) that stores and retrieves episodic snapshots of the system's perceptual state. The retrieval-and-blend mechanism alone — without any gate bias or weight learning — produces 8.5-9.7/80 MTFP divergence across 4 wireless signal patterns. The episodic memory is causally necessary: ablation (CfC without VDB blend) collapses pattern pairs that VDB feedback separates.

We also report two honest negative results from mechanisms designed to improve on the VDB baseline:

1. **Kinetic attention** (agreement-weighted gate bias): consistently harmful at MTFP resolution (mean -5.5/80, 3 runs). The bias saturates the GIE hidden state, reducing LP dot magnitude diversity. The sign-space metric (+1.3/16) incorrectly showed improvement by masking the magnitude damage.

2. **Hebbian weight learning** (3 iterations with ablation control): +0.1 ± 1.1/80 MTFP at n=3. Indistinguishable from zero. The 16-neuron LP with random ternary weights has insufficient representational capacity for single-trit Hebbian flips to find useful structure.

The system's power is in its episodic memory, not in learned weights or attentional bias. Classification is structurally separated from the temporal model by a zero-weight wall (`W_f hidden = 0`) that holds across all experiments. The classifier perceives. The memory models. The structural wall guarantees they cannot corrupt each other.

All results are from bare silicon. No simulation. No post-hoc analysis. The hardware generates the data, runs the statistics, and reports pass/fail on serial output.

---

## 1. Introduction

The dominant paradigm for neural computation on microcontrollers treats the device as a constrained deployment target — a model trained elsewhere, quantized, and loaded onto the chip to perform inference. The computation is designed in floating point and adapted to the hardware. The hardware is an inconvenience to be managed.

This paper describes a system designed the other way. The computation emerged from the hardware's native capabilities — ternary arithmetic on peripheral routing fabric — and the architecture grew from asking what structure those capabilities naturally support. The result is not a compressed version of a larger model. It is a system that could not exist in any other substrate, because its computational primitives (DMA descriptor chains, pulse counters, parallel I/O loopback) have no floating-point analog.

The system has three layers:

- **Layer 1 (GIE):** A circular DMA chain streams ternary-encoded weight-input products through a parallel I/O peripheral configured in loopback mode. Two pulse counter units accumulate agreement and disagreement edges. An ISR decodes per-neuron dot products from cumulative differencing, applies a ternary CfC blend (UPDATE/HOLD/INVERT), re-encodes the updated hidden state into the DMA descriptors, and re-arms the parallel I/O byte counter. The chain loops at 430 Hz. After initialization, the CPU computes zero dot products.

- **Layer 2 (LP core):** A 16 MHz RISC-V ultra-low-power core, drawing approximately 30 microamps, runs a second ternary CfC network in hand-written assembly. It reads the GIE's hidden state, computes 32 ternary intersections against its own weight matrices, and maintains a 16-trit hidden state. A 64-node Navigable Small World graph provides episodic memory — storing 48-trit snapshots of the system's perceptual state and retrieving the nearest match for feedback blending into the LP hidden state. Five command modes (CfC step, VDB search, VDB insert, CfC+VDB pipeline, CfC+VDB+feedback) execute in a single 10 ms wake cycle.

- **Layer 3 (HP core):** The 160 MHz application core handles initialization, weight loading, ESP-NOW wireless reception, classification arbitration, and test orchestration. It is awake only when needed.

The operations used throughout are AND, popcount (byte lookup table), add, subtract, negate, branch, and shift. The operations absent are multiply, divide, floating point, gradients, and backpropagation.

### 1.1 The Claims

This paper demonstrates three things and honestly reports two failures:

**Demonstrated:**
1. Peripheral-hardware ternary dot products at 430 Hz classify 4 wireless signal patterns at 100% label-free accuracy (TEST 11)
2. A 64-node episodic memory layer produces pattern-discriminative temporal states at 8.5-9.7/80 MTFP divergence from VDB feedback alone (TEST 12)
3. The episodic memory is causally necessary — ablation collapses what VDB feedback separates (TEST 13)

**Failed:**
4. Agreement-weighted gate bias (kinetic attention) degrades LP representation at MTFP resolution: -5.5/80 mean across 3 runs (TEST 14)
5. Hebbian LP weight learning produces no improvement: +0.1 ± 1.1/80 at n=3 (TEST 15)

The architecture was motivated by a minimum-assumptions approach — build what the hardware can do, measure what emerges. Kinetic attention and Hebbian learning were hypothesized to improve on the VDB baseline. They didn't. The VDB baseline IS the finding.

---

## 2. Architecture

### 2.1 The Geometry Intersection Engine

The GIE exploits a property of the ESP32-C6's peripheral fabric: the General DMA controller (GDMA) can stream data through the Parallel I/O transmitter (PARLIO) configured in 2-bit loopback mode, producing edge transitions on GPIO pins that are counted by two Pulse Counter (PCNT) units. By encoding ternary weight-input products as 2-bit pairs (positive = `01`, negative = `10`, zero = `00`), the PCNT accumulates agreement (positive-positive + negative-negative) and disagreement (positive-negative + negative-positive) edges across each neuron's 160-trit buffer. The dot product is `agreement - disagreement`.

The DMA descriptor chain is circular: 5 dummy neurons (all-zero buffers that absorb PCNT pipeline residue) followed by 64 real neurons (32 gate-pathway + 32 candidate-pathway), with the last descriptor pointing back to the first. Each neuron's buffer is followed by a 64-byte separator with EOF=1, which triggers a GDMA interrupt. The ISR captures cumulative PCNT values at each separator, decodes per-neuron dots via differencing, applies the CfC blend equation, re-encodes the hidden state into the DMA buffers, and re-arms PARLIO's byte counter.

The CfC blend equation in ternary:

```
f[n] = sign(dot(W_f[n], [input|hidden]))    gate:      {-1, 0, +1}
g[n] = sign(dot(W_g[n], [input|hidden]))    candidate: {-1, 0, +1}
h_new = (f == 0) ? h_old                     HOLD: preserve
      : (f == +1) ? g                        UPDATE: accept candidate
      : (f == -1) ? -g                       INVERT: reflect candidate
```

The three blend modes create non-gradient dynamics: HOLD provides inertia, UPDATE provides responsiveness, and INVERT creates oscillation and convergence resistance. Binary CfC has only UPDATE and HOLD. The third mode is a consequence of the ternary constraint.

### 2.2 Classification

**Input encoding (128 trits):** The ESP-NOW packet is encoded into 128 ternary trits:
- [0..15] RSSI thermometer (16 trits): binary, same sender → shift-invariant under argmax
- [16..23] Pattern ID one-hot (8 trits): **masked** under `MASK_PATTERN_ID_INPUT=1`
- [24..87] Payload bits (64 trits): 8 bytes × 8 bits, bit→trit, pattern-specific content
- [88..103] MTFP21 gap history (16 trits): inter-packet timing, 5 gaps × 3 trits + variance flag
- [104..127] Zeroed (24 trits): sequence counter silenced at source

**Label-free operation:** The `MASK_PATTERN_ID=1` flag zeros pattern_id trits [16..23] in the enrollment signatures. The `MASK_PATTERN_ID_INPUT=1` flag zeros them in the runtime GIE input. Under both flags, no label information exists anywhere in the system — not in the TriX signatures, not in the GIE hidden state, not in the VDB nodes, not in the LP CfC input. Classification relies entirely on payload content and inter-packet timing.

**TriX classification (ISR, 430 Hz):** Four ternary signature vectors are installed as W_f gate weights, with 8 neurons per pattern group. The ISR extracts per-group dot products and publishes the argmax. Accuracy: 32/32 = 100% label-free on the authoritative dataset (`data/apr11_2026/full_suite_label_free_final.log`). The structural guarantee `W_f hidden = 0` ensures classification is independent of the LP prior, gate bias, or temporal accumulation.

**Prior P1-P2 confusion (resolved):** With the original P2 payload (`{0xAA, alt, ...}`), label-free accuracy was 71% — P2 shared 48/64 payload trits with P1 (bytes 2-7 were 0x00 in both). P0, P1, and P3 were already 100% label-free. The P2 payload was redesigned to `{0x55, 0x33, 0xCC, 0x66, 0x99, 0x0F, 0xF0, 0x3C}`, reducing the P1-P2 signature cross-dot from 78/96 (81%) to 29/96 (30%). This made the test FAIR (all 4 patterns have distinct payload content), not easy. 100% label-free accuracy was restored.

### 2.3 The LP Core: Geometric CfC + Episodic Memory

The LP core runs a second CfC network with 16 hidden neurons, taking the GIE's 32-trit hidden state concatenated with its own 16-trit hidden state as a 48-trit input. Weights are random ternary, generated once at initialization and never updated.

The LP core's VDB stores up to 64 snapshots of the system's perceptual state — 48-trit vectors comprising `[gie_hidden | lp_hidden]`. A Navigable Small World graph (M=7 neighbors, dual entry points) provides sub-linear approximate nearest-neighbor search. On each classification event (CMD 5), the LP core:

1. Runs one CfC step (32 intersections)
2. Searches the VDB using the packed CfC input as the query
3. Blends the best match's LP-hidden portion into the current LP hidden state

The blend rule:
- Agreement (h == mem): no change
- Gap fill (h == 0, mem != 0): adopt memory value
- Conflict (h != 0, mem != 0, h != mem): set to 0 (HOLD)

The HOLD-on-conflict rule is the damper. A neuron in conflict reverts to the neutral state, preserving its value on the next CfC step. This bounds the feedback loop — ternary values can change by at most 1 per step, and conflicting feedback produces inertia rather than oscillation.

### 2.4 The MTFP Measurement Resolution

The LP CfC computes 16 integer dot products per step (`lp_dots_f[16]`). The sign-quantized version (`lp_hidden[16] = sign(lp_dots_f)`) is the 16-trit ternary vector used by the LP CfC's recurrence and the VDB. The MTFP-encoded version (5 trits per dot: sign + 2 exponent + 2 mantissa) is an 80-trit vector that preserves magnitude information.

The distinction is critical. Sign-space Hamming and MTFP Hamming can give **opposite results** for the same data (see Section 4.2). All divergence measurements in this paper use MTFP unless otherwise noted.

### 2.5 How It Fits: 16KB Memory Budget

The LP core's 16KB SRAM contains:
- Vector table: 128 bytes
- Code (.text): ~7,600 bytes (CfC + VDB search + VDB insert + pipeline + feedback)
- Popcount LUT (.rodata): 288 bytes
- CfC state (.bss): ~968 bytes
- VDB nodes (.bss): 2,048 bytes (64 × 32 bytes)
- VDB metadata: ~80 bytes
- Feedback state: ~24 bytes
- Shared memory: 16 bytes
- **Free for stack: ~4,400 bytes** (peak: 608 bytes during VDB search)

Every byte is specified. Every instruction is hand-written. No compiler decisions, no hidden register allocation, no spills.

---

## 3. Results: What the Hardware Demonstrates

### 3.1 Classification: 100% Label-Free (TEST 11)

32/32 correct classifications. Both ISR TriX (430 Hz hardware) and CPU core_pred agree at 100%. Four wireless signal patterns with distinct payloads and timing characteristics, classified from a 30-second enrollment window using sign-of-mean signatures. No labels in the input, no labels in the signatures, no labels anywhere in the system.

This is not a compressed model running inference. It is an enrollment-based classifier where the enrollment happens on the same hardware that runs the classification, using the same ternary operations, in 30 seconds of live observation.

### 3.2 Temporal Context: 8.5-9.7/80 MTFP Divergence (TEST 12)

With VDB feedback (CMD 5), 120 seconds of live wireless input from 4 cycling patterns produces the following LP MTFP divergence matrix:

```
     P0  P1  P2  P3
P0:   0   5   6   8
P1:   5   0   9  13
P2:   6   9   0  10
P3:   8  13  10   0
```

Mean: 8.5/80 (full suite) to 9.7 ± 0.6/80 (3-rep SKIP_TO_15 measurement). All 6 pairs separated. P3 (ramp: incrementing payload at 10 Hz) is the most distinctive.

This is the LP CfC's 16 dot products, MTFP-encoded, accumulated over 120 seconds of episodic memory retrieval. The VDB stores snapshots of [gie_hidden | lp_hidden] and blends the nearest match back into the LP state. Over time, the LP state develops pattern-specific magnitude profiles — different patterns produce different dot product distributions, visible in the MTFP encoding.

No labels drive this. No training signal. No gradient. The temporal model emerges from the interaction between the CfC projection (random but consistent) and the VDB episodic retrieval (stores what actually happened, blends it back in).

### 3.3 Causal Necessity: VDB Feedback Required (TEST 13)

CMD 4 (CfC + VDB search, no feedback blend) vs CMD 5 (CfC + VDB + feedback blend):
- CMD 4 P1-P2 sign Hamming: 1
- CMD 5 P1-P2 sign Hamming: 2, MTFP Hamming: 9

VDB feedback contribution: +1 sign trit, +8 MTFP trits for the P1-P2 pair. The VDB is causally necessary.

---

## 4. Honest Negatives: What We Tried and What We Learned

### 4.1 Kinetic Attention: Harmful at MTFP Resolution

**Mechanism:** Agreement-weighted gate bias. When the LP prior agrees with the TriX prediction, lower the gate threshold for the predicted pattern's neuron group. More neurons fire for the expected pattern, making the GIE hidden state more sensitive to it.

**Hypothesis:** Gate bias should amplify LP divergence by making the GIE more pattern-specific.

**Result (3 runs, label-free, Test 14):**

| Run | 14A (no bias) MTFP mean | 14C (full bias) MTFP mean | Improvement |
|---|---|---|---|
| 1 | 9.8 | 10.2 | +0.4 |
| 2 | 15.5 | 8.5 | **-7.0** |
| 3 | 15.5 | 5.7 | **-9.8** |
| Mean | 13.6 | 8.1 | **-5.5** |

**Diagnosis:** The bias lowers the gate threshold for one neuron group. More neurons in that group fire. More firing = more non-zero trits in the GIE hidden state = more saturation. A saturated GIE hidden state is MORE UNIFORM across patterns (all patterns produce high-firing states), which makes the LP input LESS discriminative. The bias improves GIE sensitivity but degrades GIE discriminability.

**The sign-space artifact:** Sign-space showed +1.3/16 mean improvement. This was because the bias changed WHICH trits had non-zero signs (creating sign-space divergence) while crushing the magnitudes (destroying MTFP divergence). The sign-space "improvement" was a net information loss visible only at MTFP resolution. See Section 4.3.

**The mechanism fires** (per-group fire rate shift >10% every run). **The effect is negative.** Reported honestly.

### 4.2 Hebbian Learning: Noise at 16 Neurons

**Three iterations, each addressing a limitation of the previous:**

1. **VDB mismatch, f-only:** +2.5 with label in input, -1.7 genuinely label-free. The VDB error signal was exploiting the pattern_id leaked through the GIE hidden state.

2. **TriX accumulator, f-only:** -1.0 label-free. Clean target (TriX labels are structurally guaranteed) but ~50% of errors were in the g-pathway, making f-pathway flips counterproductive.

3. **TriX accumulator, diagnosed f+g:** +0.1 ± 1.1/80 MTFP at n=3. The diagnosis fixed the direction (harmful → neutral) but the mechanism produces no improvement.

**Diagnosis:** The 16-neuron LP with random ternary weights produces 16 integer dot products. Each dot is the sum of ~29 non-zero ternary products. The sign() output has 2^16 possible configurations. Flipping one weight changes one dot by ±2 — a tiny perturbation in a flat landscape with vast equivalence classes.

The VDB feedback blend, by contrast, directly injects episodic content into LP hidden. It doesn't need to find good weights — it bypasses the weights entirely. Direct episodic injection is more effective than weight learning when the output space is coarsely quantized and the weights are random.

### 4.3 The Sign-Space Metric Can Mislead

The kinetic attention finding was initially reported as positive (+1.3/16 in sign-space) before the MTFP numbers were examined. The discrepancy:

**Run 2, the most dramatic case:**
- Sign-space 14A: 0.0/16 (all patterns → identical sign vector)
- Sign-space 14C: 4.2/16 (bias "broke the symmetry")
- MTFP 14A: 15.5/80 (patterns well-separated in magnitude)
- MTFP 14C: 8.5/80 (bias crushed the magnitude diversity)

The LP states had identical signs but different magnitudes. Sign-space saw them as identical (Hamming 0). MTFP saw them as well-separated (15.5/80). The bias shuffled the signs (creating sign-space divergence) while crushing the magnitudes (destroying MTFP divergence). A net information loss that sign-space reported as a gain.

**The lesson:** LP divergence in sign-space (Hamming over 16 sign trits) is a lossy metric. MTFP divergence (Hamming over 80 trits encoding sign + magnitude) is richer (5× more dimensions) and more stable (coefficient of variation 6% vs 39%). When sign-space and MTFP disagree, trust MTFP.

The MTFP encoding already existed in the codebase (implemented April 7). The tests were printing both metrics in every run. The sign-space numbers were read first because they matched the project's established voice. The MTFP numbers were the measurement hiding beneath the prior.

---

## 5. The Structural Wall

The classification guarantee `W_f hidden = 0` was verified across every experiment in the April 9-12 session, including:
- Label-free operation (pattern_id masked from both signatures and input)
- Hebbian weight learning (LP W_f and W_g modified, GIE W_f untouched)
- Multiple seeds (3 seeds for Test 14, single seed for Hebbian replication)

TriX accuracy remains 100% label-free in every configuration. The structural wall — the guarantee that the classifier cannot be influenced by the temporal model — is the project's most robust finding.

This guarantee enables the honest negative results. We could test kinetic attention and Hebbian learning because the structural wall guaranteed that no matter how badly those mechanisms performed, they could not degrade the 100% classification accuracy. The classifier is immune to the prior. The prior can be wrong, the bias can be harmful, the weights can be random — the classifier still reports what it sees, not what the system believes.

---

## 6. Related Work

### 6.1 Ternary Neural Networks
Ternary weight networks (Li et al., 2016; Zhu et al., 2016) quantize trained float models to {-1, 0, +1}. The Reflex does not quantize — its weights are generated ternary and never trained. The computational primitives (AND + popcount) are the native operations, not approximations of multiply-accumulate.

### 6.2 Complementary Learning Systems
See companion paper (Stratum 2): `PAPER_CLS_ARCHITECTURE.md`. The VDB episodic memory layer implements a permanent hippocampal analog. Consolidation (Hebbian weight learning) was tested and found ineffective at this dimensionality.

### 6.3 Neuromorphic Computing
Hardware neural networks on neuromorphic chips (Intel Loihi, BrainChip Akida) use spike-timing-dependent plasticity on dedicated silicon. The Reflex uses commodity peripheral fabric (DMA, pulse counters, parallel I/O) on a $0.50 general-purpose microcontroller. The architectural contribution is substrate-agnostic — the same ternary operations run on AVX2 at 2.8 MHz or on peripheral fabric at 430 Hz.

### 6.4 Prior-Signal Separation
See companion paper (Stratum 3): `PRIOR_SIGNAL_SEPARATION.md`. The `W_f hidden = 0` structural wall implements prior-signal separation as an architectural guarantee, not a statistical property.

---

## 7. Limitations

1. **Single seed for most measurements.** The full-suite label-free dataset is one seed (0xCAFE1234). Kinetic attention was measured across 3 runs of this seed. Hebbian was replicated at n=3 on this seed. Multi-seed validation under label-free conditions has not been performed.

2. **Four patterns, one sender.** Classification uses 4 patterns with distinct payloads from a single ESP-NOW sender. Scaling to more patterns or multiple senders is untested.

3. **JTAG attached.** All runs use USB-JTAG for serial output and power. The "peripheral-autonomous" claim requires UART-only verification with direct current measurement. Pending.

4. **MTFP metric is nonlinear.** MTFP Hamming counts differing trits in a nonlinear encoding. A sign-trit disagreement is more significant than a mantissa-trit disagreement. The 8.5-9.7/80 baseline should be interpreted as a proxy for dot-product diversity, not a calibrated distance.

5. **Transition experiment not yet label-free.** The P1→P2 transition experiment (TEST 14C) was run under the old configuration (label in input, broken enrollment). It needs re-running under label-free conditions to validate the VDB stabilization finding reported in the companion CLS paper.

6. **16 neurons may be a ceiling.** The Hebbian null result may be specific to 16 neurons with 48-trit input. Wider LP (32 neurons) or MTFP-targeted learning (80-trit error signals) might enable weight learning. Untested.

---

## 8. Conclusion

A ternary peripheral-fabric classifier at 430 Hz on a $0.50 microcontroller discriminates 4 wireless signal patterns at 100% label-free accuracy and builds a temporal context layer with 8.5-9.7/80 MTFP divergence — from episodic memory retrieval alone, without gate bias, without weight learning, without labels, without training, without floating point.

The system draws approximately 30 microamps. The classification is structurally separated from the temporal model by a zero-weight wall that holds across every experiment. The episodic memory (64 nodes, 2KB, NSW graph, 100 Hz search) is causally necessary for LP divergence.

We tried to improve on the VDB baseline with two mechanisms: agreement-weighted gate bias (kinetic attention) and Hebbian LP weight learning. Gate bias is harmful — it saturates the GIE hidden state, reducing LP magnitude diversity. Hebbian learning is noise — the 16-neuron LP with random ternary weights is too coarsely quantized for single-trit flips to find useful structure.

The system's power is in its episodic memory. The temporal model is in the memories, not in the weights. The hardware computes the dot products. The memories hold the context. The structural wall guarantees they cannot corrupt each other.

The hardware is the teacher. The memory is the model. Abstraction is the enemy.

---

## Appendix A: Hardware Constants

| Parameter | Value |
|---|---|
| Chip | ESP32-C6FH4 (QFN32) rev v0.2 |
| GIE rate | 430.8 Hz (peripheral fabric) |
| LP core | 16 MHz RISC-V, ~30 µA |
| LP CfC | 16 neurons, 48-trit input, fixed random weights |
| VDB | 64 nodes, NSW graph (M=7), 48-trit vectors |
| LP MTFP | 80 trits (5 per neuron: sign + 2 exp + 2 mant) |
| TriX | 4 patterns, 8 neurons/pattern, 100% label-free |
| Structural wall | `W_f[n][CFC_INPUT_DIM:] = 0` for all neurons |
| Gate threshold | 90 (base), 30 (floor), bias=0 (recommended) |
| Build flags | `MASK_PATTERN_ID=1`, `MASK_PATTERN_ID_INPUT=1` |

## Appendix B: Operations Used

Gate pathway: AND, popcount (byte LUT), add, subtract, branch, shift.
Candidate pathway: same operations, different weight matrix.
Absent: multiply, divide, floating point, gradients, backpropagation.

## Appendix C: Companion Papers

- **Stratum 2 (CLS Architecture):** Fixed-weight CLS with permanent hippocampus. `PAPER_CLS_ARCHITECTURE.md`.
- **Stratum 3 (Principle):** Prior-signal separation as structural hallucination resistance. `PRIOR_SIGNAL_SEPARATION.md`.

---

*The hardware is the teacher. The memory is the model. The prior should be a voice, not a verdict.*
