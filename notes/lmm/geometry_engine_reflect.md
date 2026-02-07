# Reflections: Geometry Intersection Engine

## The Core Insight

The twelve nodes collapse into one structural observation: **the ESP32-C6 peripheral fabric is a reconfigurable dataflow graph, and ETM is the switch fabric that defines its topology.**

We've been thinking bottom-up — "which peripheral does what." The reflection reveals a top-down structure: the system has three functional layers, and every resource belongs to exactly one of them.

```
Layer 3: TOPOLOGY    — ETM channels, REGDMA (defines what connects to what)
Layer 2: CHANNELS    — DMA, PARLIO, RMT, MCPWM, GPIO (move/transform shapes)
Layer 1: MANIFOLDS   — PCNT, ADC, Timers (measure intersections)
```

The computation IS the topology. Layer 3 programs Layer 2 which flows through Layer 1. Changing the topology changes the computation without changing the data.

## Resolved Tensions

### Node 1 vs Node 10: Synchronization — solved by clock domain

The synchronization fear dissolves when I look at Node 10. PARLIO and RMT both derive from APB clock via integer dividers. If both are set to 1MHz (APB/80), they are cycle-locked. The ETM trigger arrives on the same APB edge to both peripherals. The pipeline depth difference (if any) is a fixed offset — constant across all evaluations. A constant offset doesn't break the dot product; it shifts it by a fixed amount. We can compensate by offsetting the Y pattern in SRAM by that many symbols. Measure the offset once on silicon, adjust the Y pattern alignment, done forever.

Actually, the offset might even be zero. PARLIO TX and RMT TX both start on the next rising edge of their respective clocks after the ETM trigger. If both clocks are the same (APB/80 = 1MHz), and both are phase-aligned (derived from same APB), the first output appears on the same 1MHz edge. The only unknown is per-peripheral pipeline latency. This is measurable.

### Node 4 vs Node 5: REGDMA vs LP Core — they serve different scales

REGDMA is a single register copy. LP core is a programmable sequencer. They're not competing — they operate at different levels of the hierarchy.

- **REGDMA:** Rewrites one register per trigger. Good for advancing a descriptor pointer. Bad for complex conditional logic.
- **LP core:** Runs arbitrary code. Good for "if neuron result is positive, select weight matrix A, else select B." Bad for tight-loop register updates at DMA speeds.

The right answer: REGDMA handles the inner loop (advancing through a descriptor chain table within one layer). LP core handles the outer loop (reading layer results, deciding which layer to evaluate next, setting up the next phase's ETM topology).

### Node 8 vs Node 9: Measurement architecture — single-unit TMUL wins

Node 9 is clearly right. Using two PCNT units for TMUL was a leftover from our uncertainty about edge counting. With 2-bit PARLIO proven on silicon, we know exactly which edges appear on which GPIO. A single PCNT unit with Ch0=INC(agree) and Ch1=DEC(disagree) gives the net count directly. Sign of the count IS the TMUL result.

This frees 3 PCNT units. What to do with them?

- **Unit 0:** TMUL (INC/DEC, net count = dot product value)
- **Unit 1:** Threshold classifier (parallel, same inputs, different threshold → fires GPIO for next stage)
- **Unit 2:** Second independent dot product (different X or Y geometry → second neuron in parallel)
- **Unit 3:** Meta-counter (counts layer completions, watchdog events, diagnostic)

Two parallel dot products means two neurons evaluate simultaneously. With 2 PCNT units for TMUL and 2 for classification/meta, we get 2x throughput.

### Node 6: Sparse geometry — clock rate is the real lever

Variable-length descriptors add complexity for modest gain. Increasing the PARLIO clock from 1MHz to 10MHz gives 10x throughput with zero algorithmic complexity. At 10MHz, a 256-trit vector transmits in 25.6us. A 64-neuron layer takes 64 × ~30us = 1.9ms. That's 500+ layers per second. Good enough for real-time inference.

If we need more, go to 20MHz. PCNT can handle it (rated for 40MHz). The silicon verification is: does PCNT correctly count edges at 10-20MHz through GPIO loopback with level gating? One test answers this.

### Node 7: MCPWM — defer, but remember it

MCPWM is the most precise waveform generator on the chip, but it's register-driven, not DMA-driven. It requires REGDMA to feed new compare values per trit slot. That's a REGDMA transfer per trit × 3 operators × whatever symbol rate. Too much bus traffic. RMT is DMA-driven and simpler. Use RMT for Y geometry.

But MCPWM's dead-time generator is unique. If we ever need guaranteed non-overlap between Y_pos and Y_neg (insurance against "both HIGH" glitches), MCPWM is the only peripheral that enforces this in hardware. Keep it in reserve.

### Node 11: ETM topology as program

This is the deepest insight. The ETM configuration IS the program. Not the data in SRAM (that's the geometry). Not the peripheral configuration (that's the channel shape). The ETM wiring defines WHAT COMPUTATION the hardware performs.

If REGDMA can rewrite ETM channel registers between evaluation phases, the system can:

Phase 1 topology: "Compute dot products for layer 1"
Phase 2 topology: "Route results to classification, start layer 2"
Phase 3 topology: "Accumulate layer 2, signal completion"

Each phase is a different ETM configuration — a different program. REGDMA advances through a table of ETM configurations. The "instruction set" is the set of possible ETM topologies.

This is a reconfigurable computing fabric, not a fixed pipeline.

## What I Now Understand

The geometry engine has a natural three-level architecture:

**Level 1 — The Geometry Pipeline (proven on silicon)**
PARLIO(X) + static Y → PCNT → result
Single dot product per evaluation. CPU-sequenced between evaluations.
This is what we have today. It works.

**Level 2 — The Dual-Channel Engine (needs RMT verification)**
PARLIO(X) + RMT(Y) → PCNT(INC/DEC) → threshold → GPIO
Both geometries are DMA-driven. PCNT uses single-unit TMUL. Threshold events signal results on GPIO. Two parallel dot products possible with 4 PCNT units.
CPU kicks the evaluation, reads results. Semi-autonomous.

**Level 3 — The Self-Sequencing Fabric (needs REGDMA + LP core verification)**
REGDMA advances descriptor chain pointers between evaluations.
ETM topology reconfigures between phases.
LP core manages high-level sequencing and result routing.
Main CPU sleeps. Wakes on layer completion.
Fully autonomous multi-layer evaluation.

Each level builds on the one below. Each level has exactly one unknown that needs silicon verification before proceeding to the next. That's the build order.

## Remaining Questions

1. PARLIO-RMT phase alignment: how many APB cycles offset? (Level 2 blocker)
2. PCNT edge counting reliability at 10MHz through GPIO loopback? (Throughput question)
3. REGDMA descriptor format and latency? (Level 3 blocker)
4. LP core access to peripheral registers — same address space? (Level 3)
5. Can REGDMA target ETM channel registers? (Level 3 self-reconfiguration)

## The Simplicity Check

The synthesis should be: three levels, each with a clear silicon verification milestone, building toward a self-sequencing geometry intersection engine. If the spec is more complicated than that, I haven't reflected deeply enough.
