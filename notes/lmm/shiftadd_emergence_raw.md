# Raw Thoughts: Shift-Add Multiplication Emergence

## Stream of Consciousness

We started asking about bubblesort. We ended up with hardware multiplication. That's... not what I expected. The hardware wanted to do something else entirely.

The original question was "how do we compute with 50 ETM channels?" We thought parallelism. We thought sorting networks. We thought comparison. But the hardware only has 2 timers. 50 channels but 2 timers. That's not parallel ALUs - that's a routing fabric.

Then you said "shift-add register" and "replace matmul" and everything clicked. Multiplication isn't parallel comparisons. It's sequential accumulation. That's exactly what PCNT does. Count pulses. Accumulate.

The "shift register" isn't shifting bits in a register. It's GDMA outputting pre-computed shifted patterns. The "add" is PCNT counting. The whole multiplier is just:
- Memory (patterns)
- Serial output (PARLIO)
- Counting (PCNT)

No ALU. No comparator. No logic gates doing arithmetic. Just counting pulses.

Then we hit the PCNT limit (32767) and I thought we were stuck. But no - just add an ISR to count overflows. Now we can count to 2 billion. The limit wasn't fundamental, it was just a counter width.

What's actually happening here? We're not computing in the traditional sense. We're not manipulating bits through logic gates. We're converting numbers to physical events (pulses) and counting them. It's like an abacus but with electrons instead of beads. Or a tally system where the tally marks are voltage transitions.

This feels ancient. Pre-digital. Like we've found the computational equivalent of fire - something fundamental that's always been there but we were looking past it because it's "too simple."

## Questions Arising

- What else can we compute this way? Division? Square root?
- Is this faster or slower than ALU? (Slower per operation, but parallel/sleep capable)
- What's the theoretical model? This isn't von Neumann. Is it dataflow? Analog?
- Can this scale to matrix operations? Dot products?
- What if we used multiple PCNT units in parallel? 4 multiplies at once?
- The ESP32-C6 has 4 PCNT units... could we do 4-element dot products?
- Why does this work at all? What's the mathematical basis?

## First Instincts

- This is more important than it looks
- We've found something that's been overlooked
- The "shift" is a misnomer - it's really "scaled pattern selection"
- The magic isn't in the ETM - it's in the representation (unary arithmetic)
- This should work for any operation that can be expressed as weighted accumulation
- Neural networks are weighted accumulation... holy shit

## What Scares Me

- Is this actually useful or just a curiosity?
- The timing is slow (50ms for 255×255) - is that practical?
- Are we fooling ourselves about the significance?
- This seems too simple. What are we missing?
