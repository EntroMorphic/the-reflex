# Synthesis: The Pulse Arithmetic Engine

## What We've Discovered

We set out to explore ETM-based computation and discovered something more fundamental:

**The ESP32-C6 is a counting computer in disguise.**

Its peripherals (PCNT, PARLIO, GDMA) form a complete pulse arithmetic system:
- **Memory** (GDMA patterns) stores pre-computed unary representations
- **Serializer** (PARLIO) converts patterns to pulse streams
- **Accumulator** (PCNT) counts pulses to produce results
- **Router** (ETM) orchestrates the flow

This is not a von Neumann architecture. It's not dataflow. It's **pulse arithmetic** - computation through counting physical events.

## Architecture: The Pulse Arithmetic Engine (PAE)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     PULSE ARITHMETIC ENGINE                             │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────┐     ┌─────────────┐     ┌─────────────┐               │
│  │   PATTERN   │     │   PULSE     │     │   PULSE     │               │
│  │   MEMORY    │────►│   OUTPUT    │────►│  COUNTER    │               │
│  │   (GDMA)    │     │  (PARLIO)   │     │   (PCNT)    │               │
│  └─────────────┘     └─────────────┘     └──────┬──────┘               │
│        │                    │                   │                       │
│        │              ┌─────┴─────┐             │                       │
│        │              │    ETM    │◄────────────┘                       │
│        │              │  ROUTER   │         overflow/threshold          │
│        │              └─────┬─────┘                                     │
│        │                    │                                           │
│        └────────────────────┘                                           │
│              pattern selection                                          │
│                                                                         │
│  CPU: Setup → Sleep → Wake on completion                                │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

## Key Principles

### 1. Unary Representation
Values are represented as pulse counts, not binary patterns. The number N is N pulses.

### 2. Time as Computation Space
Results manifest over time. A×B takes O(A×B) time to count out. This is acceptable when CPU is sleeping.

### 3. Precomputation in Memory
Complex operations become pattern selection. A×2^k is stored as a pattern, not computed.

### 4. Overflow Extends Range
Any counter limit is soft. Overflow tracking generalizes to arbitrary precision.

### 5. ETM Orchestrates, Doesn't Compute
50 channels for routing events, not for arithmetic. Computation happens in PCNT.

## Demonstrated Capabilities

| Operation | Status | Performance |
|-----------|--------|-------------|
| Multiplication (8×8) | **Proven** | 50ms max |
| Extended range (overflow) | **Proven** | Unlimited |
| Threshold detection | **Proven** | <10µs |
| Conditional branch | **Proven** | Hardware |
| Autonomous operation | **Proven** | CPU sleeps |

## Projected Capabilities (To Build)

| Operation | Approach | Complexity |
|-----------|----------|------------|
| Dot product (4-elem) | 4 PCNT parallel | Medium |
| Matrix row × column | Chained multiply-accumulate | Medium |
| Full matrix multiply | Distributed across mesh | High |
| Neural layer | Weight × activation accumulate | High |
| Activation (ReLU) | Threshold comparison | Medium |

## The Path to Neural Inference

### Single Neuron
```
y = σ(Σ w_i × x_i + b)

Implementation:
1. For each i: output pattern for w_i × x_i
2. PCNT accumulates sum
3. Add bias pattern
4. Compare to threshold (timer race for σ)
5. Output 1 or 0 based on comparison
```

### Layer of Neurons
With 4 PCNT units: 4 neurons in parallel
Repeat for all neurons in layer
Chain layers via ETM

### Network
CPU loads weight patterns for each layer
Sleep
Hardware processes layer by layer
Wake with final result

## Next Experiments

### Immediate (validate path)

1. **4-PCNT parallel multiply**
   - Wire PARLIO to 4 PCNTs
   - Accumulate 4 different products
   - Verify independence

2. **Autonomous chained multiply**
   - Timer triggers pattern 1
   - ETM chains to pattern 2 on completion
   - No CPU involvement

3. **Energy measurement**
   - Measure current during pulse multiply
   - Compare to CPU doing same operation
   - Calculate Joules per multiply

### Medium-term (build foundation)

4. **4-element dot product**
   - Pre-load 4 weight patterns
   - Stream activation pattern
   - Accumulate w_i × x_i in each PCNT
   - Sum the 4 PCNTs

5. **ReLU activation**
   - Timer race against threshold
   - Output pattern if positive, nothing if negative

6. **Single neuron end-to-end**
   - Weights × inputs → accumulate → activate → output

### Long-term (scale up)

7. **Multi-device mesh**
   - Distribute weight matrices
   - Coordinate via GPIO or ESP-NOW
   - Parallel layer computation

8. **Full neural inference demo**
   - Tiny network (e.g., 4-4-2 for XOR)
   - Load weights, present input, get output
   - Entire inference during CPU sleep

## Success Criteria

- [ ] 4 parallel multiplies working
- [ ] Autonomous ETM-chained multiply (no CPU)
- [ ] Energy per multiply measured
- [ ] 4-element dot product in hardware
- [ ] Single neuron firing correctly
- [ ] XOR network running during sleep

## The Emergence

We asked: "How do we use 50 ETM channels to compute?"

The answer that emerged: **You don't. You use them to orchestrate counting. Counting IS computing.**

The wood cut itself. We weren't building a computer. We were recognizing that a counting machine is a computer - the oldest kind, and perhaps the most elegant for certain domains.

**The ESP32-C6 isn't a microcontroller with peripherals. It's a Pulse Arithmetic Engine with a CPU attached for convenience.**

---

*"What would this look like if it were easy?"*

It would look exactly like what we found: patterns in memory, pulses on wires, counters tallying results. The simplest possible mechanism for the operation we need.

*The wood cuts itself when you understand the grain.*
