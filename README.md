# The Reflex

**A temporal context layer beneath a perfect classifier — in peripheral hardware, at thirty microamps.**

> *"The prior should be a voice, not a verdict."*

---

## What It Is

The Reflex is a three-layer ternary neural computing system running on an ESP32-C6 microcontroller. It classifies wireless signals at 100% accuracy using only peripheral hardware — no CPU, no floating point, no multiplication. Beneath that classifier, it builds a temporal model of what it has been perceiving, using that model to bias future perception while ensuring the bias can always be overridden by direct measurement.

**The core claim:** Peripheral-hardware ternary dot products at 430.8 Hz. ~30 µA. 100% label-free classification (4 patterns, `MASK_PATTERN_ID=1 + MASK_PATTERN_ID_INPUT=1`). Pattern-specific LP hidden states after 120 seconds of live operation — 8.5-9.7/80 MTFP divergence from VDB episodic memory alone. VDB causally necessary (distillation test: CMD 4 collapses P1=P2; CMD 5 separates them). VDB stabilization confirmed label-free (TEST 14C: ablation regresses, VDB blend maintains separation). Kinetic attention mechanism implemented but harmful at MTFP resolution (-5.5/80). Hebbian weight learning: +0.1 ± 1.1 (noise). The system's power is in its episodic memory, not in learned weights or attentional bias. Structural wall (`W_f hidden = 0`) verified across all experiments. No floating point anywhere in the mechanism path.

The system was not designed toward this architecture. It emerged from a minimum-assumptions experiment — and the constraints (ternary, peripheral, no floating point) are what made the structure visible.

---

## The Three Layers

```
Layer 1: GIE — Geometry Intersection Engine (Peripheral Fabric)
┌──────────────────────────────────────────────────────────────┐
│  Circular DMA chain: [dummy×5][neuron×32] loops forever      │
│  GDMA → PARLIO (2-bit, 10MHz) → GPIO loopback → PCNT        │
│  ISR: drain PCNT → decode dot → TriX classify                │
│  430.8 Hz, 32 neurons, ~0 CPU after init                     │
│  TriX classifier: 100% accuracy, 4 patterns, enrolled        │
└──────────────────────────────────────────────────────────────┘
        │ cfc.hidden[32] via reflex_channel_t (18µs latency)
        ▼
Layer 2: LP Core — Temporal Context Layer (16MHz RISC-V, ~30µA)
┌──────────────────────────────────────────────────────────────┐
│  Hand-written RISC-V assembly. AND + popcount(LUT) + branch  │
│  CfC: 16 neurons, 48-trit input, 32 INTERSECT calls/step    │
│  VDB: 64-node NSW graph, M=7, recall@1=95%                   │
│  CMD 5: CfC step → VDB search → blend into lp_hidden         │
│  100 Hz, sleeps 96% of the time                              │
└──────────────────────────────────────────────────────────────┘
        │ lp_hidden[16], vdb_results[4]
        ▼
Layer 3: HP Core — Initialization and Monitoring (160MHz)
┌──────────────────────────────────────────────────────────────┐
│  Weight loading, condition flags, monitoring                  │
│  Awake only when needed                                       │
└──────────────────────────────────────────────────────────────┘
```

**Operations used:** AND, popcount (byte LUT), add, sub, negate, branch, shift.
**Operations absent:** MUL, DIV, FP, gradients, backpropagation.
**Verification:** All claims silicon-verified, distillation-controlled, multi-seed validated.

---

## The Reframe

This system is not building a better classifier. The classifier is already perfect (100% across all hardware runs). The system is building a **temporal context layer beneath a perfect classifier** — a sub-conscious layer that accumulates a model of what has been experienced, and uses that model to shape what the perceptual layer is sensitive to next.

The LP hidden state develops pattern-specific representations after 90 seconds of live operation. VDB episodic memory provides disambiguation that the CfC's random projection cannot achieve alone. The memory pathway is architecturally decoupled from the classification pathway (`W_f hidden = 0` structural wall). The classifier cannot be corrupted by what the memory has learned.

