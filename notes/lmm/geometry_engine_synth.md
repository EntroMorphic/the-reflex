# Synthesis: Geometry Intersection Engine — ESP32-C6

## Name

**GIE** — Geometry Intersection Engine

## Architecture

Three levels, each proven on silicon before the next begins.

```
┌─────────────────────────────────────────────────────────────────┐
│  LEVEL 3: SELF-SEQUENCING FABRIC                                │
│  LP core sequences layers. REGDMA advances descriptor tables.   │
│  ETM topology reconfigures between phases. Main CPU sleeps.     │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │  LEVEL 2: DUAL-CHANNEL ENGINE                               │ │
│  │  PARLIO(X) + RMT(Y) → PCNT(INC/DEC) → threshold → GPIO    │ │
│  │  Two simultaneous dot products. Hardware classification.     │ │
│  │  ┌─────────────────────────────────────────────────────────┐ │ │
│  │  │  LEVEL 1: SINGLE-UNIT GEOMETRY PIPELINE                 │ │ │
│  │  │  PARLIO(X) + static Y → PCNT(INC/DEC) → result          │ │ │
│  │  │  One dot product. 256 trits. Single-unit TMUL.           │ │ │
│  │  │  PROVEN: Milestone 4 (9/9 TMUL on silicon)              │ │ │
│  │  └─────────────────────────────────────────────────────────┘ │ │
│  └─────────────────────────────────────────────────────────────┘ │
│                                                                   │
│  SRAM: 512KB geometry space (ground state = 0x00)                │
│  ETM: 50 channels = reconfigurable topology                      │
│  GPIO: 8 event + 8 task channels = inter-stage signal bus        │
└─────────────────────────────────────────────────────────────────┘
```

## Resource Allocation

### Level 1 — Single-Unit Geometry Pipeline

**What changes from current Milestone 4 code:**
Collapse TMUL from 2 PCNT units to 1. Redesign patterns for 1 dibit = 1 trit.

