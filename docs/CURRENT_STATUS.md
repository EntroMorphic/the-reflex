# The Reflex: Current Status

**Last Updated:** March 23, 2026 — Phase 5 designed, full LMM assessment complete, temporal context reframe articulated.

---

## Executive Summary

The Reflex is a three-layer ternary neural computing system built from commodity peripheral hardware. It classifies wireless signals at 100% accuracy using peripheral-hardware ternary dot products at 430.8 Hz, drawing ~30 µA in autonomous mode. Beneath that classifier, a sub-conscious layer on the LP core accumulates a temporal model of what the system has been perceiving — pattern-specific internal representations that develop from VDB episodic memory retrieval over 90 seconds of live operation.

**The reframe (March 23, 2026):** The Reflex is not building a better classifier. It is building a temporal context layer beneath a perfect classifier. Every downstream contribution — kinetic attention, multi-agent coordination, Hebbian learning — earns its meaning from the quality and independence of that temporal model.

**Current state: 13/13 PASS.** Phase 5 (kinetic attention) fully designed and ready to implement.

**The complete loop:** perceive → classify → remember → retrieve → modulate (potential modulation verified). Phase 5 makes it kinetic: the temporal model actively biases what the perceptual layer fires on next.

All ternary. No floating point. No multiplication. No training.

**Key sessions:**
- March 19: Silicon Interlock identified (USB-JTAG clamping GPIOs 4–7, forced 20MHz PARLIO). LP Core NSW + CfC pipeline verified 100%.
- March 21: Second-call hang fixed (GDMA reset in stop_freerun, interrupt ordering).
- March 22 (morning): TEST 2+ zero-loop stall diagnosed and fixed (pulsing `parl_tx_rst_en`, PCR bit 19). Board B PEER_MAC corrected (`c4:d4` → `c8:24`). **11/11 PASS**.
- March 22 (night): TEST 12 (memory-modulated attention), TEST 13 (CMD 4 distillation). **13/13 PASS**. VDB episodic memory causally necessary.
- March 23: Full LMM project assessment. Temporal context reframe. Three strata of contribution. Two operating modes defined. L-Cache opcode spec written (12 opcodes, 1:1 with firmware, ~350ns/loop). THE_PRIOR_AS_VOICE perspective paper written. Phase 5 firmware spec finalized.

**Key documentation:**
- `docs/SESSION_MAR22_2026.md`: Full March 22 session — 13/13 PASS, complete TEST 12/13 results.
- `docs/SESSION_MAR23_2026.md`: Full March 23 session — LMM assessment, three strata, blockers, action table.
- `docs/MEMORY_MODULATED_ATTENTION.md`: Paper-quality writeup of TEST 12 — experimental design, silicon results, analysis.
- `docs/KINETIC_ATTENTION.md`: Phase 5 design spec — agreement-weighted gate bias, TEST 14 three conditions.
- `docs/THE_PRIOR_AS_VOICE.md`: Perspective paper — technical, engineering, ontological, and personal dimensions.
- `docs/PRIOR_SIGNAL_SEPARATION.md`: Theoretical note — structural hallucination resistance, five-component architecture.
- `docs/LCACHE_REFLEX_OPCODES.md`: L-Cache opcode spec — 12 AVX2 opcodes, 1:1 with firmware, ~2.8 MHz.
- `docs/HARDWARE_TOPOLOGY.md`: Nucleo ↔ C6 wiring spec for APU-expanded mode (SPI2 at 40 MHz).

---

## The Three-Layer Hierarchy (Verified on Silicon)

| Layer | Hardware | Rate | Power | What It Does |
|-------|----------|------|-------|-------------|
| GIE | GDMA+PARLIO+PCNT peripherals | 430.8 Hz | ~0 CPU | 64-neuron CfC inference via peripheral routing |
| LP core | 16MHz RISC-V (hand-written ASM) | 100 Hz | ~30uA | Geometric CfC + NSW vector database + pipeline |
| HP core | Full 160MHz CPU | On demand | ~15mA | Init + monitoring only |

**No floating point. No multiplication anywhere.** All ternary operations use AND, popcount (byte LUT), branch, add, sub, negate.

---

## Complete Milestone History

Every milestone verified on silicon (ESP32-C6FH4 QFN32 rev v0.2), exact dot-for-dot match against CPU reference.

