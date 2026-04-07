# Nodes of Interest: The Reflex — Full Project LMM

## Node 1: The Ternary Constraint as Generative Force

Every distinctive property traces to {-1, 0, +1}:
- Three blend modes (UPDATE/HOLD/INVERT) vs binary's two
- The HOLD state as inertia (zero gate = preserve)
- W_f hidden = 0 as structural separation (consequence of TriX routing, not deliberate safety)
- Bitmask packing (16 trits per word pair) → 16× compression vs float32
- AND+popcount dot product → runs on peripheral hardware without MUL
- MTFP encoding: 3^5 = 243 states, naturally covers the dot range

Tension: is this a property of ternary arithmetic specifically, or would any low-precision fixed-point system have similar properties? The HOLD mode requires a true zero (not just a small value). Bitmask packing requires exactly 2 bits per element (pos/neg). These are ternary-specific. A binary system packs 1 bit/element but has no zero state. A quaternary system needs 2 bits/element but the extra state is waste (no natural interpretation for the fourth value in a signed system).

Why it matters: if the ternary constraint is genuinely generative (not just compact), it suggests a design principle — start from the hardware's native precision and let the architecture follow, rather than designing in float and compressing.

## Node 2: The MTFP Pattern (Three Applications, One Principle)

1. **Timing:** 16-trit thermometer → 5×3 = 15-trit MTFP21 gap history. Classification 80% → 96%.
2. **LP dots:** 1-trit sign → 5-trit MTFP per neuron. LP hidden 16 → 80 trits. P1-P2 separation from 0/16 to 5-10/80.
3. **Next (predicted):** GIE hidden 32 sign trits → MTFP encoding of GIE dot magnitudes. Would give the LP core richer input without changing GIE hardware.

Principle: when a scalar quantization (thermometer, sign) collapses information a downstream consumer needs, replace it with a structured multi-trit encoding that preserves the collapsing dimension.

Tension with Node 6: each MTFP expansion increases dimensionality. LP hidden 80 trits is already 5× the original. GIE MTFP would push LP input from 48 to 192 trits. At some point the packing overhead exceeds the LP SRAM budget. The encoding is not free.

## Node 3: The Permanent VDB (Hippocampus Without Consolidation)

The VDB never consolidates into the CfC. The CfC weights are fixed forever. The VDB is not a scaffold — it is permanently load-bearing. This is the most interesting architectural property and the biggest departure from biological CLS.

In biological CLS, the hippocampus replays memories to train the neocortex, eventually making itself redundant for well-learned categories. In The Reflex, the VDB can never be made redundant because the CfC can never learn. The VDB compensates for projection degeneracies (P1-P2 sign-space collapse) that are permanent features of the random weight matrix.

Tension: Pillar 3 (Hebbian learning) would allow the CfC to learn from VDB content. This would move the system toward biological CLS — but it would also eliminate the most novel property (permanent hippocampal dependence). The fixed-weight architecture is a cleaner theoretical contribution. Hebbian learning is a better engineering contribution. Which do you optimize for?

Node 3 tension with Node 5: if the VDB is permanent, it will fill up (64 nodes). Dynamic scaffolding (Pillar 1) must solve this. But pruning criteria assume you can tell which memories are redundant — and redundancy depends on whether the CfC can represent a state independently. With fixed CfC weights, the CfC may permanently be unable to represent certain states, meaning no VDB node encoding those states is ever redundant.

## Node 4: The Agreement Mechanism as Philosophical Core

The system checks: does my prior agree with my measurement? When they agree, the prior amplifies perception. When they disagree, the prior yields. Within one cycle. No debate, no deliberation, no averaging. Structural deference.

This is not a technical detail. It is the design philosophy: **the prior should be a voice, not a verdict.** The entire gate bias mechanism, the W_f hidden = 0 wall, the TriX structural guarantee — all serve this one principle. The system that accumulates experience and the system that reads evidence are different subsystems with a structural wall between them, and when they conflict, the evidence wins.