```
PERIPHERALS:
  PARLIO TX        → GPIO 4 (X_pos), GPIO 5 (X_neg)     [2-bit mode]
  GPIO 6, 7        → Y_pos, Y_neg                         [CPU-driven static]
  PCNT Unit 0      → TMUL: Ch0=INC(agree), Ch1=DEC(disagree)
  PCNT Unit 1      → Diagnostics: X_pos edge count (no gating)
  GPTimer 0        → Kickoff trigger
  GPTimer 1        → Watchdog
  GDMA CH0         → PARLIO TX data

ETM WIRING (5 channels):
  Ch 0: Timer0 alarm       → GDMA CH0 start      [kick DMA]
  Ch 1: Timer0 alarm       → PCNT reset           [clear accumulator]
  Ch 2: Timer1 alarm       → Timer0 stop          [watchdog]
  Ch 3: GDMA CH0 total EOF → Timer1 stop          [auto-stop on completion]
  Ch 4: (prime)             → GDMA CH0 start       [dummy first-trigger]

PATTERN ENCODING (1 dibit = 1 trit):
  Each byte: 4 trit slots
  dibit 01 = trit +1 (X_pos edge)
  dibit 10 = trit -1 (X_neg edge)
  dibit 00 = trit  0 (silence)
  64 bytes = 256 trits per descriptor

PCNT UNIT 0 CONFIGURATION:
  Ch0: edge=GPIO4(X_pos), level=GPIO6(Y_pos)
       edge_pos=INCREMENT, edge_neg=HOLD
       level_high=KEEP, level_low=HOLD
  Ch1: edge=GPIO4(X_pos), level=GPIO7(Y_neg)
       edge_pos=DECREMENT, edge_neg=HOLD
       level_high=KEEP, level_low=HOLD

  Wait — this only catches X_pos edges gated by Y. We also need
  X_neg edges. But PCNT unit has only 2 channels.

  Rethink: For single-unit TMUL with both X_pos and X_neg, we need
  to handle 4 cases: X_pos×Y_pos(+1), X_neg×Y_neg(+1),
  X_pos×Y_neg(-1), X_neg×Y_pos(-1).

  With 2 channels and static Y, only one Y line is active per test.
  When Y=+1 (Y_pos HIGH, Y_neg LOW):
    Ch0: edge=X_pos, level=Y_pos → INC (agree: +1 × +1)
    Ch1: edge=X_neg, level=Y_pos → DEC (disagree: -1 × +1)
    Net count = (X_pos edges) - (X_neg edges) = correct TMUL

  When Y=-1 (Y_pos LOW, Y_neg HIGH):
    Ch0: edge=X_pos, level=Y_neg → INC? No — this should DEC.
    Problem: Ch0 always INC on edge. We can't change it per-test.

  Actually we can rethink the channel actions:
    Ch0: edge=X_pos, level=Y_pos → INC when Y_pos HIGH, HOLD when LOW
    Ch1: edge=X_neg, level=Y_neg → INC when Y_neg HIGH, HOLD when LOW

  This only gives agree counts (both positive). Where's disagree?

  The single-unit approach needs all 4 gating combinations in 2 channels.
  That's impossible with static Y — each channel has one edge input and
  one level input. 2 channels = 2 combinations. We need 4.

  RESOLUTION: Keep 2-unit TMUL for Level 1. The single-unit optimization
  requires dynamic Y (Level 2) where the X pattern encodes the pre-
  multiplied result, not raw X values. OR accept 2 units for TMUL.

  Actually — simpler path: with static Y, only ONE of Y_pos/Y_neg
  is HIGH at a time. So only 2 of 4 combinations are active per test.
  Two channels suffice:

  When Y=+1:
    Ch0: edge=X_pos, level=Y_pos → INC  (X_pos counts as +1)
    Ch1: edge=X_neg, level=Y_pos → DEC  (X_neg counts as -1)
    Result: net = (+1 edges) - (-1 edges) = TMUL

  When Y=-1:
    Ch0: edge=X_pos, level=Y_neg → INC... but this should be -1.
    The PROBLEM: INC on X_pos gated by Y_neg gives POSITIVE count,
    but (+1)×(-1) should be NEGATIVE.

  The sign flip for Y=-1 needs to happen somewhere. Options:
    A. Swap Ch0/Ch1 edge actions when Y=-1 (CPU sets this before test)
    B. Use level_low=KEEP, level_high=HOLD (invert the gating)
    C. Negate the PCNT result in software when Y=-1

  Option B: Configure both channels with INVERTED level logic:
    Ch0: edge=X_pos, level=Y_pos
         level_high=KEEP (count when Y_pos HIGH → Y=+1)
         level_low=HOLD  (ignore when Y_pos LOW)
    Ch1: edge=X_neg, level=Y_pos
         level_high=DECREMENT... no, level action is KEEP or HOLD,
         not INC/DEC. Level action just gates whether the edge counts.

  I think the level actions are:
    KEEP = allow the edge action to apply
    HOLD = suppress the edge action (edge is ignored)

  So level gating is binary: either the edge counts or it doesn't.
  The sign of the count comes from the edge action (INC or DEC).

  For Y=+1 (Y_pos HIGH):
    Ch0: edge=X_pos, level=Y_pos, edge_pos=INC, level_high=KEEP
         → counts X_pos edges positively ✓
    Ch1: edge=X_neg, level=Y_pos, edge_pos=DEC, level_high=KEEP
         → counts X_neg edges negatively ✓
    Net = X_pos - X_neg. For X=+1 pattern: net > 0 → TMUL=+1 ✓
    For X=-1 pattern: net < 0 → TMUL=-1 ✓
    For X=0: net = 0 → TMUL=0 ✓

  For Y=-1 (Y_neg HIGH, Y_pos LOW):
    Ch0: gated by Y_pos=LOW → HOLD → no counts
    Ch1: gated by Y_pos=LOW → HOLD → no counts
    Net = 0. WRONG. Should be -X.

  The Y_neg line isn't being used at all by this config!

  CONCLUSION: Single-unit TMUL with static Y requires dedicating
  channels to BOTH Y_pos and Y_neg gating. That's 4 channels minimum
  (X_pos×Y_pos, X_neg×Y_pos, X_pos×Y_neg, X_neg×Y_neg). PCNT only
  has 2 channels per unit.

  **FINAL DECISION: Keep 2-unit TMUL for Level 1.**
  Unit 0: agree (INC on X_pos gated by Y_pos + INC on X_neg gated by Y_neg)
  Unit 1: disagree (INC on X_pos gated by Y_neg + INC on X_neg gated by Y_pos)
  Result = sign(Unit0 - Unit1)

  This leaves 2 PCNT units free for diagnostics or parallel eval.
```