### GIE Milestones (Feb 5-7)

| Milestone | Tests | What It Proved | Commit |
|-----------|-------|----------------|--------|
| M1: Sub-CPU ALU | 59/59 | PCNT+PARLIO boolean gates | `f41d5ea` |
| M2: Autonomous Fabric | 5/5 | ETM crossbar peripheral loop | `5b2f62d` |
| M3: Autonomous ALU | 9/9 | GDMA descriptor chain sequencing | `59d0bba` |
| M4: Ternary TMUL | 9/9 | 2-bit PARLIO + dual PCNT = ternary multiply | `66469ce` |
| M5: 256-Trit Dot Product | 10/10 | Multi-buffer DMA accumulation | `d45067b` |
| M6: Multi-Neuron Layer | 6/6 | 32-neuron layer, 108.8K trit-MACs/s | `d45067b` |
| M7: Ternary CfC | 6/6 | Fully-ternary liquid network, 3 blend modes | `b136ae9` |

### Free-Running + Scale Milestones (Feb 7-8)

| Milestone | Tests | What It Proved | Commit |
|-----------|-------|----------------|--------|
| M8 v1: Giant DMA Chain | 4/4 | 64 neurons, cumulative sum via DMA chain | `28ff786` |
| M8 v2: Per-Neuron ISR | 3/3 | 64/64 exact, repeated runs stable | `1980c9a` |
| M9: 10MHz PARLIO | 6/6 | PCNT keeps up at 10MHz, zero errors | `28ff786` |
| M10: Differentiation | 4/5 | Stem cell hypothesis tested | `06e81e8` |
| CfC on GIE | 6/6 | Autonomous ternary CfC, 6/6 tests | `2272eb4` |
| PCNT Drain Fix | verified | Clock domain crossing race eliminated | `ae3368f` |
| Free-Run | 3/3 | Free-running sub-CPU neural network, 64/64 exact | `f8860d3` |

### LP Core Milestones (Feb 8)

| Milestone | Tests | What It Proved | Commit |
|-----------|-------|----------------|--------|
| LP Core C | 4/4 | LP core geometric processor verified | `9b80ff0` |
| LP Core ASM | 4/4 | Hand-assembled RISC-V, exact match | `dd87898` |

### VDB Milestones (Feb 8-9)

| Milestone | Tests | What It Proved | Commit |
|-----------|-------|----------------|--------|
| VDB Initial | 5/5 | Ternary vector database on LP core | `d0ea002` |
| VDB Top-K | 5/5 | Top-K=4 sorted search | `6ea0497` |
| VDB M2: Scale | 5/5 | 64-node scale + latency benchmark | `2c6dd17` |
| VDB M3: HP API | 5/5 | vdb_insert/search/clear/count API | `fa54192` |
| VDB M4: NSW Graph | 6/6 | M=7, ef=32, dual entry, recall@1=95%, recall@4=90% | `7db919f` |
| **VDB M5: Pipeline** | **4/4** | **CfC→VDB in one LP wake: perceive+think+remember** | **`06d5535`** |
| Reflex Channel | 7/7 | ISR→HP coordination via reflex_channel_t, 18us avg latency | `e9e67f1` |
| **VDB→CfC Feedback** | **8/8** | **CMD 5: memory blend into lp_hidden, HOLD damping, 50 unique states** | **`dc57d60`** |

### TriX Classification Milestones (Feb 9-10)