Tension: the prior always yields. There is no mechanism for the prior to override the evidence even when the evidence is noisy or corrupt. A single spurious classification can zero the bias. In a noisy environment, the prior might be more reliable than the immediate measurement — but the architecture always trusts the measurement. This is epistemic humility carried to an extreme. It works because TriX is 100% accurate. In a system with lower classification accuracy, unconditional evidence deference would be a liability.

## Node 5: Scaling Walls

Current: 4 patterns, 32 GIE neurons, 16 LP neurons, 64 VDB nodes, 16KB SRAM.

Scaling up means hitting:
- **VDB capacity (64 nodes):** ~3 minutes at current insert rate. Dynamic scaffolding is the answer but not implemented.
- **Pattern count (4 groups × 8 neurons):** Adding P5 means either 5 groups × 6 neurons (lose sensitivity) or 5 groups × 8 = 40 neurons (need more PCNT range or multiple PCNT chains).
- **LP hidden dimensionality:** 16 sign trits or 80 MTFP trits. More patterns need more discriminative dimensions.
- **LP SRAM (16KB):** Currently 73% used. MTFP expansions consume the remaining headroom.

Tension: the beauty of the system is its minimalism. Scaling it risks losing what makes it interesting. A 256-neuron GIE with 1024 VDB nodes would be a conventional embedded neural network, just in ternary. The insight is that 32 neurons and 64 nodes are *enough* when the architecture is right.

## Node 6: The Three-Paper Cluster as Intellectual Structure

- Stratum 3 (Principle): The five-component architecture. Why structural separation matters. Scale-independent.
- Stratum 1 (Engineering): The silicon implementation. How it works. Hardware-specific.
- Stratum 2 (Architecture): The CLS prediction. What it means. Theory-meeting-silicon.

These aren't three papers about the same thing. They're three different arguments that share a common substrate. The principle paper says "this structure is necessary." The engineering paper says "this structure is achievable." The architecture paper says "this structure predicts behavior."

Tension: the principle paper makes claims about LLMs that the engineering can't support. The engineering paper makes claims about power consumption that the principle doesn't need. The architecture paper makes claims about CLS theory that neither of the others addresses. The cluster is stronger than the individual papers — but only if a reader encounters all three. Independently, each has gaps the others fill.

## Node 7: What the Hardware Taught

The most important lessons came from hardware failures:
- The PCNT clock-domain drain (200 volatile loops, ~5µs) was discovered by empirical failure, not analysis
- The triple PCNT clear was found by reducing from 3 to 2 and watching intermittent residue corrupt dots
- The PARLIO state machine reset (`parl_tx_rst_en`, PCR bit 19) was the fix for a second-call hang that blocked progress for a full session
- The phantom EOF clearing prevents stale interrupt triggers that would desynchronize the ISR count

None of these are in any datasheet. They were extracted from the silicon by breaking things and watching carefully. The errata list (20+ entries) is as much a contribution as the architecture — it documents ESP32-C6 peripheral behavior that Espressif hasn't published.

This is the deepest version of "the hardware is the teacher": not just that the hardware's native operations define the architecture, but that the hardware's failure modes define the engineering. The system works because of 20+ workarounds for undocumented silicon behavior. Remove any one and the free-running loop stalls, desynchronizes, or produces wrong dots.

## Node 8: The Moment of Truth (TEST 14C)

TEST 14C is running right now. If the LP state reorients faster with VDB feedback (CMD 5) than without it (CMD 4) during a pattern switch, the CLS prediction holds: the hippocampal layer accelerates adaptation. If not, the VDB is episodic storage that doesn't accelerate learning — useful but not CLS.

The first run showed P2 alignment exceeding P1 alignment from step 0 — the switch was too fast to measure because the sync wasn't working. The current run has fixed sync (ground-truth before novelty gate) and a properly-built transition sender. The outcome determines whether Stratum 2 is a paper or a future direction.

Tension: even if the CLS prediction holds, N=1 per condition is not publication-grade. Multiple runs with different weight seeds are needed. But the existence of the signal — any signal — is the finding that justifies the investment.