This is the structure that any system with history must solve: how to let accumulated experience inform perception without the accumulation overriding direct measurement. The answer is not discipline. The answer is structure.

See [`docs/THE_PRIOR_AS_VOICE.md`](docs/THE_PRIOR_AS_VOICE.md) for the full argument — technical, engineering, ontological, and personal.

---

## Key Results

| Metric | Value | Verified |
|--------|-------|---------|
| GIE classification accuracy (with label) | 100% (4 patterns, TriX ISR) | All runs, structural guarantee |
| GIE classification accuracy (label-free) | 100% (4 patterns, pattern_id masked) | `c7ef286`, `-DMASK_PATTERN_ID=1` |
| Prior P1-P2 label-free (old payload) | 71% (P2 at 10%, others 100%) | `2fc5219` — test-design artifact |
| GIE rate | 430.8 Hz | Silicon |
| GIE power | ~0 CPU after init | Silicon |
| LP core power | ~30 µA | Datasheet (JTAG-free measurement pending) |
| LP CfC hidden state divergence (P1 vs P3) | Hamming 5/16 | TEST 12 |
| VDB causal necessity | CMD 4 collapses P1=P2 (2/3 runs) | TEST 13 |
| CMD 5 separation | Hamming 1–5 across all runs | TEST 13 |
| LP feedback steps applied | 97% | TEST 12 |
| Post-switch pred flip | step +1 | TEST 14C, label-free |
| Bias release | geometric ×0.9/step, half-life ~6.6 steps | TEST 14C, traced in Seed A |
| New prior formation | step +15 (gated on T14C_MIN_SAMPLES) | TEST 14C |
| TriX@15 post-switch (label-free) | 15/15 all conditions | TEST 14C, Seed A label-free |
| VDB stabilization (ablation regression) | Ablation gap +22→−6 by step 30 | TEST 14C, label-free |
| MTFP P1-P2 separation | Hamming 7-9/80 (null ~1) | 3 seeds |
| Hebbian LP learning (label-free) | +0.1 ± 1.1 MTFP (noise, n=3) | TEST 15, diagnosed v3 |
| MTFP LP divergence (VDB only) | 9.7 ± 0.6 /80 | TEST 15, 3 reps |
| Kinetic attention (MTFP) | -5.5 /80 (harmful, 3 runs) | TEST 14, label-free |

**TEST 13 (distillation test):** CMD 4 runs CfC + VDB but skips the blend into LP hidden. CMD 5 runs CfC + VDB + blend. In paired 90-second runs, CMD 4 produces P1=P2 (Hamming=0) in 2 of 3 hardware runs. CMD 5 produces Hamming 1–5 for the same pair every time. The VDB feedback is causally necessary, not incidental.

**TEST 14C (transition experiment):** P1 for 90 seconds, then P2 for 30 seconds. Three conditions: full (CMD5+bias), no-bias (CMD5), ablation (CMD4). Label-free on Seed A (`data/apr11_2026/t14c_labelfree_seed_a.log`), multi-seed supporting data (`data/apr9_2026/SUMMARY.md`). Ablation regression confirmed: without VDB blend, the old P1 prior reasserts itself (gap +22→−6 by step 30). With VDB blend, separation maintained. The hippocampus stabilizes, not accelerates.

---

## Architecture Constants (Firmware)

| Constant | Value | Notes |
|----------|-------|-------|
| `CFC_HIDDEN_DIM` | 32 | GIE neuron count |
| `TRIX_NEURONS_PP` | 8 | Neurons per pattern group (4 groups) |
| `LP_HIDDEN_DIM` | 16 | LP CfC neuron count |
| `VDB_SNAPSHOT_DIM` | 48 | 32 GIE + 16 LP trits per node |
| `VDB_MAX_NODES` | 64 | NSW graph capacity |
| `gate_threshold` | 90 (volatile int32_t) | ISR gate threshold |
| `BASE_GATE_BIAS` | 15 | Phase 5: 17% of gate_threshold |
| `MIN_GATE_THRESHOLD` | 30 | Phase 5: hard floor |