| Milestone | Tests | What It Proved | Commit |
|-----------|-------|----------------|--------|
| TriX Signature Routing | 11/11 | Ternary signatures classify ESP-NOW patterns, 78% | `6b61da3` |
| Signatures as W_f Weights | 11/11 | Per-packet voting + sig-as-weights, 90% | `ce0f788` |
| Online Maintenance | 11/11 | Signature re-signing + novelty detection, 100% input-TriX | `ef9dc69` |
| 7-Voxel TriX Cube | 11/11 | Core + 6 temporal faces, core 100% | `d4f7ef0` |
| XOR Masks | 11/11 | Faces as intervention sensors, XOR displacement data | `b117955` |
| **ISR TriX Classification** | **11/11** | **DMA race solved, ISR 87% at 430 Hz** | **`24ba035`** |
| **ISR 100% + Timeout Guard** | **11/11** | **Timeout guard + extended spin, ISR 100%, Core 100%** | **`fd338f5`** |
| **TriX Classification Channel** | **11/11** | **Packed dots via reflex_signal, channel-based consumer** | **`b79f09b`** |
| **CfC Blend Disabled (Phase 3)** | **11/11** | **gate_threshold=INT32_MAX, 0% firing, TriX-only classification** | **`c6fd284`** |
| **Hidden Re-encode Skipped (Phase 4)** | **11/11** | **Step 5 gated by threshold, saves ~20us/loop when blend off** | **`8a33369`** |
| **Phase 4 Full Verification** | **11/11** | **PARLIO TX fix + Board B live input, 100% vs 84% baseline** | **`07b5b66`** |
| **Memory-Modulated Attention** | **12/12** | **TEST 12: LP hidden diverges by pattern from VDB episodes; P1 vs P3 Hamming 5** | **`38a0811`** |
| **VDB Causal Necessity** | **13/13** | **TEST 13: CMD 4 distillation — P1=P2 (Hamming 0) in 2/3 CMD 4 runs; CMD 5 separates every time** | **`12aa970`** |

---

## What's Working

### Core Technology

| Component | Status | Performance |
|-----------|--------|-------------|
| reflex.h primitive | Production | 12ns pure (C6), 309ns (Thor) |
| 10kHz control loop | Verified | 926ns P99 (Thor) |
| GIE free-running | Verified | 430.8 Hz, 64 neurons, ~0 CPU |
| LP core CfC | Verified | 100 Hz, 16 neurons, ~30uA |
| LP core VDB | Verified | 64 nodes, NSW graph, recall@1=95% |
| CfC→VDB pipeline | Verified | Perceive+think+remember in one 10ms wake |
| Reflex channel | Verified | ISR→HP, 18us avg, fence-ordered |
| VDB→CfC feedback | Verified | CMD 5, HOLD damping, 50 unique states in 50 steps |
| ESP-NOW live input | Verified | 4-pattern wireless input drives GIE |
| **TriX classification** | **Verified** | **32/32 = 100% (Core + ISR), zero-shot from signatures** |
| **TriX classification channel** | **Verified** | **Packed dots via reflex_signal, 705 Hz ISR classification rate** |
| Online maintenance | Verified | Signature re-sign every 16 pkts, novelty gate at 60 |
| 7-voxel TriX Cube | Verified | Core + 6 temporal faces, XOR mask displacement data |
| **Memory-modulated LP priors** | **Verified** | **13/13 PASS: LP hidden diverges by pattern (P1 vs P3: Hamming 5/16), 97% feedback applied** |
| **VDB causal necessity** | **Verified** | **CMD 4 distillation: P1=P2 in 2/3 runs without blend; CMD 5 separates P1/P2 every time** |

### The GIE Signal Path (Current Architecture)

