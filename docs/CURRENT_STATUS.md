# The Reflex: Current Status

**Last Updated:** April 11, 2026 — Two compounding bugs fixed (trix_enabled `f97ac1c`, sender enrollment starvation `63877f7`). All prior multi-seed TEST 14C data invalidated and re-collected (`data/apr9_2026/`). Label-free classification experiment (R3a): 100% accuracy achieved after P2 payload redesign (`c7ef286`) — pattern_id trits masked from signatures, classifier uses only payload and timing features. Bias release mechanism correctly described as geometric ×0.9/step (not "within 4 steps"). Test harness split into per-area files (`5657e92`). 66 inactive C files archived. Red-team self-audit produced 12 items, 3 resolved (R3, R3a, R8 partial).

---

## Executive Summary

The Reflex is a three-layer ternary neural computing system built from commodity peripheral hardware. It classifies wireless signals at 100% accuracy using peripheral-hardware ternary dot products at 430.8 Hz, drawing ~30 µA in autonomous mode. Beneath that classifier, a sub-conscious layer on the LP core accumulates a temporal model of what the system has been perceiving — pattern-specific internal representations that develop from VDB episodic memory retrieval over 90 seconds of live operation.

**The reframe (March 23, 2026):** The Reflex is not building a better classifier. It is building a temporal context layer beneath a perfect classifier. Every downstream contribution — kinetic attention, multi-agent coordination, Hebbian learning — earns its meaning from the quality and independence of that temporal model.

**Current state: 14/14 PASS (label-free).** Phase 5 kinetic attention verified on silicon at both 100% and 71% label-free accuracy — the mechanism survives 29% classifier error. Classification is 100% label-free on 4 well-separated patterns with distinct payloads (commit `c7ef286`). Prior P1-P2 confusion (71%) was a test-design artifact: old P2 payload shared 48/64 payload trits with P1. MTFP21 gap history encoding provides timing discrimination. LP dot magnitude diagnostic identifies the `sign()` quantization bottleneck; MTFP dot encoding (5 trits/neuron) spec ready for implementation.

**The complete loop:** perceive → classify → remember → retrieve → modulate (potential modulation verified). Phase 5 makes it kinetic: the temporal model actively biases what the perceptual layer fires on next. Bias decays geometrically (×0.9/step, half-life ~6.6 steps); `pred` flips at step +1 post-switch; new prior forms by step +15.

All ternary. No floating point. No multiplication. No training.