**Silicon verification for Level 1:**
1. Redesigned patterns (1 dibit = 1 trit, 256 trits per buffer)
2. PCNT correctly counts single edges at 1MHz (not amplified 208x)
3. Descriptor chain accumulation (10 descriptors, verify sum)
4. GDMA auto-stop via ETM on total EOF (no watchdog needed)

**Milestone 5 criteria: 256-trit dot product verified on silicon.**

---

### Level 2 — Dual-Channel Engine

**What's new:** RMT drives Y geometry. Both X and Y are DMA-driven.
PCNT threshold events route results to GPIO. Two parallel TMUL possible.

```
PERIPHERALS:
  PARLIO TX        → GPIO 4 (X_pos), GPIO 5 (X_neg)     [GDMA CH0]
  RMT TX Ch0       → GPIO 6 (Y_pos)                      [GDMA CH1]
  RMT TX Ch1       → GPIO 7 (Y_neg)                      [GDMA CH2]
  PCNT Unit 0      → TMUL neuron A: agree
  PCNT Unit 1      → TMUL neuron A: disagree
  PCNT Unit 2      → TMUL neuron B: agree (or threshold classifier)
  PCNT Unit 3      → TMUL neuron B: disagree (or meta-counter)
  GPTimer 0        → Kickoff trigger
  GPTimer 1        → Watchdog / layer timer
  GPIO 8-11        → Result output bus (4 ternary signals)

ETM WIRING (12 channels):
  Ch 0:  Timer0 alarm        → GDMA CH0 start     [X geometry launch]
  Ch 1:  Timer0 alarm        → GDMA CH1 start     [Y_pos geometry launch]  
  Ch 2:  Timer0 alarm        → GDMA CH2 start     [Y_neg geometry launch]
  Ch 3:  Timer0 alarm        → PCNT reset          [clear all accumulators]
  Ch 4:  GDMA CH0 total EOF  → Timer0 stop         [X done]
  Ch 5:  PCNT0 thresh(+N)    → GPIO task CH0 SET   [neuron A positive]
  Ch 6:  PCNT1 thresh(+N)    → GPIO task CH1 SET   [neuron A negative]
  Ch 7:  PCNT0 eq zero       → GPIO task CH2 SET   [neuron A zero]
  Ch 8:  Timer1 alarm        → GPIO task CH0 CLR   [reset result bus]
  Ch 9:  Timer1 alarm        → GPIO task CH1 CLR
  Ch 10: Timer1 alarm        → GPIO task CH2 CLR
  Ch 11: GDMA CH0 total EOF  → Timer1 reload       [arm next-layer timer]

  Channels 12-19: Mirror of 0-11 for neuron B (PCNT units 2,3)
  Total: ~20 channels for dual-neuron evaluation

SRAM LAYOUT:
  X patterns:     64 bytes × N vectors
  Y_pos symbols:  512 bytes × M vectors (RMT format: 256 × 2-byte symbols)
  Y_neg symbols:  512 bytes × M vectors
  X descriptors:  12 bytes × N
  Y descriptors:  12 bytes × M × 2 (pos + neg)

  For a 256×256 weight matrix (one layer):
    X (weight rows): 256 × 64 = 16,384 bytes (16KB)
    Y (input vector): 1 × 512 × 2 = 1,024 bytes (1KB)
    Descriptors: 256 × 12 + 2 × 12 = 3,096 bytes (3KB)
    Total per layer: ~20KB
    SRAM fits: ~25 layers simultaneously

RMT SYMBOL ENCODING:
  Each RMT symbol: 16 bits = {level(1), duration(15)}
  For 1MHz trit clock: duration = 1 (one tick at 1MHz RMT resolution)
  Y=+1 at slot k: Y_pos channel symbol = {level=1, duration=1}
                   Y_neg channel symbol = {level=0, duration=1}
  Y=-1 at slot k: Y_pos = {level=0, dur=1}, Y_neg = {level=1, dur=1}
  Y=0  at slot k: both = {level=0, dur=1}

  256 trits × 2 bytes per symbol × 2 channels = 1,024 bytes per Y vector.
  Compare: X vector = 64 bytes. Y vector is 16x larger due to RMT encoding.
  Acceptable for SRAM. Not ideal for bandwidth.

  OPTIMIZATION: RMT run-length encoding. Consecutive same-value trits
  merge into one symbol with longer duration. A Y vector of all +1 becomes
  a single symbol {level=1, duration=256} = 2 bytes instead of 512.
  Sparse Y vectors compress well. Dense alternating vectors don't compress.
```

