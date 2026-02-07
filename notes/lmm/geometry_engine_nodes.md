# Nodes of Interest: Geometry Intersection Engine

## Node 1: The Synchronization Cliff

Two independent waveform generators (PARLIO and RMT) must produce signals that align at the dibit level — 1us precision. ETM can trigger both from the same timer alarm, guaranteeing simultaneous start. But "simultaneous start" doesn't guarantee "simultaneous first output." PARLIO has a FIFO fill latency. RMT has a symbol load latency. The delta between first X edge and first Y level change determines whether the dot product is computed correctly or shifted by one or more positions.

Why it matters: A 1-slot phase shift turns a dot product into a shifted dot product — a completely different computation. This is either a fatal flaw or an exploitable feature (convolution is just a shifted dot product summed over offsets).

## Node 2: Three Classes of Resource

The 165 events and 165 tasks decompose into three functional classes:

**Generators** — peripherals that produce temporal waveforms on GPIO:
  PARLIO TX, RMT TX (×2), MCPWM (×3 operators), LEDC (×6), I2S TX

**Measurers** — peripherals that accumulate/quantify signal properties:
  PCNT (×4 units), ADC, Timer capture, MCPWM capture (×3)

**Connectors** — ETM channels, GPIO signal paths, REGDMA:
  ETM (×50), GPIO event (×8), GPIO task (×8), REGDMA (×4)

The engine is: Generators → (GPIO manifold) → Measurers, connected by ETM.
Every design decision is about which generators, which measurers, and how to connect them.

## Node 3: The Feedback Topology

PCNT threshold → ETM → GPIO task SET → GPIO event RISE → ETM → next PCNT INC.
That's a complete feedback loop in hardware. But the signal degrades at each hop:

- PCNT threshold is a single event (fires once when count crosses threshold)
- GPIO SET is a level (stays HIGH until explicitly cleared)
- GPIO RISE is a single event (fires once on LOW→HIGH transition)
- PCNT INC is a single bump (+1)

So each traversal of the feedback loop produces exactly one increment in the downstream PCNT. That's a 1-bit signal path. To communicate a full trit result (+1, 0, -1), you need two such paths (one for positive, one for negative) or a more complex encoding.

With 8 GPIO task channels and 8 GPIO event channels, we have 8 independent signal paths. That's 4 ternary signals (2 paths per trit) or 8 binary signals between stages.

## Node 4: The REGDMA Program Counter

REGDMA can copy register values between memory locations, triggered by ETM. If we set up a table of GDMA descriptor pointers in SRAM:

```
addr[0] = &desc_chain_neuron_0
addr[1] = &desc_chain_neuron_1
...
addr[N] = &desc_chain_neuron_N
```

And REGDMA is configured to copy addr[i] into the GDMA LINK register, then on each GDMA completion → ETM → REGDMA start, the next neuron's descriptor chain loads automatically.

But: how does REGDMA advance through the table? Does it have its own descriptor chain? If so, REGDMA descriptors form a meta-program — a program that programs the DMA that drives the geometry. Three levels of indirection: REGDMA descriptor → GDMA descriptor pointer → GDMA descriptor → pattern data.

Unknown: REGDMA latency, REGDMA descriptor format, whether REGDMA can target GDMA LINK registers, bus contention between REGDMA and GDMA.

## Node 5: The LP Core as Sequencer

The LP (low-power) RISC-V core runs at ~20MHz independently from the main core. It has its own SRAM (8KB), its own GPIOs (LP GPIOs), and ETM events/tasks. The main CPU can sleep while LP core manages the computation sequence.

LP core capabilities relevant to us:
- Read/write peripheral registers (including PCNT, GDMA, GPIO)
- Simple branching and loops
- Wake the main CPU via ETM task
- Run on ~100uA power

LP core could: set up descriptor chains, kick timers, read PCNT results, classify outputs, set up next neuron, loop. This is "sub-CPU" in the sense that the main 160MHz core is asleep. The computation happens in the peripheral fabric, sequenced by a 20MHz microcontroller.

Tension with Node 4: REGDMA is fully autonomous but limited to register copies. LP core is programmable but burns (some) power. Which is the right sequencer?

## Node 6: Sparse Geometry and Variable Descriptors