**Key sessions:**
- March 19: Silicon Interlock identified (USB-JTAG clamping GPIOs 4–7, forced 20MHz PARLIO). LP Core NSW + CfC pipeline verified 100%.
- March 21: Second-call hang fixed (GDMA reset in stop_freerun, interrupt ordering).
- March 22 (morning): TEST 2+ zero-loop stall diagnosed and fixed (pulsing `parl_tx_rst_en`, PCR bit 19). Board B PEER_MAC corrected (`c4:d4` → `c8:24`). **11/11 PASS**.
- March 22 (night): TEST 12 (memory-modulated attention), TEST 13 (CMD 4 distillation). **13/13 PASS**. VDB episodic memory causally necessary.
- March 23 (morning): Full LMM project assessment. Temporal context reframe. Three strata of contribution. Two operating modes defined. L-Cache opcode spec. THE_PRIOR_AS_VOICE perspective paper. Phase 5 firmware spec finalized.
- March 23 (evening): TEST 14C AVX2 simulation (3 conditions × 1000 trials, three-claim structure confirmed). Red-team + LMM cycle on 3 open risks — root cause identified as unknown LP dominant path. LP characterization firmware written (`625b00d`).
- April 6: Full repository audit. Monolith split (`c815869`): `gie_engine.c`/`.h` extracted from `geometry_cfc_freerun.c`. ESP-NOW refactored to `.c` + header. Dead code archived. **13/13 PASS** post-refactor. Phase 5 kinetic attention implemented (`429ce38`): agreement-weighted gate bias, three conditions (14A/14C/14C-iso), confound controls. **14/14 PASS.** MTFP21 gap history encoding (`c814e51`): 80% → 96% classification accuracy. Sequence features masked from signatures (`5735119`). Paper draft with confusion matrices (`feb709b`). TriX ISR vs CPU agreement at 100% (`0b09f69`).
- April 7: LP dot magnitude diagnostic (`7391876`): P1-P2 separation IS in the magnitudes. `sign()` quantization is the bottleneck. MTFP dot encoding spec written (`81175c6`): 5 trits/neuron, LP hidden expands from 16 to 80 trits. LMM cycle on Pillar 3 concerns (`b77bf01`): dimensionality must be proven before learning.
- April 8: Red-team of all 3 papers (10 issues). "Enrollment, not training" terminology. Multi-seed TEST 14C on silicon (3 seeds × 3 conditions). Discovered bug: agreement hardcoded to P1 accumulator. Three compounding fixes: (1) TriX dispatch — LP feedback from 100% ISR, not 80% CPU classifier; (2) per-pattern accumulator — bug fix; (3) ternary disagree-count agreement — immediate bias release when 4+ trits disagree. Transition headwind: 18/0/7 → 0/22/2 across seeds. LMM cycle on TriX dispatch (`journal/trix_dispatch_{raw,nodes,reflect,synth}.md`). All three papers updated. Session: `docs/SESSION_APR08_2026.md`. **NOTE:** All multi-seed data from this session was later invalidated (see April 9-11 entry).
- April 9-11: Full audit + remediation session. (1) Test harness split into per-area files (`5657e92`). (2) Constants deduplicated, 66 inactive C files archived, vestigial extern hoisted. (3) **Second compounding bug found:** sender enrollment starvation in transition mode — Board A only saw P1 during enrollment, making sig[0]/sig[2]/sig[3] zero. Fixed (`63877f7`). All prior multi-seed data invalidated; apr8 data deprecated (`data/apr8_2026/DEPRECATED.md`). (4) Multi-seed re-run with corrected sender: A=15/15, B=8/15, C=15/15 (`data/apr9_2026/SUMMARY.md`). (5) **Bias release mechanism** correctly traced as geometric ×0.9/step (not "within 4 steps"). "4" was a disagree-count trit threshold, not a step count. Hard disagree-zero path not exercised on clean seeds. (6) **Label-free accuracy** experiment (R3a): pattern_id trits [16..23] were undisclosed "primary discriminator" in signatures. With label masked: 71% accuracy (P2 10%, all others 100%). Root cause: P2 payload shared 48/64 trits with P1. Fix: distinct P2 payload (`c7ef286`). Result: **100% label-free, 14/14 PASS**. RSSI noise hypothesis disproved (masking RSSI gave 68%). (7) Self-adversarial red-team produced 12 items (`docs/REMEDIATION_PLAN_APR09_REDTEAM.md`, local-only). (8) **Pillar 3 implemented:** LP Hebbian weight learning (`lp_hebbian_step()`, commit `32fb061`). Ablation-controlled TEST 15 confirmed +2.5 Hamming over control (`4343447`). **BUT H2 found label leakage** (`a0d3a36`): the +2.5 was from pattern_id leaked through GIE hidden state. With genuinely label-free input (`MASK_PATTERN_ID_INPUT=1`), Hebbian contribution is -1.7 (harmful). VDB mismatch error signal is label-dependent. (9) **Key secondary finding:** removing pattern_id from GIE input improved VDB-only LP divergence from 0.7 to 3.3/16. The label was drowning out discriminative features. Recommended operating mode: `MASK_PATTERN_ID_INPUT=1`. (10) Next: TriX-output-based Hebbian learning (use structurally guaranteed classifier output instead of raw VDB mismatch as the error signal). Remaining: paper rewrites, multi-seed re-run, UART verification.

