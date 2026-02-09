# Reflections: The Reflex

## Core Insight

The Reflex is not a neural network running on a microcontroller. It is a **ternary dynamical system implemented as hardware infrastructure**. The distinction matters because it changes what the system IS, not just how it's built.

Traditional embedded ML: you have a processor, you run inference code on it, you get results. The processor is the computer; the neural network is the program.

The Reflex: the peripheral routing IS the computation. The GDMA descriptor chain IS the weight matrix. The PCNT accumulator IS the dot product. The CfC blend IS the dynamics. There is no program being executed in the traditional sense at the GIE layer — there is a peripheral configuration that, by its nature, transforms input state into output state. The CPU observes but does not compute.

This is closer to how analog circuits compute, or how biological neurons compute, than to how digital processors compute. The medium IS the message.

## The Three-Layer Insight

Why three layers? Because there are three fundamentally different computational substrates available on this chip, and each has a natural timescale and power cost:

| Layer | Substrate | Timescale | Power | Nature |
|-------|-----------|-----------|-------|--------|
| GIE | Peripheral fabric | ~2.3ms/loop | ~0 (peripheral clocks) | Infrastructure — not code |
| LP | RISC-V microcore | ~10ms/wake | ~30uA | Geometric — AND/popcount/branch |
| HP | Full RISC-V | On-demand | ~15mA | Algorithmic — general purpose |

This maps to biological nervous systems with suspicious fidelity:
- **GIE = spinal reflex arc**: fast, fixed, automatic, no "thinking" — just signal routing
- **LP CfC = brainstem/cerebellum**: rhythmic, integrative, pattern-matching, sub-conscious
- **HP = cortex**: slow, expensive, flexible, conscious

The three layers aren't an engineering convenience. They're what you get when you take "computation at the lowest possible level of abstraction" seriously. Peripheral fabric computes if you let it. A 16MHz core with a popcount LUT computes geometry. A full CPU does everything else but costs 500x more power.

## Resolved Tensions

### Node 1 vs Node 9: Constraint vs Capacity
The 48-trit dimension isn't a limitation of the ternary approach — it's the natural unit that emerges from the LP SRAM budget (16KB) when you want 64 searchable vectors with graph metadata. 48 trits in 6 words. 32 bytes per node including 7 neighbor IDs. 64 × 32 = 2KB. This leaves room for code, weights, and stack.

The resolution: the dimension is CORRECT for this substrate. Trying to make it larger would mean fewer nodes or cutting into code space. The system found its natural scale. The question isn't "is 48 trits enough?" — it's "what problems live in 48-trit space?"

### Node 7 vs Node 4: Feedback Danger vs Natural Regulation
The CfC's HOLD mode (f=0) is a natural feedback damper. When the gate fires zero, the hidden state persists regardless of input. In a closed loop where VDB results modulate CfC input, HOLD mode means "I've seen this before, stay the course." UPDATE mode means "new information, adapt." INVERT mode means "this contradicts my state, resist."

The resolution: the ternary CfC may be naturally stable under feedback BECAUSE of the discrete dynamics. There's no gradient to explode. The state is always {-1, 0, +1}. The worst case is oscillation between two states, which HOLD mode dampens. This is worth testing but there's reason for optimism.

### Node 12 vs Node 5: No Training vs Memory
The absence of training isn't a gap — it's a design choice consistent with the reflex metaphor. Biological reflexes aren't trained by backpropagation. They're wired by genetics (configuration) and modulated by experience (VDB insertion). The CfC doesn't learn weights; it learns STATES. The VDB stores states it has visited. Over time, the VDB accumulates a history of the system's dynamical trajectory.

The resolution: the VDB IS the training. Not in the gradient sense, but in the experiential sense. The system learns by remembering where it has been, not by optimizing where it should go.

## The Delta — Where Mistakes Hide

Applying the Laundry Method: what's at the boundaries between the three layers?

1. **GIE→LP boundary**: The ISR writes cfc.hidden[] to BSS; the main loop copies it to LP SRAM via feed_lp_core(). This boundary has a known issue — LP SRAM bus contention from ISR context stalls the time-critical ISR. We solved it by writing from main loop context only. But the hidden state the LP core sees may be 1-2 loops stale. Is this a bug or a feature? (In biological systems, sensory processing delay is real and compensated for.)

2. **LP→HP boundary**: The HP core reads LP SRAM variables (lp_hidden, vdb_results) by polling. There's no interrupt from LP to HP. The HP core doesn't know exactly when the LP core last completed a step. It just reads and trusts the data is recent enough.

3. **VDB query identity**: In M5, the CfC's packed input vector IS the VDB query. But this vector represents [gie_hidden | lp_hidden] BEFORE the CfC step updates lp_hidden. The VDB search happens AFTER the update. So the query is "what did the world look like just before I changed my mind" not "what does the world look like now." Is this the right semantics?

These boundary questions aren't bugs. They're design decisions that should be made consciously rather than inherited from implementation order.

## What I Now Understand

The Reflex is a three-layer ternary dynamical system that computes without executing code at its fastest layer, without multiplication at any layer, and without training at all. It is a reflex arc implemented in silicon: fast peripheral responses, sub-conscious integration, and expensive conscious oversight.

The ternary constraint is not a compromise — it is the generative principle that makes the architecture possible. The three blend modes are not a simplified neural network — they are dynamical primitives (follow, persist, resist) that produce rich behavior from minimal machinery.

The VDB pipeline (M5) is the first step toward a system that doesn't just react but REMEMBERS. The next step — closing the feedback loop — would create a system that doesn't just remember but ADAPTS.

What surprised me: the system found its own natural scale at every level. 48-trit vectors. 64 nodes. 16 neurons. 10ms wake cycles. 428 Hz GIE loops. None of these were designed top-down. They all emerged from the hardware constraints. The wood told us where to split.
