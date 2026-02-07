# Raw Thoughts: Programmable Geometry Intersection Engine on ESP32-C6

## Stream of Consciousness

The first thing that hits me is the sheer asymmetry of what we've used versus what's available. 3 of 50 ETM channels. We've been looking through a keyhole at a cathedral. But that doesn't mean we should use all 50. The question is which ones compose into something coherent.

The geometry framing changes what "resource allocation" means. We're not assigning peripherals to tasks. We're designing channels through which shapes flow, intersect, and produce new shapes. The peripherals aren't workers — they're the topology of the space.

I'm scared of synchronization. Everything we've proven works because PARLIO clocks data at a known rate and PCNT samples edges synchronously with GPIO changes. The moment we add a second waveform generator (RMT) running on a different internal clock, phase alignment becomes the central problem. Two shapes that are supposed to intersect at the same moment might be offset by a few clock cycles. At 1MHz that's microseconds — enough to shift which dibit of X meets which symbol of Y.

The ETM fan-out is interesting. One event can trigger multiple tasks simultaneously. Timer0 alarm → GDMA start AND RMT start AND PCNT clear, all in the same clock cycle. That's how you synchronize the launch. But does "same clock cycle" actually mean same clock cycle on silicon? We know ETM works for single-target triggering. Multi-target fan-out from one event is the first thing to verify.

REGDMA nags at me. It can copy register values between peripherals. That's not just "DMA for registers" — it's the ability to reconfigure a peripheral autonomously. If REGDMA can rewrite the GDMA descriptor pointer, then the system can advance through a sequence of geometry programs without CPU. That's the program counter. But I have no idea if REGDMA is fast enough, or if it causes bus contention with GDMA, or if it even works the way I think it does. Zero silicon experience with REGDMA.

GPIO ETM tasks feel like the nervous system. 8 task channels, each can set/clear/toggle any GPIO. 8 event channels, each can detect edges on any GPIO. That's 8 bidirectional signal paths that can route computation results between stages. PCNT threshold reached → GPIO set → next stage's PCNT sees the edge. But GPIO has propagation delay. How fast does a GPIO ETM task execute? If it's 1 clock cycle at 160MHz, that's 6.25ns — way faster than PARLIO's 1us per dibit. If it's slower, it might miss the timing window.

The PCNT direct increment/decrement tasks (IDs 85, 86) are wild. ETM can bump PCNT without any GPIO edge. That means PCNT can count events from anywhere — timer overflows, GDMA completions, other PCNT thresholds. PCNT becomes a general-purpose event counter, not just an edge counter. You could count how many descriptor chains have completed, or how many neurons have fired positive.

MCPWM has 30 events and 22 tasks. I keep ignoring it because it's "motor control" but it's really 3 independent timer/comparator/generator units. The deadtime generators could create complementary waveforms (one line goes HIGH when another goes LOW) which is exactly the +1/-1 ternary encoding. MCPWM could potentially drive the Y geometry lines with precise timing relative to PARLIO.

The LP (low-power) core is a separate RISC-V processor that runs independently. It has ETM events (start, error) and one task (wake main CPU). Could the LP core handle the neuron sequencing while the main CPU sleeps? LP core is slow (~20MHz) but it could manage descriptor chain setup between evaluations while consuming almost no power. The "sub-CPU" constraint might be satisfied by using the LP core as a tiny sequencer — it's not the main CPU, it's a separate processor.

I2S has DMA and can drive GPIOs with precise clocking. I2S TX could potentially be another geometry channel — a third waveform generator. But I2S is typically 16-bit or 32-bit words, not 2-bit dibits. The bit width mismatch might make it useless for ternary encoding.

The descriptor chain length is limited by SRAM. Each lldesc_t is 12 bytes. For a 256-neuron layer with 256-trit weight vectors: 256 descriptors × 12 bytes = 3KB for descriptors, 256 × 64 bytes = 16KB for patterns. Total 19KB per layer. 512KB SRAM fits ~26 layers. That's a serious neural network entirely in SRAM.