**Key documentation:**
- `docs/SESSION_MAR22_2026.md`: Full March 22 session — 13/13 PASS, complete TEST 12/13 results.
- `docs/SESSION_MAR23_2026.md`: Full March 23 session — LMM assessment, three strata, blockers, action table.
- `docs/SESSION_APR06_07_2026.md`: April 6-7 session — audit remediation, Phase 5, MTFP21, LP diagnostics.
- `docs/SESSION_APR08_2026.md`: April 8 session — red-team, multi-seed 14C, TriX dispatch, ternary agreement, three bug fixes.
- `docs/MEMORY_MODULATED_ATTENTION.md`: Paper-quality writeup of TEST 12 — experimental design, silicon results, analysis.
- `docs/KINETIC_ATTENTION.md`: Phase 5 design spec — agreement-weighted gate bias, TEST 14 three conditions.
- `docs/PAPER_KINETIC_ATTENTION.md`: Paper draft — kinetic attention results with confusion matrices.
- `docs/THE_PRIOR_AS_VOICE.md`: Perspective paper — technical, engineering, ontological, and personal dimensions.
- `docs/PRIOR_SIGNAL_SEPARATION.md`: Theoretical note — structural hallucination resistance, five-component architecture.
- `data/apr9_2026/SUMMARY.md`: Authoritative multi-seed TEST 14C digest — TriX@15, alignment traces, bias release trace, Seed B analysis, metric caveats.
- `data/apr8_2026/DEPRECATED.md`: Deprecation flag — all TriX-accuracy claims from apr8 data are invalid (two compounding bugs).
- `DO_THIS_NEXT.md`: Forward todo list — paper rewrites, verdict logic fix, UART verification, mechanism corrections.
- `docs/LCACHE_REFLEX_OPCODES.md`: L-Cache opcode spec — 12 AVX2 opcodes, 1:1 with firmware, ~2.8 MHz.
- `docs/HARDWARE_TOPOLOGY.md`: Nucleo ↔ C6 wiring spec for APU-expanded mode (SPI2 at 40 MHz).
- `docs/MTFP21_TIMING_ENCODING.md`: MTFP21 gap history encoding — 80% → 96% classification.
- `docs/NEXT_STEPS.md`: MTFP dot encoding for LP neurons — 5 trits/neuron, full assembly spec.
- `docs/AUDIT_APRIL_2026.md`: Full repository audit — architecture verified, 6 issues identified, 4 remediated.

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
| **Audit Remediation** | **13/13** | **Monolith split (gie_engine.c), ESP-NOW refactored, dead code archived** | **`c815869`** |
| **Kinetic Attention (Phase 5)** | **14/14** | **TEST 14: agreement-weighted gate bias, 3 conditions, confound-controlled** | **`429ce38`** |
| **MTFP21 Gap History** | **14/14** | **Classification 80% → 96%, P1-P2 timing degeneracy resolved** | **`c814e51`** |
| **TriX ISR Agreement** | **14/14** | **ISR vs CPU core_pred agreement 100%, 80% was reporting artifact** | **`0b09f69`** |
| **LP Dot Magnitude Probe** | diagnostic | **P1-P2 separation IS in magnitudes; sign() is the bottleneck** | **`7391876`** |

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
| **Kinetic attention (Phase 5)** | **Verified** | **14/14 PASS: agreement-weighted gate bias, per-group fire rate shift, confound-controlled** |
| **MTFP21 gap history** | **Verified** | **5-gap temporal encoding, classification 80% → 96%** |
| **TriX ISR ↔ CPU agreement** | **Verified** | **100% agreement; 80% finding was GDMA offset resolution artifact** |

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
│     CfC blend: Phase 5 — gate_threshold=90, per-group bias       │
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
├── embedded/                       # ESP32-C6 firmware
│   ├── main/
│   │   ├── ulp/main.S              # LP core: hand-written RISC-V (CMD 1-5)
│   │   ├── gie_engine.c            # GIE core: ISR, peripherals, TriX, LP interface
│   │   ├── geometry_cfc_freerun.c  # Test orchestrator: app_main(), shared state
│   │   ├── test_harness.h          # Shared constants, state decls, MTFP encoder
│   │   ├── test_gie_core.c         # Tests 1-8: GIE, LP core, VDB, pipeline, feedback
│   │   ├── test_espnow.c           # Tests 9-10: ESP-NOW receive, live input
│   │   ├── test_live_input.c       # Test 11: classification + enrollment
│   │   ├── test_memory.c           # Tests 12-13: memory-modulated attention
│   │   ├── test_kinetic.c          # Tests 14, 14C: kinetic attention, CLS transition
│   │   ├── test_lp_char.c          # LP characterization + dot magnitude diagnostic
│   │   ├── reflex_vdb.c            # HP-side VDB API
│   │   ├── reflex_espnow.c         # ESP-NOW receiver state + functions
│   │   └── espnow_sender.c         # Board B sender (separate build target)
│   ├── include/                     # Active headers (4 files)
│   │   ├── gie_engine.h             # GIE public interface
│   │   ├── reflex.h                 # Core primitive (reflex_channel_t)
│   │   ├── reflex_vdb.h             # VDB API declarations
│   │   ├── reflex_espnow.h          # ESP-NOW declarations
│   │   └── archive/                 # 50+ archaeological headers
│   └── docs/                        # Embedded-specific docs
│
├── docs/                            # Project documentation (27 files)
├── sim/                             # AVX2 simulations (test14c.c)
├── journal/                         # Lincoln Manifold Method working files
├── experiments/                     # Coordination experiments (Thor/Pi4)
├── primitive/                       # reflex.h — original primitive
└── archive/                         # Historical logs and captures
```

---

## What's Next

**14/14 PASS (April 6, 2026).** Phase 5 kinetic attention verified on silicon. LP dot magnitude diagnostic confirms next bottleneck. MTFP dot encoding spec ready.

### Immediate Priority: MTFP Dot Encoding → LP Dimensionality Resolution

| Step | What | Status |
|------|------|--------|
| **MTFP dot encoding** | Implement 5-trit per LP neuron (sign + 2 exp + 2 mantissa). LP hidden expands from 16 to 80 trits. Full spec in `docs/NEXT_STEPS.md` | **Spec complete (`81175c6`), implementation pending** |
| **VDB dimension update** | VDB snapshot expands from 48 to 112 trits (32 GIE + 80 LP). Node size grows from 32 to 56 bytes. Capacity drops from 64 to ~36 nodes | After MTFP encoding |
| **Re-run TEST 12–14** | Verify P1-P2 separation resolves with MTFP encoding. Hamming should increase significantly | After VDB update |
| **Dimensionality decision** | If MTFP resolves P1-P2: proceed to papers. If not: wider LP needed before Pillar 3 | After re-run |

### Blocking Before Any Paper Submission

1. **UART falsification** — Re-route console to GPIO 16/17, power from battery/dumb USB, run full test suite without JTAG. The "peripheral-autonomous" claim requires this data. Currently: JTAG attached for all runs.

### Strategic Phase Order

| Phase | What | Status |
|-------|------|--------|
| **Phase 1** | TriX dot extraction in ISR | **DONE** |
| **Phase 2** | TriX classification channel (reflex_signal, 430 Hz) | **DONE — `b79f09b`** |
| **Phase 3** | Disable CfC blend (gate_threshold = INT32_MAX) | **DONE — `c6fd284`** |
| **Phase 4** | Skip hidden re-encode when blend disabled | **DONE — `8a33369`** |
| **TEST 12** | Memory-modulated attention confirmed | **DONE — `38a0811`** |
| **TEST 13** | CMD 4 distillation — VDB causally necessary | **DONE — `12aa970`** |
| **LP CHAR** | LP dynamics characterization (Path A vs B) | **DONE — `625b00d`** |
| **Refactor** | Core vs. test layer separation | **DONE — `c815869`** |
| **Phase 5 / TEST 14** | Kinetic attention: LP prior → GIE gate bias | **DONE — `429ce38`, 14/14 PASS** |
| **MTFP21 gap encoding** | Temporal input encoding for P1-P2 timing | **DONE — `c814e51`, 80% → 96%** |
| **LP dot magnitude probe** | Confirm P1-P2 separation is in magnitudes | **DONE — `7391876`** |
| **MTFP dot encoding** | 5 trits/neuron, resolve sign() bottleneck | Spec complete, implementation pending |
| **Paper 1** | TEST 12/13/14 hardware paper (Stratum 1) | Draft in progress (`docs/PAPER_KINETIC_ATTENTION.md`) |
| **Paper 2** | CLS architecture paper (Stratum 2) | After MTFP dot encoding data |
| **Paper 3** | Prior-signal separation note (Stratum 3) | Near complete — one session |
| **Pillar 1** | Dynamic scaffolding (VDB sliding window) | After papers |
| **Pillar 2** | SAMA (substrate-aware multi-agent) | After papers |
| **Pillar 3** | Hebbian GIE weight updates | After dimensionality resolved (MTFP dot encoding or wider LP) |

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