**Silicon verification for Level 2:**
1. RMT TX drives GPIO 6/7 with known test pattern
2. ETM simultaneous trigger of PARLIO + RMT (measure phase offset)
3. PCNT correctly counts the intersection of two DMA-driven geometries
4. PCNT threshold event → GPIO set (result signaling works)
5. Two parallel TMUL evaluations on PCNT units 0-1 and 2-3

**Milestone 6 criteria: Dual-geometry 256-trit dot product, both DMA-driven, with hardware result classification, verified on silicon.**

---

### Level 3 — Self-Sequencing Fabric

**What's new:** Autonomous layer evaluation. REGDMA advances the program.
LP core manages inter-layer logic. Main CPU sleeps.

```
PERIPHERALS (additions to Level 2):
  REGDMA CH0      → Advances GDMA CH0 descriptor pointer (X program)
  REGDMA CH1      → Advances GDMA CH1 descriptor pointer (Y_pos program)  
  REGDMA CH2      → Advances GDMA CH2 descriptor pointer (Y_neg program)
  REGDMA CH3      → Advances ETM configuration (topology switch)
  LP Core          → Inter-layer sequencer

ETM WIRING (30+ channels):
  Channels 0-19:   Level 2 wiring (dual-neuron evaluation)
  Ch 20: GDMA CH0 total EOF → REGDMA CH0 start   [advance X to next neuron]
  Ch 21: REGDMA CH0 done    → Timer0 reload        [re-arm kickoff]
  Ch 22: Timer0 alarm        → GDMA CH0 start      [kick next neuron]
  Ch 23: REGDMA CH0 done    → PCNT reset           [clear for next neuron]

  This creates an autonomous loop:
    DMA finishes → REGDMA loads next descriptor → Timer re-arms →
    Timer fires → DMA starts next neuron → PCNT accumulates →
    DMA finishes → ...

  The loop continues until REGDMA runs out of descriptor table entries.
  REGDMA total done → ETM → LP core wakeup event → LP core reads results.

  Ch 24-29: Result routing
    PCNT thresh → GPIO SET → (routed to next-layer PCNT or LP core GPIO)
    
  Ch 30-35: Layer completion signaling
    REGDMA total done → GPIO → LP core interrupt
    LP core reads PCNT values, decides next layer
    LP core reconfigures REGDMA for next layer's descriptor table
    LP core kicks Timer0 → next layer begins

  Channels 36-50: Reserved for:
    - Error handling (GDMA error → stop everything)
    - Diagnostic counters
    - ADC/sensor input integration
    - Future expansion

LP CORE PROGRAM (pseudocode):
  while (layers_remaining > 0):
    configure_regdma(layer[i].descriptor_table)
    configure_etm(layer[i].topology)   // may use REGDMA CH3
    kick_timer0()
    sleep_until(regdma_completion_event)
    read_pcnt_results()                 // or read GPIO result bus
    classify_layer_output()
    prepare_next_layer_input()          // remap Y descriptors
    i++
  wake_main_cpu()

MEMORY MAP:
  0x00000 - 0x03FFF: Layer 0 weight matrix (16KB)
  0x04000 - 0x07FFF: Layer 1 weight matrix (16KB)
  ...
  0x60000 - 0x603FF: Input vector Y (1KB)
  0x60400 - 0x607FF: Hidden vector (1KB, written by LP core from results)
  0x60800 - 0x60BFF: Descriptor tables (3KB)
  0x60C00 - 0x60FFF: REGDMA program tables (1KB)
  0x61000 - 0x613FF: ETM topology snapshots (1KB)
  0x61400 - 0x7FFFF: Free (~120KB)

  Total for 6-layer, 256×256 ternary network: ~100KB
  Remaining SRAM: ~400KB
```

**Silicon verification for Level 3:**
1. REGDMA basic operation: copy a word from SRAM to GDMA register
2. REGDMA descriptor chain: advance through a table of GDMA pointers
3. REGDMA → ETM → GDMA auto-loop (neurons sequence without CPU)
4. LP core wakeup on REGDMA completion
5. LP core reads PCNT, reconfigures REGDMA, kicks next layer
6. Full 2-layer evaluation with LP core sequencing, main CPU asleep