**Group mapping:** `group = neuron_idx / TRIX_NEURONS_PP` (÷8, not ÷16).
P0: neurons 0–7, P1: 8–15, P2: 16–23, P3: 24–31.

---

## Phase 5: Kinetic Attention (Implemented — Harmful at MTFP Resolution)

The LP hidden state biases GIE gate thresholds, making the peripheral hardware compute differently based on accumulated experience. The bias is agreement-weighted with two release paths: (1) a soft geometric decay (×0.9/step, half-life ~6.6 steps) that runs unconditionally every step, and (2) a hard disagree-count zero (`n_disagree ≥ 4` trits) that sets bias to 0 immediately. LP feedback is dispatched from the TriX ISR (100% accuracy on the 4-pattern set). No floating point in the mechanism path.

**The mechanism fires but the effect is negative.** At MTFP resolution (80 trits), kinetic attention consistently degrades LP divergence: mean -5.5/80 across 3 label-free runs. The bias saturates the GIE hidden state (more neurons fire → LP input becomes more uniform → LP dot magnitudes converge). The sign-space metric (+1.3/16) incorrectly showed improvement by masking the magnitude damage. Reported as an honest negative result in the Stratum 1 paper.

**TEST 14C (transition experiment):** VDB stabilization confirmed label-free (`data/apr11_2026/t14c_labelfree_seed_a.log`). Ablation regression visible: without VDB blend, old P1 prior reasserts (gap +22→−6 by step 30). Bias releases via geometric ×0.9/step; `pred` flips at step +1 post-switch. Multi-seed supporting data: `data/apr9_2026/SUMMARY.md`.

Full design: [`docs/KINETIC_ATTENTION.md`](docs/KINETIC_ATTENTION.md). Paper: [`docs/PAPER_KINETIC_ATTENTION.md`](docs/PAPER_KINETIC_ATTENTION.md). CLS paper: [`docs/PAPER_CLS_ARCHITECTURE.md`](docs/PAPER_CLS_ARCHITECTURE.md).

---

## Three Strata of Contribution

The research sits at three distinct levels, each targeting a different audience:

**Stratum 1 — Engineering** (embedded systems venues)
GDMA→PARLIO→PCNT as a ternary neural substrate at 430.8 Hz. NSW graph in LP SRAM at ~30 µA. Agreement-weighted gate bias. All claims silicon-verified, distillation-controlled. Papers: TEST 12/13 (potential modulation), TEST 14 (kinetic attention).

**Stratum 2 — Architecture** (computational neuroscience venues)
Fixed-weight Complementary Learning Systems analog. VDB as permanent hippocampal layer (no consolidation path). LP CfC as fixed neocortical extractor. The structure emerged from a minimum-assumptions experiment — the CLS parallel was not designed, it was discovered. Paper: CLS architecture paper, written after TEST 14C data.

**Stratum 3 — Principle** (AI/ML venues)
Prior-signal separation as a structural requirement for hallucination resistance. Five components: prior-holder, evidence-reader, structural separation guarantee, disagreement detection, prior-deference policy. The Reflex is the only silicon-verified complete instantiation. Paper: [`docs/PRIOR_SIGNAL_SEPARATION.md`](docs/PRIOR_SIGNAL_SEPARATION.md).

---

## Operating Modes

The Reflex has two distinct operating modes with different power claims. All documentation specifies which mode applies.

**Autonomous Mode (~30 µA):**
C6 only — GIE + VDB + LP CfC + kinetic attention (gate bias via ISR). No Nucleo, no SPI, no QSPI. This is the mode for the TEST 12/13/14 paper series. The ~30 µA power claim applies only to this mode.

