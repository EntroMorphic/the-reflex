# Nodes of Interest: The Reflex

## Node 1: The Ternary Constraint is Generative, Not Restrictive
The entire system exists BECAUSE of the ternary constraint, not despite it. {-1, 0, +1} maps to 2-bit GPIO encoding, which maps to PARLIO loopback, which maps to PCNT edge/level gating. Floating point would make none of this possible. The constraint created the architecture.
Why it matters: This inverts the usual narrative about low-precision neural networks being "approximations" of the real thing. Here, ternary IS the real thing. The hardware IS the precision.

## Node 2: Three Layers = Three Timescales
GIE runs at 428 Hz (peripheral clock speed). LP core runs at 100 Hz (timer wake). HP core runs on-demand (human timescale). These aren't arbitrary — they emerge from the hardware. Peripheral fabric is fastest. Geometric processor is medium. Full CPU is slowest and most expensive. The hierarchy is a power-frequency tradeoff ladder.
Why it matters: This mirrors biological neural hierarchies — spinal reflexes are fast/cheap, brainstem is medium, cortex is slow/expensive.

## Node 3: The GIE is Not Software
The GIE doesn't execute instructions. It configures peripheral routing (GDMA→PARLIO→GPIO→PCNT) such that data flowing through the routing IS the computation. The CPU sets up the configuration; the peripherals compute. This is closer to an FPGA or analog circuit than to code.
Why it matters: This is a different computational paradigm. It's "computation as infrastructure" — the plumbing IS the processor.

## Node 4: The CfC Blend Modes Map to Dynamical Primitives
UPDATE (f=+1): state follows candidate. HOLD (f=0): state persists. INVERT (f=-1): state opposes candidate. These three modes, with no gradients, produce: self-sustaining oscillation, convergence resistance, and path-dependent memory. These are properties of dynamical systems, not neural networks.
Why it matters: The CfC isn't learning in the ML sense. It's a dynamical system that responds to inputs with a rich behavioral repertoire from just three operations.

## Node 5: The VDB Closes a Perception-Memory Loop
The M5 pipeline packs [gie_hidden | lp_hidden] as a 48-trit vector, uses it as both the CfC input AND the VDB query. The same representation serves perception and retrieval. This is content-addressable memory driven by the system's own state.
Why it matters: This is the seed of associative recall. The system queries its memory with "what does the world look like right now?" and gets back "here are times the world looked similar."

## Node 6: No Multiplication Anywhere
The entire stack — from GIE dot products through LP core CfC to VDB graph search — uses only AND, popcount, add, sub, negate, branch, shift. The RV32IMAC M extension is never exercised. Ternary multiplication is sign comparison (XOR + branch). This isn't optimization; it's a different computational substrate.
Why it matters: Multiplication-free computation has implications for energy, silicon area, and verifiability. Every operation is exact — no rounding, no accumulation error.

## Node 7: The Open Loop
The VDB results currently flow up to the HP core and stop. They don't feed back into the CfC or the GIE. The system perceives and remembers but doesn't USE its memories to modify behavior. This is the obvious next step and the most dangerous one — feedback loops can diverge.
Tension with Node 4: The CfC already has convergence resistance (HOLD mode). Could this naturally regulate feedback?

## Node 8: The Name "Reflex"
A reflex is a fast, automatic response that bypasses conscious processing. The GIE is exactly this — peripheral hardware responding to input patterns without CPU involvement. The LP core CfC adds a second layer: a sub-conscious processor that integrates over time. The HP core is "consciousness" — slow, expensive, aware.
Why it matters: The name isn't metaphorical. The architecture IS a reflex arc: sensor → spinal cord → response, with optional cortical involvement.

## Node 9: The 48-Trit Vector Dimension
48 trits = 3^48 ≈ 7.9 × 10^22 possible vectors. But with 30% sparsity (many zeros), the effective space is smaller. 48 trits in 24 bytes (6 words). The NSW graph achieves 95% recall@1 at this dimension with only 64 nodes. Is this enough for real tasks? Or is it a proof-of-concept dimension?
Tension with Node 1: The dimension is constrained by LP SRAM (16KB). Bigger vectors mean fewer nodes. There's a capacity tradeoff.

## Node 10: Verified on Silicon
Every milestone was verified on real hardware with exact dot-for-dot comparison against CPU reference. This isn't simulation. The PCNT counts match. The CfC trajectories match. The VDB self-match is 64/64. This level of verification is unusual for hardware ML and is possible BECAUSE of the ternary precision — there's nothing to approximate.
Why it matters: Exactness is a feature of the ternary substrate. Binary floating point can't guarantee this.

## Node 11: The Stack Budget
16KB LP SRAM. Code is ~7KB, popcount LUT is 288B, CfC state is ~1KB, VDB nodes are 2KB, stack peak is 608B (VDB search). Total ~11KB used, ~5KB free. The M5 pipeline uses sequential frames (96B + 608B) so peak is still 608B. This is tight but not critical.
Why it matters: Every byte is accounted for. This is bare-metal in the truest sense. And yet it runs a complete neural network + vector database + graph search.

## Node 12: The Absence of Training
There is no training loop. No loss function. No backpropagation. The weights are random ternary values. The CfC dynamics emerge from the blend modes. The VDB learns by insertion, not gradient descent. This system was never trained — it was CONFIGURED.
Tension with Node 4: If the dynamics are useful without training, what would happen WITH training? Or is the absence of training the point?