**Milestone 7 criteria: Multi-neuron layer evaluation autonomously sequenced by REGDMA, with LP core managing inter-layer transitions. Main CPU asleep during computation.**

---

## Key Decisions

1. **2-unit TMUL stays for Level 1.** Single-unit requires 4 gating combinations but PCNT only has 2 channels. The 2-unit approach is proven and correct. Freed PCNT units serve diagnostics and parallel evaluation.

2. **RMT for Y geometry, not MCPWM.** RMT is DMA-driven, directly compatible with descriptor chains. MCPWM requires REGDMA per-trit register updates — too much bus traffic.

3. **Clock rate stays at 1MHz initially.** Increase to 10-20MHz after verifying PCNT edge counting at higher rates. This is a throughput optimization, not an architectural change.

4. **REGDMA is the program counter.** It advances through descriptor tables, enabling autonomous multi-neuron evaluation without CPU. The three-level indirection (REGDMA → GDMA descriptor pointer → GDMA descriptor → pattern data) is the instruction fetch pipeline.

5. **LP core owns inter-layer logic.** It's programmable, low-power, and independent. It handles the conditional/branching logic that REGDMA can't. Main CPU sleeps during inference.

6. **ETM topology = program.** Different ETM configurations compute different things. Level 3 uses REGDMA to swap ETM configs between evaluation phases, making the fabric reconfigurable.

7. **Build order follows silicon verification.** Each level has exactly one blocking unknown. Prove it, then build the next level. No speculative stacking.

## Build Order

```
MILESTONE 5: Single-Unit Pipeline with 256-Trit Dot Product
  ├── Redesign patterns: 1 dibit = 1 trit
  ├── Descriptor chain test: 10 chained descriptors, verify accumulation
  ├── Auto-stop via ETM (GDMA EOF → Timer stop, no watchdog)
  └── Silicon verification: dot product of known 256-trit vectors

MILESTONE 6: Dual-Channel Geometry Engine
  ├── RMT TX configuration for Y geometry on GPIO 6,7
  ├── ETM simultaneous trigger: PARLIO + RMT
  ├── Measure phase offset, compensate in Y pattern alignment
  ├── PCNT threshold → GPIO result signaling
  └── Silicon verification: both geometries DMA-driven, correct TMUL

MILESTONE 7: Self-Sequencing Fabric
  ├── REGDMA basic: copy SRAM word to GDMA register via ETM trigger
  ├── REGDMA chain: auto-advance through neuron descriptor table
  ├── Autonomous multi-neuron loop (DMA→REGDMA→DMA→...)
  ├── LP core integration: wakeup, read, reconfigure, sleep
  └── Silicon verification: multi-layer inference, main CPU asleep

MILESTONE 8: Performance (optional, after 7 works)
  ├── Increase PARLIO clock to 10MHz
  ├── Verify PCNT at 10MHz edge rate
  ├── RMT symbol compression for sparse Y vectors
  ├── Parallel dual-neuron evaluation (4 PCNT units)
  └── Benchmark: layers/second, trits/second, power consumption
```

## ETM Channel Budget

```
Level 1:   5 channels  (kick, clear, watchdog, auto-stop, prime)
Level 2:  20 channels  (dual-neuron eval + result routing)
Level 3:  15 channels  (REGDMA loop + LP core signaling + layer control)
Total:    40 channels
Remaining: 10 channels (error handling, diagnostics, sensor input, expansion)
```

## Success Criteria

- [ ] Milestone 5: 256-trit dot product correct on silicon
- [ ] Milestone 6: Dual DMA geometry intersection correct on silicon
- [ ] Milestone 7: Multi-neuron layer, main CPU asleep during eval
- [ ] Milestone 8: ≥100K trit-MAC/s sustained throughput

## What This Is

A programmable geometry intersection engine built from the peripheral fabric of a $1 microcontroller. Shapes flow through DMA channels, intersect at PCNT manifolds, and produce new shapes routed through GPIO. The ETM topology defines the computation. The SRAM holds the geometry space. The ground state is silence. Sparsity is native. The hardware computes by measuring overlap.

It's not a neural network accelerator. It's not a DSP. It's not an FPGA.
It's a geometry computer that happens to be able to do neural network inference, among other things, because dot products are geometry.