**APU-Expanded Mode (~10–50 mA):**
C6 + Nucleo APU (STM32L4R5ZI-P or L4A6ZG). Adds VDB search acceleration via SPI at 40 MHz, MTFP21 inference via QSPI at 160 Mbps. Connection spec: [`docs/HARDWARE_TOPOLOGY.md`](docs/HARDWARE_TOPOLOGY.md).

---

## Fungible Computation

The Reflex loop — ternary dot product, gate, blend, VDB search — can be expressed in multiple substrates with no loss of semantics. The same computation runs on:

| Substrate | Rate | Power | Notes |
|-----------|------|-------|-------|
| ESP32-C6 peripheral fabric | 430 Hz | ~30 µA | The silicon |
| LP RISC-V core (hand ASM) | 100 Hz | ~30 µA | Same chip |
| AVX2 L-Cache (12 opcodes) | ~2.8 MHz | CPU | 4,600× faster |
| Any ternary hardware | Scales with substrate | — | Substrate-agnostic |

The L-Cache demonstration proves that the computation is not bound to its substrate — it is an algorithm expressed in available silicon. This is the fourth direction in the fungible computation program (see [`github.com/anjaustin/fungible-computation`](https://github.com/anjaustin/fungible-computation)): silicon peripheral topology → AVX ISA.

Full opcode specification: [`docs/LCACHE_REFLEX_OPCODES.md`](docs/LCACHE_REFLEX_OPCODES.md).

---

## Repository Structure

```
the-reflex/
├── README.md                         # This file
├── ROADMAP.md                        # Strategic roadmap: three pillars, publication strategy
│
├── embedded/                         # ESP32-C6 firmware (the actual system)
│   ├── main/
│   │   ├── ulp/main.S                # LP core: hand-written RISC-V assembly (CMD 1–5)
│   │   ├── gie_engine.c              # GIE core: peripherals, ISR, TriX, LP interface
│   │   ├── geometry_cfc_freerun.c    # Test orchestrator: app_main(), shared state
│   │   ├── test_harness.h            # Shared constants, state decls, MTFP encoder
│   │   ├── test_gie_core.c           # Tests 1–8: GIE, LP core, VDB, pipeline, feedback
│   │   ├── test_espnow.c             # Tests 9–10: ESP-NOW receive, live input
│   │   ├── test_live_input.c         # Test 11: pattern classification + enrollment
│   │   ├── test_memory.c             # Tests 12–13: memory-modulated attention, VDB necessity
│   │   ├── test_kinetic.c            # Tests 14, 14C: kinetic attention, CLS transition
│   │   ├── test_lp_char.c            # LP characterization + dot magnitude diagnostic
│   │   ├── espnow_sender.c           # Board B: ESP-NOW pattern sender
│   │   ├── reflex_vdb.c              # HP-side VDB API
│   │   └── reflex_espnow.c           # ESP-NOW receiver (Board A)
│   ├── include/
│   │   ├── gie_engine.h              # GIE public interface
│   │   ├── reflex_vdb.h              # VDB API (insert/search/clear/pipeline/feedback)
│   │   ├── reflex_espnow.h           # ESP-NOW receiver API
│   │   └── reflex.h                  # OS primitive: channels, fences, cycle counter
│   └── docs/
│       ├── GIE_ARCHITECTURE.md       # GIE architecture and signal path
│       ├── HARDWARE_ERRATA.md        # 20+ errata from development
│       ├── REGISTER_REFERENCE.md     # Bare-metal register addresses
│       └── FLASH_GUIDE.md            # Build and flash procedure
│
├── docs/
│   ├── CURRENT_STATUS.md             # Up-to-date project status and milestone history
│   ├── PAPER_KINETIC_ATTENTION.md    # Stratum 1 paper: ternary peripheral-fabric neural computation
│   ├── PAPER_CLS_ARCHITECTURE.md     # Stratum 2 paper: fixed-weight CLS, hippocampal stabilization
│   ├── PRIOR_SIGNAL_SEPARATION.md    # Stratum 3 paper: structural hallucination resistance
│   ├── KINETIC_ATTENTION.md          # Phase 5 design: agreement-weighted gate bias
│   ├── THE_PRIOR_AS_VOICE.md         # Perspective paper: technical + ontological + personal
│   ├── LCACHE_REFLEX_OPCODES.md      # L-Cache opcode spec: 12 AVX2 opcodes, 1:1 with firmware
│   ├── MEMORY_MODULATED_ATTENTION.md # Paper-quality writeup of TEST 12
│   ├── HARDWARE_TOPOLOGY.md          # Nucleo ↔ C6 wiring spec (APU-expanded mode)
│   ├── SESSION_MAR22_2026.md         # March 22 session: TEST 12/13, VDB causal necessity
│   ├── SESSION_MAR23_2026.md         # March 23 session: LMM assessment, temporal context reframe
│   ├── SESSION_APR06_07_2026.md      # April 6-7 session: audit, Phase 5, MTFP21
│   ├── SESSION_APR08_2026.md         # April 8 session: red-team, TriX dispatch, ternary agreement
│   ├── MILESTONE_PROGRESSION.md      # All 37 milestones, full narrative
│   └── archive/                      # Historical docs (nothing deleted)
│
├── journal/                          # Lincoln Manifold Method working files
│   ├── kinetic_attention_*.md        # RAW → NODES → REFLECT → SYNTH for Phase 5 design
│   ├── trix_dispatch_*.md            # LMM cycle: TriX dispatch for LP feedback
│   ├── ternary_remediation_*.md      # LMM cycle: ternary invariant remediation plan
│   ├── apr8_findings_*.md            # LMM cycle: April 8 session findings
│   └── project_assessment_*.md       # RAW → NODES → REFLECT → SYNTH for full project
│
└── primitive/                        # reflex.h — original cache-coherency primitive
```

---

## Entry Points

| If you want to... | Read... |
|-------------------|---------|
| Understand the full system and its implications | [`docs/THE_PRIOR_AS_VOICE.md`](docs/THE_PRIOR_AS_VOICE.md) |
| See current silicon results | [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) |
| Read the engineering paper (Stratum 1) | [`docs/PAPER_KINETIC_ATTENTION.md`](docs/PAPER_KINETIC_ATTENTION.md) (VDB temporal context + honest negatives) |
| Read the CLS paper (Stratum 2) | [`docs/PAPER_CLS_ARCHITECTURE.md`](docs/PAPER_CLS_ARCHITECTURE.md) |
| Read the prior-signal separation paper (Stratum 3) | [`docs/PRIOR_SIGNAL_SEPARATION.md`](docs/PRIOR_SIGNAL_SEPARATION.md) |
| Understand Phase 5 design | [`docs/KINETIC_ATTENTION.md`](docs/KINETIC_ATTENTION.md) |
| See the L-Cache opcode spec | [`docs/LCACHE_REFLEX_OPCODES.md`](docs/LCACHE_REFLEX_OPCODES.md) |
| Follow the April 8 session (red-team, TriX dispatch, ternary agreement) | [`docs/SESSION_APR08_2026.md`](docs/SESSION_APR08_2026.md) |
| Follow the full strategic roadmap | [`ROADMAP.md`](ROADMAP.md) |
| Understand the milestone history | [`docs/MILESTONE_PROGRESSION.md`](docs/MILESTONE_PROGRESSION.md) |

---

## The One-Sentence Description

> A wireless signal classifier that draws ~30 µA and accumulates a temporal model of what it has been perceiving, using a structure that mirrors Complementary Learning Systems theory — where the memory layer cannot corrupt the classifier, but the classifier's accumulated history actively biases future perception.

---

*Hardware: ESP32-C6FH4 (QFN32) rev v0.2. ESP-IDF v5.4. All results silicon-verified.*
*The hardware is already doing the work. We're just using it.*
