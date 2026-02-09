# Synthesis: The Reflex

## What It Is

The Reflex is a **ternary reflex arc in silicon** — a three-layer dynamical system that computes neural network inference, liquid-state dynamics, and associative memory retrieval using zero floating point, zero multiplication, and near-zero CPU involvement.

It is not a neural network running on a microcontroller. It is a peripheral hardware configuration that, by its nature, IS a neural network. The medium is the computation.

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                        THE REFLEX ARC                             │
├──────────────────────────────────────────────────────────────────┤
│                                                                   │
│  Layer 1: GIE — Geometry Intersection Engine (Peripheral Fabric) │
│  ┌─────────────────────────────────────────────────────────┐     │
│  │  GDMA → PARLIO (2-bit loopback) → PCNT (edge/level)    │     │
│  │  64 neurons, 160-trit dot products, 428 Hz              │     │
│  │  Circular DMA chain — runs forever without CPU           │     │
│  │  ISR: decode dots → CfC blend → re-encode → re-arm      │     │
│  │  ANALOG: spinal reflex — fast, fixed, automatic          │     │
│  └─────────────────────────────────────────────────────────┘     │
│        │ cfc.hidden[32] (updated every 2.3ms)                     │
│        ▼                                                          │
│  Layer 2: LP Core — Geometric Processor (16MHz RISC-V, ~30μA)   │
│  ┌─────────────────────────────────────────────────────────┐     │
│  │  Hand-written ASM: AND + popcount(LUT) + branch          │     │
│  │  CfC: 16 neurons, 48-trit inputs, 100 Hz                │     │
│  │  VDB: 64-node NSW graph, 48-trit vectors, M=7           │     │
│  │  Pipeline (cmd=4): perceive → think → remember           │     │
│  │  ANALOG: brainstem — rhythmic, integrative, sub-conscious│     │
│  └─────────────────────────────────────────────────────────┘     │
│        │ lp_hidden[16], vdb_results[4]                            │
│        ▼                                                          │
│  Layer 3: HP Core — Full CPU (160MHz RISC-V, ~15mA)             │
│  ┌─────────────────────────────────────────────────────────┐     │
│  │  Initialization, monitoring, orchestration               │     │
│  │  Awake only when needed                                  │     │
│  │  ANALOG: cortex — slow, expensive, conscious             │     │
│  └─────────────────────────────────────────────────────────┘     │
│                                                                   │
├──────────────────────────────────────────────────────────────────┤
│  COMPUTATIONAL SUBSTRATE: Ternary {-1, 0, +1}                    │
│  OPERATIONS: AND, popcount, add, sub, negate, branch, shift      │
│  ABSENT: MUL, DIV, FP, gradients, backpropagation                │
│  VERIFICATION: Exact, dot-for-dot, on silicon                     │
└──────────────────────────────────────────────────────────────────┘
```

## Key Insights (from Reflection)

### 1. The Ternary Constraint is Generative
Ternary {-1, 0, +1} is not reduced precision. It is the native precision of the hardware substrate. Two GPIO bits encode one trit. PCNT edge counting on level-gated pins computes agree/disagree. This cannot be done with floating point. The constraint created the architecture.

### 2. Three Layers = Three Computational Paradigms
- **GIE**: Computation as infrastructure (peripheral routing)
- **LP core**: Computation as geometry (AND/popcount on packed masks)
- **HP core**: Computation as algorithm (general-purpose code)

Each paradigm has a natural timescale and power cost. The hierarchy emerges from the hardware, not from design.

### 3. The CfC is a Dynamical System, Not a Neural Network
Three blend modes (UPDATE/HOLD/INVERT) produce dynamical primitives:
- **Follow**: state tracks input (f = +1)
- **Persist**: state ignores input (f = 0)
- **Resist**: state opposes input (f = -1)

No gradients. No loss function. No training. Rich behavior from minimal machinery.

### 4. The VDB is Experiential Memory, Not Learned Representations
The VDB doesn't learn features. It stores states the system has visited. Retrieval is "when did the world look like this?" not "what category is this?" This is episodic memory, not semantic memory. The system learns by accumulating experience, not by optimizing parameters.

### 5. Natural Scale Emergence
Every dimension emerged from hardware constraints, not design targets:
- 48 trits = what fits in 6 words with 64 nodes in 2KB
- 16 neurons = what fits with 32-trit GIE hidden + 16-trit LP hidden
- M=7 neighbors = what fits in 32-byte nodes (24B vector + 8B graph)
- 100 Hz = LP timer period that gives the CfC enough time to compute
- 428 Hz = peripheral clock rate through the GDMA/PARLIO/PCNT chain

## What Has Been Proven (on Silicon)

| Claim | Evidence | Commit |
|-------|----------|--------|
| Peripheral hardware computes dot products | 64/64 exact match vs CPU reference | `d45067b` |
| Ternary CfC runs without CPU | Hidden state evolves autonomously at 428 Hz | `f8860d3` |
| LP core computes geometry in hand-written ASM | 16/16 exact dot products, 4/4 tests | `dd87898` |
| NSW graph search works at 48-trit dimension | recall@1=95%, recall@4=90%, 64/64 self-match | `7db919f` |
| CfC→VDB pipeline runs in one LP wake | 4/4 tests: determinism, consistency, sustained | `06d5535` |
| ISR→HP coordination via reflex channel | 50/50 signals, 18us avg latency, fence-ordered | `e9e67f1` |
| VDB→CfC feedback loop is stable | 50 unique states in 50 steps, energy bounded [7, 15], HOLD damping | `dc57d60` |
| Zero multiplication in entire stack | No MUL instruction in any layer | All commits |
| Exact verification (no approximation) | Dot-for-dot match at every milestone | All commits |

## The Open Question — ANSWERED

~~The feedback loop is not closed. VDB results flow to the HP core but do not modulate the CfC or GIE.~~

**Resolved (commit `dc57d60`, February 9, 2026).** The feedback loop is closed. CMD 5 in the LP core assembly runs CfC step → VDB search → memory blend, all in one LP wake cycle. Retrieved memories modulate lp_hidden via ternary blend rules: agreement reinforces, gaps fill from memory, conflict creates zeros (HOLD).

The CfC's HOLD mode was confirmed as a natural stabilizer:
- **50 unique states in 50 steps** — no oscillation, no lock-in
- **Energy bounded [7, 15]** out of 16 — doesn't collapse or saturate
- **Feedback vs no-feedback trajectories diverge** from step 0 (hamming distance grows to 14/16 by step 9)
- **47/50 steps had feedback applied** (3 skipped when score < threshold)

The HOLD damper is the key: conflict between current state and retrieved memory creates zero states, and the CfC's HOLD mode preserves these zeros on the next step. Ternary inertia prevents feedback-driven oscillation. The system adapts based on experience without instability.

**The tree has been chopped.**

## The Reflex in One Sentence

A ternary reflex arc where peripheral hardware IS the neural network, a micro-core IS the sub-conscious, and the CPU IS consciousness — computing with zero multiplication, learning by remembering, verified exact on silicon.