A 256-trit vector that's 90% zero wastes 90% of DMA bandwidth clocking out silence. Options:

**A. Accept it.** 256us per transfer at 1MHz. For 90% sparse vectors, 230us is silence. But silence costs zero PCNT counts, so the only waste is time and bus bandwidth.

**B. Variable-length descriptors.** Non-zero regions get their own short descriptors (4-16 bytes). Zero regions share one large zero buffer. Total DMA time depends on vector density. But descriptor-switch overhead adds jitter.

**C. Higher PARLIO clock.** Run at 10MHz or 20MHz instead of 1MHz. The same 64-byte pattern transmits in 25.6us or 12.8us. But PCNT must be able to count edges at this rate. PCNT max frequency on ESP32-C6 is ~40MHz (datasheet), so 20MHz should work.

Option C is the simplest and most impactful. 10x clock = 10x throughput. The question is whether PCNT can reliably count edges at 10-20MHz through the GPIO loopback path, and whether the level gating (Y lines) is fast enough to be stable during the faster edge transitions.

## Node 7: MCPWM as Ternary Waveform Generator

MCPWM has 3 timer/operator units, each producing two complementary outputs (A and B). The dead-time generator ensures A and B never overlap — when A is HIGH, B is LOW and vice versa, with configurable dead time between transitions.

Map this to ternary: A = positive, B = negative. Both LOW = zero. The dead-time generator naturally prevents the "both HIGH" invalid state. MCPWM timers can be synchronized to each other and to external events via ETM.

For Y geometry: MCPWM operator A → GPIO 6 (Y_pos), operator B → GPIO 7 (Y_neg). The PWM pattern encodes the Y vector. MCPWM timer period = one trit slot. Compare values determine whether the trit is +1 (A high, B low), -1 (A low, B high), or 0 (both low via output disable).

Advantage: MCPWM has hardware synchronization with sub-nanosecond precision between operators. No phase alignment problem.

Disadvantage: MCPWM patterns aren't DMA-driven — the compare values need to be updated per-trit. Unless we use ETM to trigger compare updates from a timer... which MCPWM supports (MCPWM_TASK_CMPR0_A_UP, etc.). But the new compare values come from registers, not SRAM. We'd need REGDMA to feed new compare values from SRAM → MCPWM registers on each trit clock.

This is getting complex. Maybe too complex for the value it adds over RMT.

## Node 8: The Measurement Problem

PCNT gives us a signed integer count. Currently we read it once after the watchdog fires. But PCNT also has:
- Threshold events (fires at a programmable count)
- Limit events (fires at the configured limit)
- Zero-crossing events (fires when count returns to zero)

These events can trigger ETM tasks. So the measurement isn't just a number — it's a stream of events. PCNT can signal "the dot product just crossed +40" or "the accumulation hit the limit" or "the positive and negative counts just balanced."

Multiple PCNT units with different thresholds could implement multi-level quantization:
- Unit 0: threshold at +20 → fires if result > +20 (strong positive)
- Unit 1: threshold at -20 → fires if result < -20 (strong negative)  
- Unit 2: threshold at +5 → fires if result > +5 (weak positive)
- Unit 3: threshold at -5 → fires if result < -5 (weak negative)

Four threshold events → four GPIO signals → next stage sees a 4-bit classification of the dot product result. That's not binary, not ternary — it's a 5-level quantized activation function computed entirely in hardware.

But wait. We only have 4 PCNT units total, and TMUL uses 2 of them (agree/disagree). That leaves 2 for multi-level classification. Unless we rethink: do we really need separate agree/disagree units? The PCNT INC/DEC channel actions could put agree and disagree into the SAME unit as increment and decrement. Then one PCNT unit computes TMUL directly as sign(count), freeing up 3 units for other purposes.

## Node 9: Single-Unit TMUL

Currently: Unit 0 counts agree (INC), Unit 1 counts disagree (INC). Result = sign(unit0 - unit1).

Alternative: One unit, two channels. Ch0 = agree path → INC on edge. Ch1 = disagree path → DEC on edge. The count IS the difference. Positive count = positive TMUL. Negative count = negative TMUL. Zero count = zero TMUL.

