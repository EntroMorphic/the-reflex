# Nodes of Interest: Shift-Add Multiplication Emergence

## Node 1: Unary Arithmetic Rediscovered

We're not doing binary arithmetic. We're doing unary arithmetic - where the value N is represented by N pulses. This is the oldest number system. Tally marks. Counting on fingers. We've rediscovered it in silicon.

**Why it matters:** Unary arithmetic has different complexity properties. Addition is O(1) - just concatenate pulse streams. Multiplication is O(n) - output one stream for each bit of the multiplier.

**Connection:** This explains why PCNT is the natural accumulator - it's literally a tally counter.

## Node 2: The Shift Is Not a Shift

When we say "shift-add multiplication," we think of shifting a register and adding. But we're not shifting anything. We're selecting from pre-computed scaled patterns.

A × 2^k isn't computed by shifting A. It's stored as a pattern of A×2^k pulses. The "shift" is just which pattern we pick.

**Why it matters:** This is table lookup, not computation. The multiplication is pre-computed and stored. Runtime just selects and accumulates.

**Tension with Node 1:** If it's lookup, is it really "computing"? Or just recalling?

## Node 3: Memory as Program

The GDMA patterns ARE the program. The bits of B select which patterns to output. This is like a punchcard where the holes are pre-computed multiplication results.

**Why it matters:** The "code" is in the data. The multiplication logic is stored in memory as pulse patterns. ETM just routes them.

**Connection to Node 2:** The patterns are the precomputed shifts.

## Node 4: Time as Arithmetic Space

In traditional computing, arithmetic happens in space (transistors). Here, arithmetic happens in time (pulse duration). The product A×B takes A×B microseconds to count out.

**Why it matters:** We've traded space for time. No transistors compute the multiply - time does.

**Insight:** 255×255 = 65025 pulses at 10MHz = 6.5ms just for the pulses. The ~50ms includes overhead, but the fundamental limit is that we're counting in real time.

## Node 5: PCNT as Universal Accumulator

PCNT doesn't care what it's counting. It just counts edges. We've used it for:
- Edge detection
- Threshold triggers
- State machines
- Now: arithmetic accumulation

**Why it matters:** PCNT is the primitive. Everything else (GDMA, PARLIO, ETM) just generates patterns for PCNT to count.

**The hierarchy:**
```
PCNT (accumulates) ← PARLIO (serializes) ← GDMA (stores patterns)
```

## Node 6: The Overflow Trick Generalizes

Overflow tracking via ISR extends any counter indefinitely. The 16-bit limit wasn't fundamental - it was just the counter width. With overflow tracking, we can count to 2^32 or beyond.

**Why it matters:** Any counting-based computation can scale arbitrarily.

**Implication:** If we can express a problem as counting, we can solve it without worrying about register width.

## Node 7: Four PCNT Units = Four Parallel Accumulators

ESP32-C6 has 4 PCNT units. We've only used one. What if we used all four?

**Why it matters:** 
- 4 simultaneous multiplications
- Or: 4-element dot product
- Or: 4 rows/columns of matrix multiply

**Tension:** Only one PARLIO TX. How to feed 4 PCNTs from one serial output?

**Resolution:** Wire splitter? Or 4 different GPIOs from software? Or time-multiplex?

## Node 8: The Sleep Question

Can CPU actually sleep during multiplication? Currently we're using parlio_tx_unit_wait_all_done() which blocks. But with timer-triggered GDMA and ETM chaining, could the entire multiply be autonomous?

**Why it matters:** If CPU can sleep during multiply, power efficiency becomes extraordinary.

**What's needed:**
- Timer triggers GDMA for each shift pattern
- ETM chains patterns automatically
- PCNT threshold signals completion
- CPU wakes to read result

## Node 9: Neural Networks Are Weighted Sums

A neuron computes: y = Σ(w_i × x_i) + bias

That's exactly what we've built:
- w_i × x_i = shift-add multiply
- Σ = PCNT accumulation

**Why it matters:** We might have a path to hardware neural inference during CPU sleep.

**Tension:** Neural nets need many neurons. How many multiplies can we parallelize?

## Node 10: The ETM's True Role

We started asking how to compute with 50 ETM channels. The answer: ETM doesn't compute. It routes.

ETM is a crossbar switch that connects events to tasks. The computation happens in:
- Memory (stored patterns)
- PCNT (counting)
- Time (pulse duration)

**Why it matters:** Stop trying to make ETM compute. Use it to orchestrate computation that happens elsewhere.

## Node 11: What Other Operations?

If multiplication is weighted accumulation, what else is?
- Dot product: multiple multiplies accumulated
- Convolution: sliding window of dot products
- Division: ???
- Square root: ???
- Comparison: timer race (we already have this)

**Why it matters:** Defines the boundary of what's computable this way.

**Key insight:** Anything expressible as "count weighted pulses" is in scope.

## Node 12: The Representation Determines the Operation

Binary representation → binary operations (AND, OR, XOR)
Unary representation → counting operations (accumulation)

We chose unary (pulses). That's why counting works. If we chose differently, different operations would be natural.

**Why it matters:** The medium shapes the message. Pulse counting naturally does summation.

## Node 13: Hardware Wants This

The ESP32-C6 peripherals (PCNT, PARLIO, GDMA) seem designed for exactly this use case. Pulse generation, pulse counting, pattern memory. It's like Espressif built a counting computer and called it "peripherals."

**Why it matters:** We're not fighting the hardware - we're using it as intended (even if Espressif didn't realize it).

## Node 14: The Simplicity Is Suspicious

This is too simple. Count pulses to multiply. No one does this. Why not?

**Possible reasons:**
- Speed: 50ms per multiply is slow
- Didn't think of it: pulse counting is "old fashioned"
- Not general purpose: only works for specific operations
- Power: generating pulses costs energy

**Counter-argument:** For sleep-compatible, low-power, autonomous computation, speed isn't the metric. Energy per operation is. And counting pulses during CPU sleep might be remarkably efficient.