But do we actually want a neural network? The geometry framing suggests something more general. A dot product is one kind of intersection. What about convolution — sliding one shape past another? What about correlation — measuring similarity at different offsets? What about transform — reshaping one geometry based on another? The hardware primitives (DMA, waveform generation, edge counting, level gating) might support operations we haven't thought of yet.

The watchdog pattern bothers me. Right now the CPU waits in a NOP loop for 2ms. That's not really "sub-CPU" — it's "CPU doing nothing useful." True sub-CPU would be: CPU starts the computation, does something else (or sleeps), gets interrupted when it's done. PCNT threshold event → ETM → GPIO set → GPIO interrupt → CPU wakes up. The CPU doesn't poll. The hardware signals completion.

What about error handling? If a DMA chain fails, or PCNT overflows, or RMT underruns — in a fully autonomous pipeline, who notices? ETM can route error events to GPIO pins or to timer stops, but the error information is limited to "something went wrong." No error codes, no register dumps. The system either works or stops. That's actually fine for a geometry engine — a shape either intersects or it doesn't. There's no "partial intersection error."

The temperature sensor and ADC have ETM events. That's interesting. ADC can measure an analog voltage and fire an event when it crosses a threshold. Temperature can fire when the chip overheats. These could be used as environmental inputs to the geometry computation — the physical world feeding into the ternary pipeline. A sensor reading above threshold sets a GPIO, which gates a PCNT channel, which modifies the next neuron's accumulation. The boundary between "computation" and "sensing" dissolves.

I keep coming back to the 0x00 ground state. The SRAM is 512KB of silence. Each non-zero byte is a perturbation. The geometry engine reads perturbations and measures their interactions. Most of SRAM will be zeros — the shapes are sparse. The DMA bandwidth is mostly spent clocking out silence. That feels wasteful, but it's also what gives the shapes their temporal positions. The silence is structural.

Could we use variable-length descriptors to skip the silence? A descriptor with length=4 bytes transmits only 4 bytes, then GDMA moves to the next descriptor. String together: 4 bytes of shape, 60 bytes of skip (just a descriptor pointing to 60 zero bytes in a shared zero buffer), 4 bytes of next shape. The "skip" descriptors all point to the same zero region in SRAM — you don't need separate zero buffers. One 64-byte zero buffer shared by all skip descriptors. The shapes sit in their own small buffers. Total SRAM for the shapes: only the non-zero bytes. This is run-length encoding via descriptor chains.

That's clever but it might break the timing. Each descriptor has DMA setup overhead. If we need microsecond-precise positioning of shapes within the temporal stream, extra descriptors add jitter. Need to measure descriptor-switch latency on silicon.

## Questions Arising

- Does ETM fan-out (one event → multiple tasks) actually execute simultaneously?
- What is REGDMA's latency? Can it rewrite GDMA config between descriptor chains fast enough?
- Can RMT and PARLIO be clocked from the same source at exactly the same rate?
- What is GPIO ETM task latency?
- Does the LP core have enough capability to sequence neuron evaluations?
- Can MCPWM generate ternary-compatible waveforms synchronized to PARLIO?
- What is the descriptor-switch latency in GDMA?
- How does PCNT behave when ETM directly increments/decrements it (tasks 85/86)?
- Can multiple PCNT units watch different thresholds and fire different ETM events simultaneously?
- Is REGDMA even documented well enough to use bare-metal?

## First Instincts

- Start with what we know works: PARLIO + PCNT + GDMA + ETM
- Add RMT as the Y geometry channel — it's the lowest-risk addition
- Use GPIO ETM tasks for inter-stage signaling
- Defer REGDMA until we understand it better
- Defer MCPWM — too much unknown
- Keep the LP core as a stretch goal
- The architecture should be layered: prove each new primitive on silicon before composing them
- Don't try to build the full engine at once. Build the next smallest thing that teaches us something new.