This uses 1 PCNT unit instead of 2. The threshold event at +N means "TMUL result is strongly positive." The threshold event at -N means "strongly negative." Zero crossing means "balanced."

This frees up 3 PCNT units for:
- A second parallel dot product (another geometry intersection)
- Multi-level quantization of the first result
- Monitoring/diagnostics
- Counting layer completions or other meta-events

Why didn't we do this originally? Because PCNT channels can only INC or DEC on edge, and the level action is KEEP or HOLD. To get Ch1 to DEC on X_pos edges gated by Y_neg, we need: edge=X_pos, level=Y_neg, edge_action=DECREASE, level_high=KEEP, level_low=HOLD. That should work — the PCNT channel supports DECREASE as an edge action.

This is a significant architectural improvement. One unit per TMUL instead of two.

## Node 10: The Clock Hierarchy

Everything runs from the same root clock (XTAL 40MHz → PLL → APB bus). But different peripherals divide this differently:

- PARLIO: configurable output clock, currently 1MHz
- RMT: configurable tick resolution, typically 1MHz-80MHz  
- MCPWM: configurable timer clock, up to 160MHz
- PCNT: samples on APB clock (80MHz), can count up to ~40MHz edges
- ETM: executes on APB clock (one event→task per APB cycle)
- GDMA: runs on APB clock, transfers at bus speed
- GPIO: output changes on APB clock, input sampled on APB clock

If PARLIO and RMT both derive their rate from APB via integer dividers, they will be phase-locked (same clock domain). The question is whether the latency from ETM trigger to first output is deterministic and identical for both peripherals. If PARLIO has a 3-cycle pipeline and RMT has a 5-cycle pipeline, there's a 2-cycle (25ns at 80MHz) offset. At 1MHz PARLIO clock, that's negligible. At 20MHz, it might matter.

## Node 11: ETM as a Topology, Not a Bus

I've been thinking of ETM as "event A triggers task B" — point-to-point wiring. But 50 channels with 165 event sources means the same event can be wired to multiple tasks via multiple channels. And the same task can be triggered by multiple events via multiple channels.

ETM is a crossbar switch. 165 × 165 possible connections, 50 of which can be active simultaneously. The configuration of those 50 active connections defines the computation topology.

Changing the ETM configuration between evaluation phases changes what the hardware computes. That's a different kind of "program" — not a sequence of instructions, but a reconfiguration of signal topology. Each topology is a different geometry intersection engine.

Could we use ETM self-modification? REGDMA could rewrite ETM channel registers between phases, changing the topology autonomously. The geometry engine wouldn't just run one program — it would rewire itself between stages. Phase 1: TMUL topology. Phase 2: accumulation topology. Phase 3: classification topology. All autonomously sequenced.

## Node 12: What is the Actual Bottleneck?

Where does the time go in a multi-neuron evaluation?

- DMA transfer: 256us at 1MHz (25.6us at 10MHz)
- ETM propagation: ~1 APB cycle = 12.5ns (negligible)
- PCNT readout: CPU reads register, ~50ns (negligible)
- Descriptor chain setup: CPU writes SRAM, ~1-10us
- PCNT clear: ETM task, ~12.5ns
- GPIO set for Y: CPU write, ~50ns (or RMT DMA if dynamic Y)

At 1MHz: dominated by DMA transfer time (256us).
At 10MHz: DMA drops to 25.6us, descriptor setup becomes significant.
At 20MHz: DMA at 12.8us, descriptor setup dominates.

For fully autonomous operation (no CPU between neurons), the bottleneck shifts to REGDMA latency — how fast can it reprogram GDMA for the next descriptor chain?

The real bottleneck might be SRAM bandwidth. GDMA reads patterns from SRAM. If we run 3 GDMA channels simultaneously (X pattern + Y_pos RMT + Y_neg RMT), all three compete for the SRAM bus. At 80MHz bus clock with 32-bit width, that's 320MB/s peak. At 10MHz PARLIO, X needs 640KB/s (trivial). RMT at 1M symbols/s × 2 bytes = 2MB/s per channel. Total: ~5MB/s. Bus is 320MB/s. Not even close to saturated.

The bottleneck is the PARLIO clock rate, which determines how fast shapes flow through the channel. Everything else is fast enough.