```
┌─────────────────────────────────────────────────────────────────┐
│              FREE-RUNNING GEOMETRY INTERSECTION ENGINE            │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│   Circular DMA chain: [dummy×5][neuron×64] loops forever         │
│                                                                  │
│   GDMA → PARLIO TX (2-bit, 10MHz) → GPIO 4,5 (loopback)        │
│                              │                                   │
│                       PCNT Unit 0 (agree)                        │
│                       PCNT Unit 1 (disagree)                     │
│                              │                                   │
│   ISR (LEVEL3, on each neuron EOF):                              │
│     200-loop drain (clock domain crossing)                       │
│     dot = agree - disagree                                       │
│     Re-encode next neuron's W×X products                         │
│     CfC blend: DISABLED (Phase 3, all neurons HOLD)              │
│                              │                                   │
│   430.8 Hz continuous, CPU-free after init                       │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
        │ cfc.hidden[32] via reflex_channel_t (18us, fence-ordered)
        ▼
┌─────────────────────────────────────────────────────────────────┐
│           LP CORE GEOMETRIC PROCESSOR (16MHz, ~30uA)             │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│   Hand-written RISC-V assembly. Every instruction specified.     │
│   Operations: AND, popcount(LUT), add, sub, negate, branch      │
│                                                                  │
│   CMD 1: CfC step (16 neurons, 48-trit input, 32 INTERSECTs)   │
│   CMD 2: VDB search (NSW graph, ef=32, or brute-force)          │
│   CMD 3: VDB insert (brute-force candidates + top-M + edges)    │
│   CMD 4: CfC + VDB pipeline (one wake: think + remember)        │
│   CMD 5: CfC + VDB + Feedback (memory → lp_hidden blend)       │
│                                                                  │
│   100 Hz (10ms LP timer wake cycle)                              │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
        │ lp_hidden[16], vdb_results[4]
        ▼
┌─────────────────────────────────────────────────────────────────┐
│              HP CORE (160MHz, ~15mA) — CONSCIOUSNESS             │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│   Initialization, weight loading, monitoring                     │
│   Awake only when needed                                         │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### LP Core Memory Budget (16KB LP SRAM)

| Section | Size | Notes |
|---------|------|-------|
| Vector table | 128 B | Fixed |
| Code (.text) | ~7,600 B | CfC + VDB search + VDB insert + pipeline + feedback |
| Popcount LUT (.rodata) | 288 B | 256-byte LUT + alignment |
| CfC state (.bss) | ~968 B | Weights, hidden, dots, sync vars |
| VDB nodes (.bss) | 2,048 B | 64 x 32 bytes (M=7 neighbors) |
| VDB metadata (.bss) | ~80 B | Query, results, counters |
| Feedback state (.bss) | ~24 B | Blend scratch, loop counters |
| shared_mem | 16 B | Top of SRAM |
| **Free for stack** | **~4,400 B** | Peak usage: 608B (VDB search), measured 4,356 free |

### NSW Graph Performance (64 Nodes, 48 Trits)

| Metric | Value |
|--------|-------|
| Self-match | 64/64 (100%) |
| Recall@1 | 95% (vs brute-force) |
| Recall@4 | 90% (vs brute-force) |
| Connectivity | 64/64 reachable (BFS from node 0) |
| Search latency | ~10-15ms round-trip (includes LP wake jitter) |
| Sub-linear | Yes (visited 60/64 — graph prunes some) |

---

## Key Technical Decisions

### 1. Ternary Constraint is Generative
{-1, 0, +1} maps to 2-bit GPIO encoding → PARLIO loopback → PCNT edge/level gating. The constraint created the architecture. This isn't reduced precision — it's native hardware precision.

### 2. No Multiplication Anywhere
The entire stack uses AND, popcount, add, sub, negate, branch, shift. Ternary multiplication is sign comparison. The RV32IMAC M extension is never exercised. Every operation is exact.

### 3. Three CfC Blend Modes
UPDATE (f=+1), HOLD (f=0), INVERT (f=-1). Binary CfC has only two. The third creates oscillation, convergence resistance, and path-dependent memory — dynamical primitives, not neural network operations.

### 4. NSW Graph (Not HNSW)
Single-layer NSW with M=7, ef=32, dual entry points. No multi-layer hierarchy needed at N=64. Plain top-M selection beats diversity heuristics for small N with low-dimensional ternary vectors. Reverse edge eviction is critical for connectivity.

### 5. Sequential Stack Frames for Pipeline
CfC (96B frame) → bridge copies 6 words to VDB query BSS → deallocate → VDB (608B frame). Peak stack is 608B, not 704B. Clean separation — neither function knows the other exists.

---

## Hardware Platform

| Detail | Value |
|--------|-------|
| Chip | ESP32-C6FH4 (QFN32) rev v0.2 |
| CPU | RISC-V 160MHz (HP core) |
| LP core | RISC-V 16MHz (~30uA) |
| Flash | 4MB embedded |
| LP SRAM | 16KB (RESERVE_MEM=16320) |
| ESP-IDF | v5.4 |
| Board | Single chip, /dev/esp32c6 |

### GPIO Pin Map

| GPIO | Function | Direction |
|------|----------|-----------|
| 4 | X_pos (PARLIO bit 0) | Output (loopback to PCNT) |
| 5 | X_neg (PARLIO bit 1) | Output (loopback to PCNT) |
| 6 | Y_pos (static level) | Output (CPU-driven) |
| 7 | Y_neg (static level) | Output (CPU-driven) |

---

## Errata (Discovered During Development)

| Source | Errata | Resolution |
|--------|--------|------------|
| M2 | ETM base address wrong | Correct: 0x60013000 |
| M3 | IDF disables ETM bus clock | Re-enable via PCR_SOC_ETM_CONF |
| M3 | GDMA+ETM_EN won't follow chains | Use normal GDMA, gate with PARLIO TX_START |
| M3 | PARLIO nibble boundary glitches | Fixed in M4: switched to 2-bit PARLIO |
| M5 | GDMA LINK_START leaks to FIFO | Triple PCNT clear with delays |
| M6 | ESP32-C6 RMT has no DMA | Keep Y static, pre-multiply on CPU |
| M7 | Large structs overflow stack | Declare as static (BSS) |
| Free-run | PCNT clock domain drain | 200 volatile loops (~5us) in ISR before read |
| Free-run | LP SRAM bus contention | Never write LP SRAM from ISR; main loop only |
| Free-run | GPIO output disable after PCNT init | Re-enable with gpio_set_direction() |
| Phase 4 | PARLIO TX state machine corrupts on mid-tx stop | Pulse parl_tx_rst_en (PCR bit 19) in stop_freerun() |
| Phase 4 | GDMA ISR race on second start_freerun() | GDMA reset in stop_freerun() + enable interrupts after setup |
| Phase 4 | out_eof_mode=1 unusable (no PARLIO handshake) | Use mode=0; account for ~23 pre-fill phantom EOFs |
| Phase 4 | PARLIO INT_RAW sticky across tx_start cycles | Behavioral no-op (INT_ENA=0), but misleads diagnostics |
| Phase 4 | GDMA owner bits not cleared on ESP32-C6 | No maintenance needed; confirmed by 432 Hz continuous |
| Multi-board | espnow_sender PEER_MAC is a point-in-time constant | Record Board A MAC in FLASH_GUIDE; verify after each board swap |

---

## Repository Structure (Current)

```
the-reflex/
├── reflex-os/                    # ESP32-C6: GIE + LP Core + VDB
│   ├── main/
│   │   ├── ulp/main.S            # LP core: hand-written RISC-V assembly
│   │   │                         #   CfC + VDB search + insert + pipeline + feedback (cmd 1-5)
│   │   ├── geometry_cfc_freerun.c  # Current entry point (8 tests)
│   │   ├── reflex_vdb.c          # HP-side VDB API implementation
│   │   ├── geometry_cfc.c        # M7: Original CfC (single-step)
│   │   ├── geometry_layer.c      # M6: Multi-neuron layer
│   │   ├── geometry_dot.c        # M5: 256-trit dot product
│   │   ├── ternary_alu.c         # M4: Ternary TMUL
│   │   ├── autonomous_alu.c      # M3: Autonomous ALU
│   │   ├── alu_fabric.c          # M1: Sub-CPU ALU
│   │   └── raid_etm_fabric.c     # M2: Autonomous fabric
│   ├── include/
│   │   ├── reflex_vdb.h          # VDB API (insert/search/clear/pipeline)
│   │   ├── reflex.h              # Core primitive (50 lines)
│   │   └── ...
│   └── docs/
│       ├── GIE_ARCHITECTURE.md   # GIE architecture
│       ├── HARDWARE_ERRATA.md    # 20+ errata
│       ├── REGISTER_REFERENCE.md # Bare-metal register addresses
│       └── FLASH_GUIDE.md        # Build/flash procedure
│
├── reflex-robotics/              # 10kHz control loop (Jetson Thor)
├── reflex_ros_bridge/            # ROS2 integration
├── reflex-deploy/                # CLI deployment tool
├── delta-observer/               # Neural network observation research
├── docs/                         # Project documentation
├── notes/                        # Design notes, LMM explorations
├── journal/                      # Lincoln Manifold Method journal
│   ├── the_reflex_raw.md         # Phase 1: RAW
│   ├── the_reflex_nodes.md       # Phase 2: NODES
│   ├── the_reflex_reflect.md     # Phase 3: REFLECT
│   └── the_reflex_synth.md       # Phase 4: SYNTHESIZE
└── the-reflex-tvdb.md            # VDB PRD (5 milestones, all complete)
```

---

## What's Next

**13/13 PASS (March 22, 2026).** Phase 5 kinetic attention fully designed. Firmware refactor needed before Phase 5 code lands.

### Immediate Priority: TEST 14 (Kinetic Attention)

| Condition | What | Status |
|-----------|------|--------|
| 14A | Baseline: gate_bias = 0 for all groups | Pending |
| 14B | Per-group bias from LP prior (agreement-weighted) | Pending |
| 14C | Transition test: Board B switches patterns mid-run | Pending — **mandatory CLS prediction test** |

**Pass criteria:**
- Classification accuracy remains 100%
- LP Hamming matrix under 14B ≥ TEST 12 baseline on ≥ 4 of 6 pairs
- LP prior updates within 15 confirmations of pattern switch (14C)
- GIE hidden state does not saturate (energy < 60/64 on average)

### Blocking Before Any Paper Submission

1. **UART falsification** — Re-route console to GPIO 16/17, power from battery/dumb USB, run full 13-test suite without JTAG. The "peripheral-autonomous" claim requires this data. Currently: JTAG attached for all runs.

2. **Firmware refactor** — Separate core layer (GIE, VDB, LP, CMD dispatch) from test layer (condition flags, parameters, logging) before Phase 5 code lands. Reviewers must find the difference between TEST 14A and 14B in under 10 lines.

### Strategic Phase Order

| Phase | What | Status |
|-------|------|--------|
| **Phase 1** | TriX dot extraction in ISR | **DONE** |
| **Phase 2** | TriX classification channel (reflex_signal, 430 Hz) | **DONE — `b79f09b`** |
| **Phase 3** | Disable CfC blend (gate_threshold = INT32_MAX) | **DONE — `c6fd284`** |
| **Phase 4** | Skip hidden re-encode when blend disabled | **DONE — `8a33369`** |
| **TEST 12** | Memory-modulated attention confirmed | **DONE — `38a0811`** |
| **TEST 13** | CMD 4 distillation — VDB causally necessary | **DONE — `12aa970`** |
| **Refactor** | Core vs. test layer separation | Pending (before Phase 5) |
| **Phase 5 / TEST 14** | Kinetic attention: LP prior → GIE gate bias | Pending |
| **TEST 14C** | Transition experiment — primary CLS prediction test | Pending |
| **Paper 1** | TEST 12/13/14 hardware paper (Stratum 1) | Pending TEST 14 data |
| **Paper 2** | CLS architecture paper (Stratum 2) | Pending TEST 14C data |
| **Paper 3** | Prior-signal separation note (Stratum 3) | Near complete — one session |
| **Pillar 1** | Dynamic scaffolding (VDB sliding window) | After Phase 5 |
| **Pillar 2** | SAMA (substrate-aware multi-agent) | After Phase 5 |
| **Pillar 3** | Hebbian GIE weight updates | After all fixed-weight papers written |

### Three Strata of Contribution

**Stratum 1 — Engineering** (embedded systems venues):
TEST 12/13/14 papers. GDMA→PARLIO→PCNT as ternary neural substrate. NSW graph in LP SRAM at ~30 µA. Agreement-weighted gate bias. All claims silicon-verified, distillation-controlled.

**Stratum 2 — Architecture** (computational neuroscience):
CLS architecture paper. Fixed-weight CLS analog — VDB as permanent hippocampal layer, LP CfC as fixed neocortical extractor. Transition experiment (TEST 14C) is the primary empirical test of the CLS prediction.

**Stratum 3 — Principle** (AI/ML venues):
Prior-signal separation note. Five-component architecture for structural hallucination resistance. The Reflex is the silicon-verified instantiation. Near-complete draft: `docs/PRIOR_SIGNAL_SEPARATION.md`.

### Two Operating Modes

**Autonomous Mode (~30 µA):** C6 only. GIE + VDB + LP CfC + kinetic attention. No Nucleo. This is the mode for all TEST 12/13/14 papers. The power claim applies only to this mode.

**APU-Expanded Mode (~10–50 mA):** C6 + Nucleo APU. SPI at 40 MHz (VDB acceleration), QSPI at 160 Mbps (MTFP21 inference). For MTFP21/L-Cache papers and future SAMA work. Connection spec: `docs/HARDWARE_TOPOLOGY.md`.

---

## Platforms

| Platform | Status | Use Case |
|----------|--------|----------|
| Jetson AGX Thor | Verified | Production robotics (926ns P99) |
| ESP32-C6 (x3) | Verified | GIE + LP core + VDB |
| Raspberry Pi 4 | Working | Development, OBSBOT control |

---

*The hardware is already doing the work. We're just using it.*
