# Reflections: Shift-Add Multiplication Emergence

## The Core Insight

**We haven't built a multiplier. We've rediscovered counting.**

The ancient insight: any quantity can be represented by a count of identical units. The number 5 is ||||| tally marks. Multiplication is repeated addition. 5 × 3 is ||||| + ||||| + ||||| = 15 marks.

Our "shift-add multiplier" is exactly this:
- The number A is stored as A pulse patterns
- For each bit of B, we output the appropriate scaled pattern
- PCNT tallies the total

We're not computing. We're counting. The multiplication "happens" because counting is isomorphic to addition, and repeated addition is multiplication.

**This is not a clever hack. It's a fundamental truth about computation that we've been overlooking because "counting is too primitive."**

## Resolved Tensions

### Node 2 vs Node 1 (Is it computing or lookup?)

**Resolution:** It's both. The "lookup" is retrieving the pre-computed unary representation. The "computing" is the accumulation (counting). The multiplication is split:
- Compile time: compute A×2^k and store as patterns
- Runtime: select patterns based on B, count total pulses

This is analogous to how modern CPUs use lookup tables for multiplication. We've just made the "table" physical (pulse patterns in memory).

### Node 4 vs Speed Concerns (Is time-based arithmetic practical?)

**Resolution:** It depends on the use case. For:
- Real-time DSP: No, too slow
- Sleep-compatible sensing: Yes, perfect
- Neural inference during deep sleep: Potentially transformative
- Energy-constrained applications: Might be optimal (Joules per operation, not operations per second)

The metric isn't FLOPS. It's **operations per Joule** or **operations while sleeping**.

### Node 7 (Parallel PCNTs) vs Node 8 (Single PARLIO)

**Resolution:** Multiple strategies:
1. **Wire splitting:** One PARLIO output to 4 PCNT inputs. All count the same stream. Useful for redundancy, not parallelism.
2. **Time multiplexing:** Round-robin across 4 PCNTs. Still serial, but accumulates 4 separate totals.
3. **Software pre-load:** CPU loads different patterns, sleeps, all 4 PCNTs accumulate different things. Wake to combine.
4. **External routing:** Use discrete logic (mux) to route PARLIO to different PCNTs based on ETM signals.

The path to 4-wide parallelism exists but requires additional work.

## The Hierarchy of Abstraction

Looking at the nodes as a system, a hierarchy emerges:

```
Level 3: Application (Neural network, Matrix multiply)
    ↓
Level 2: Operation (Multiply-accumulate, Dot product)
    ↓
Level 1: Mechanism (Pulse generation → Counting)
    ↓
Level 0: Physics (Voltage transitions, Capacitor counting)
```

We've been working at Level 1, discovering that Level 2 operations emerge naturally from pulse counting. The question is: how much of Level 3 can be built on this foundation?

## What I Now Understand

### 1. Unary Arithmetic Is Natural Here

The ESP32-C6 peripherals are pulse-oriented. PCNT counts pulses. PARLIO generates pulses. This is a counting machine. Trying to do binary logic with it is fighting the hardware. Embrace unary.

### 2. Time Is a Computational Resource

In traditional computing, time is an enemy (latency). Here, time is the medium of computation. The answer literally takes A×B time units to produce. This is fine if you're sleeping anyway.

### 3. The Overflow Pattern Generalizes

Overflow tracking (count the counter rollovers) works for any counting operation. This means:
- Arbitrarily large products
- Arbitrarily long integration times
- Arbitrarily complex weighted sums

The 16-bit PCNT is a detail, not a limit.

### 4. ETM Is an Orchestrator, Not a Computer

50 ETM channels = 50 ways to route events. Use them to:
- Chain operations (pattern 1 done → start pattern 2)
- Handle exceptions (overflow → increment counter)
- Signal completion (total reached → wake CPU)

Don't use them to "compute."

### 5. Neural Networks Are In Scope

A neuron: y = σ(Σ w_i × x_i + b)

The weighted sum (Σ w_i × x_i) is exactly what we've built:
- w_i × x_i = shift-add multiply (demonstrated)
- Σ = PCNT accumulation (demonstrated)
- σ (activation) = ??? (needs threshold comparison, which is timer race?)
- b (bias) = additional pulse pattern

**A hardware neuron during CPU sleep is architecturally possible.**

### 6. The Boundary of This Approach

What's IN scope (expressible as weighted counting):
- Addition, Multiplication (demonstrated)
- Dot products, Matrix multiply
- Convolution (sliding dot products)
- Weighted averages
- Integration
- Counting, Thresholding

What's OUT of scope (not naturally counting):
- Division (inverse operation - unclear)
- Square root (would need iterative approximation)
- Arbitrary logic (AND, OR, XOR on bits)
- Branching based on values (though threshold branch works)

## The Emergent Question

If a single ESP32-C6 can do:
- 255×255 in 50ms during idle
- 4 parallel multiplies with 4 PCNT units
- Chained multiply-accumulate for dot products

Then a **mesh of ESP32-C6 devices** could do:
- 4 multiplies × N devices = 4N parallel multiplies
- Matrix multiplication distributed across mesh
- All while CPUs sleep

**Is this a path to distributed, ultra-low-power neural inference?**

## Remaining Questions

1. **Energy measurement:** What's the actual Joules per multiply? Compare to CPU/GPU.

2. **Autonomous chaining:** Can ETM chain multiple multiplies without CPU wakeup?

3. **Activation functions:** How to implement sigmoid/ReLU as pulse operations?

4. **Mesh coordination:** How do devices synchronize for parallel matrix ops?

5. **Precision:** 8-bit × 8-bit is limited. Can we do 16-bit with nibble decomposition?

6. **Training vs Inference:** Inference seems possible. Training requires gradients - is that in scope?
