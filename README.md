# The Reflex: Sub-Microsecond Robotics Coordination

**Cache coherency signaling for 10kHz robotics control with 926ns P99 latency.**

> **For a cold, honest assessment of what exists and what doesn't, read [`TECHNICAL_REALITY.md`](TECHNICAL_REALITY.md).**
>
> **LATEST DOCUMENTATION (March 19, 2026):**
> *   [`READMETOO.md`](READMETOO.md) - Deep Technical Audit & Falsification Report.
> *   [`WHITEPAPER.md`](WHITEPAPER.md) - The Reflex Manifesto: Substrate-Aware Agency.
> *   [`PAP_PAPER.md`](PAP_PAPER.md) - Peripheral-As-Processor (PaP) Micro-Architecture Academic Draft.
> *   [`ROADMAP.md`](ROADMAP.md) - Future Horizons: Strategic Step-Changes.
>
> This README is written for GitHub.

---

<p align="center">
  <strong>255x faster than baseline Linux. Tested on NVIDIA Jetson AGX Thor.</strong>
</p>

---

## Headline Results

| Platform | Metric | Result |
|----------|--------|--------|
| **Jetson Thor** | P99 Latency | **926 ns** (255x improvement over baseline) |
| **Jetson Thor** | Control Rate | **10 kHz** sustained |
| **ESP32-C6** | GIE Free-Running | **428 Hz**, 64 neurons, ~0 CPU (peripheral hardware IS the neural network) |
| **ESP32-C6** | **TriX Classification** | **32/32 = 100%** — ESP-NOW wireless input classified by hardware, zero training |
| **ESP32-C6** | TriX ISR Channel | **430 Hz** loop rate, packed dots via `reflex_signal()`, 62 Hz effective clean rate |
| **ESP32-C6** | LP Core CfC | **100 Hz**, 16 neurons, ~30uA (hand-written RISC-V assembly) |
| **ESP32-C6** | Ternary VDB | **64 nodes**, NSW graph, recall@1=95%, recall@4=90% |
| **ESP32-C6** | CfC Stripped (Phases 3-4) | Blend disabled + re-encode skipped — TriX-only, 0% gate firing, ~20us saved/loop |
| **ESP32-C6** | Verified Milestones | **37 milestones**, all verified exact on silicon, 11/11 tests pass |
| **ESP32-C6** | Signal Path | GDMA → PARLIO(2-bit, 10MHz) → GPIO loopback → PCNT(agree/disagree) |

## What is The Reflex?

The Reflex uses **cache coherency traffic** as a coordination primitive—the mechanism by which multi-core processors maintain memory consistency. No syscalls, no locks, no DDS overhead.

```
Sensor (Core 0) ──→ Controller (Core 1) ──→ Actuator (Core 2)
       │                    │                     │
       └──── 324 ns ────────┴────── 240 ns ───────┘
                     Total: 620 ns median
```

**Traditional approach:** ROS2/DDS → ~100μs per hop → max ~5kHz control
**The Reflex:** Cache coherency → ~300ns per hop → 10kHz+ control

---

## Repository Structure

```
the-reflex/
├── TECHNICAL_REALITY.md          # Honest technical assessment (START HERE)
├── README.md                     # This file (GitHub overview)
│
├── reflex-os/                    # ESP32-C6: GIE + LP CORE + VDB (THE ACTUAL SYSTEM)
│   ├── main/
│   │   ├── ulp/main.S            # LP core: hand-written RISC-V assembly (cmd 1-5)
│   │   ├── geometry_cfc_freerun.c  # Current entry point: GIE + LP + VDB + tests
│   │   ├── reflex_vdb.c          # HP-side VDB API implementation
│   │   └── [earlier milestones]  # M1-M7 source files (historical, not active)
│   ├── include/
│   │   ├── reflex_vdb.h          # VDB API (insert/search/clear/pipeline/feedback)
│   │   ├── reflex.h              # Core coordination primitive
│   │   └── ...
│   └── docs/                     # Technical docs (architecture, errata, registers)
│
├── reflex-robotics/              # Jetson Thor cache coherency work (separate system)
│
├── docs/                         # Current project documentation
│   ├── CURRENT_STATUS.md         # Up-to-date project status
│   ├── MILESTONE_PROGRESSION.md  # All 37 milestones documented
│   ├── FALSIFICATION_COMPLETE.md # Adversarial test results
│   ├── HARDWARE_INVENTORY.md     # Physical hardware list
│   └── archive/                  # Historical docs (nothing deleted)
│       ├── lmm/                  # 40 Lincoln Manifold Method explorations
│       ├── prds/                 # 20 historical PRDs
│       ├── sessions/             # Session notes, demo prep, deployment guides
│       └── concepts/             # Speculative/philosophical docs
│
├── journal/                      # LMM journal (4 phases, current)
├── notes/                        # Design notes, LMM method definition
│   ├── LMM.md                    # The Lincoln Manifold Method (process)
│   ├── lmm/                      # GIE-specific LMM analysis
│   └── archive/                  # Business planning, evolution notes
│
├── the-reflex-tvdb.md            # VDB PRD (6 milestones, all complete)
├── delta-observer/               # Neural network observation research
└── notebooks/                    # Colab demos
```

---

## Quick Start: Robotics Control Loop

### On Jetson AGX Thor (or any ARM64 Linux)

```bash
git clone https://github.com/EntroMorphic/the-reflex.git
cd the-reflex/reflex-robotics

# Build
make all

# Run (needs root for SCHED_FIFO)
sudo taskset -c 0-2 ./build/control_loop
```

### Expected Output

```
╔═══════════════════════════════════════════════════════════════╗
║       REFLEX ROBOTICS: 10kHz CONTROL LOOP DEMO                ║
╚═══════════════════════════════════════════════════════════════╝

Coordination Latency (nanoseconds):
┌─────────────────────┬──────────┬──────────┐
│ Hop                 │  Median  │   P99    │
├─────────────────────┼──────────┼──────────┤
│ Sensor → Controller │    324   │    334   │
│ Controller → Actuat │    240   │    306   │
│ Total Loop          │    620   │    676   │
└─────────────────────┴──────────┴──────────┘

✓ SUCCESS: Sub-microsecond control loop achieved!
```

---

## Quick Start: Research Experiments

### On Raspberry Pi (Recommended)

```bash
git clone https://github.com/EntroMorphic/the-reflex.git
cd the-reflex/src

# Latency comparison
gcc -O3 -Wall -pthread e3_latency_comparison.c -o e3_latency -lm
./e3_latency
```

### On Google Colab (One-Click)

[![Open In Colab](https://colab.research.google.com/assets/colab-badge.svg)](https://colab.research.google.com/github/EntroMorphic/the-reflex/blob/main/notebooks/stigmergy_demo.ipynb)

---

## The Journey: From 236μs to 926ns

| Phase | Change | P99 Latency | Improvement |
|-------|--------|-------------|-------------|
| Baseline | Stock Linux | 236 μs | - |
| Phase 1 | +SCHED_FIFO, +mlockall | 2.4 μs | 98x |
| **Phase 4** | +isolcpus, +rcu_nocbs | **926 ns** | **255x** |

Full documentation in [`reflex-robotics/docs/`](reflex-robotics/docs/).

---

## How It Works

### The Coordination Primitive

```c
// reflex.h - 64-byte cache-aligned channel
typedef struct {
    volatile uint64_t sequence;   // Monotonically increasing
    volatile uint64_t timestamp;  // Producer's timestamp
    volatile uint64_t value;      // Optional payload
    char padding[40];             // Pad to cache line
} __attribute__((aligned(64))) reflex_channel_t;

// Producer: Signal with sub-100ns overhead
static inline void reflex_signal(reflex_channel_t* ch, uint64_t ts) {
    ch->timestamp = ts;
    ch->sequence++;
    __asm__ volatile("dsb sy" ::: "memory");  // ARM barrier
}

// Consumer: Wait for signal (spinning)
static inline uint64_t reflex_wait(reflex_channel_t* ch, uint64_t last_seq) {
    while (ch->sequence == last_seq) {
        __asm__ volatile("" ::: "memory");  // Compiler barrier
    }
    return ch->sequence;
}
```

### Why It's Fast

| Mechanism | Path | Latency |
|-----------|------|---------|
| **Reflex** | Cache write → snoop → invalidate → reload | ~300 ns |
| Futex | User → kernel → scheduler → wake → user | ~9,000 ns |
| DDS | Serialize → network stack → deserialize | ~100,000 ns |

The hardware already maintains cache coherency. We're just using it.

---

## Platforms Tested (Falsified)

| Platform | Architecture | Processing Time | Under Stress | 10kHz Control |
|----------|--------------|-----------------|--------------|---------------|
| **Jetson AGX Thor** | 14-core ARM | 309 ns | 366 ns (+18%) | ✅ 926ns P99 |
| **ESP32-C6** | RISC-V | 87 ns (ideal) / 187 ns (realistic) | 5.5 μs max | ✅ 10kHz + autonomous ALU (9/9) |
| Raspberry Pi 4 | 4-core ARM | 167 ns | - | ✅ (untested) |
| x86_64 Linux | Intel/AMD | ✅ works | - | ⚠️ (no isolcpus test) |

All numbers verified under adversarial testing with 100K+ samples. Zero catastrophic failures (>6μs) observed.

---

## The Three-Layer Ternary Reflex Arc

The deepest layer of The Reflex: a three-layer hierarchy where **peripheral hardware IS the neural network**, a **micro-core IS the sub-conscious**, and the **CPU IS consciousness**.

No floating point. No multiplication. Verified exact on silicon.

### The Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                       THE REFLEX ARC                              │
├──────────────────────────────────────────────────────────────────┤
│                                                                   │
│  Layer 1: GIE — Geometry Intersection Engine (Peripheral Fabric) │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │  Circular DMA chain loops forever: [dummy×5][neuron×64]   │    │
│  │  GDMA → PARLIO (2-bit, 10MHz) → GPIO loopback → PCNT     │    │
│  │  ISR: drain PCNT → decode dot → CfC blend → re-encode    │    │
│  │  428 Hz, 64 neurons, ~0 CPU after init                    │    │
│  └──────────────────────────────────────────────────────────┘    │
│        │ cfc.hidden[32] (updated every ~2.3ms)                    │
│        ▼                                                          │
│  Layer 2: LP Core — Geometric Processor (16MHz RISC-V, ~30uA)   │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │  Hand-written assembly: AND + popcount(LUT) + branch       │    │
│  │  CfC: 16 neurons × 2 pathways = 32 INTERSECT calls        │    │
│  │  VDB: 64-node NSW graph, M=7, recall@1=95%                │    │
│  │  Pipeline (cmd=4): perceive → think → remember             │    │
│  │  Feedback (cmd=5): VDB best match → blend into lp_hidden    │    │
│  │  100 Hz (10ms wake cycle), sleeps 96% of the time          │    │
│  └──────────────────────────────────────────────────────────┘    │
│        │ lp_hidden[16], vdb_results[4]                            │
│        ▼                                                          │
│  Layer 3: HP Core — Full CPU (160MHz, ~15mA)                     │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │  Initialization, weight loading, monitoring                │    │
│  │  Awake only when needed                                    │    │
│  └──────────────────────────────────────────────────────────┘    │
│                                                                   │
├──────────────────────────────────────────────────────────────────┤
│  Operations: AND, popcount, add, sub, negate, branch, shift      │
│  Absent: MUL, DIV, FP, gradients, backpropagation                │
│  Verification: Exact, dot-for-dot, on silicon (37 milestones)    │
└──────────────────────────────────────────────────────────────────┘
```

### 37 Milestones Verified on Silicon

Each verified on ESP32-C6FH4 (QFN32) rev v0.2. See [`docs/MILESTONE_PROGRESSION.md`](docs/MILESTONE_PROGRESSION.md) for the full narrative.

**GIE Foundation (M1-M7):**

| Milestone | Tests | What It Proved |
|-----------|-------|----------------|
| M1: Sub-CPU ALU | 59/59 | PCNT+PARLIO = boolean gates |
| M2: Autonomous Fabric | 5/5 | ETM crossbar peripheral loop |
| M3: Autonomous ALU | 9/9 | GDMA descriptor chain sequencing |
| M4: Ternary TMUL | 9/9 | 2-bit PARLIO + dual PCNT = ternary multiply |
| M5: 256-Trit Dot Product | 10/10 | Multi-buffer DMA accumulation |
| M6: Multi-Neuron Layer | 6/6 | 32-neuron layer, 108.8K trit-MACs/s |
| M7: Ternary CfC | 6/6 | Fully-ternary liquid network, 3 blend modes |

**Free-Running + Scale (M8-M10):**

| Milestone | Tests | What It Proved |
|-----------|-------|----------------|
| M8: 64-Neuron Chain | 4/4 + 3/3 | Giant DMA chain, per-neuron ISR, 64/64 exact |
| M9: 10MHz PARLIO | 6/6 | PCNT keeps up, zero errors at 10x clock |
| M10: Differentiation | 4/5 | Stem cell hypothesis: 4/5 predictions confirmed |
| Free-Run | 3/3 | 428 Hz autonomous CfC, 64/64 exact |

**LP Core (hand-written RISC-V assembly):**

| Milestone | Tests | What It Proved |
|-----------|-------|----------------|
| LP Core ASM | 4/4 | 16/16 exact dots, every instruction specified |

**Ternary Vector Database:**

| Milestone | Tests | What It Proved |
|-----------|-------|----------------|
| VDB M1: Top-K | 5/5 | Top-4 sorted search |
| VDB M2: Scale | 5/5 | 64 nodes, latency benchmark |
| VDB M3: HP API | 5/5 | Clean C API (insert/search/clear/count) |
| VDB M4: NSW Graph | 6/6 | M=7, ef=32, recall@1=95%, recall@4=90% |
| **VDB M5: Pipeline** | **4/4** | **Perceive+think+remember in one LP wake** |
| Reflex Channel | 7/7 | ISR→HP coordination, 18us avg latency |
| **VDB→CfC Feedback** | **8/8** | **Memory shapes inference, HOLD damping stable** |

**TriX Classification (ESP-NOW → hardware pattern recognition):**

| Milestone | Tests | What It Proved |
|-----------|-------|----------------|
| TriX Signatures | 11/11 | Zero-shot classification from observed signatures, 78%→100% |
| ISR TriX | 11/11 | DMA race solved, ISR classifies at 430 Hz, 87%→100% |
| **TriX Channel** | **11/11** | **Packed dots via reflex_signal, channel-based consumer** |
| **CfC Blend Disabled** | **11/11** | **Phase 3: gate_threshold=INT32_MAX, 0% firing, TriX-only** |
| **Re-encode Skipped** | **11/11** | **Phase 4: step 5 gated out, saves ~20us/loop** |

### The CfC: Three Blend Modes (DISABLED — Phase 3)

```
f[n] = sign(dot(concat, W_f[n]))   // gate:      {-1, 0, +1}
g[n] = sign(dot(concat, W_g[n]))   // candidate: {-1, 0, +1}
h_new = (f == 0) ? h_old : f * g   // ternary blend
```

- **f = +1 (UPDATE):** Accept candidate. Follow the evidence.
- **f =  0 (HOLD):** Keep current state. Maintain belief.
- **f = -1 (INVERT):** Negate candidate. Actively oppose.

The inversion mode is unique to ternary CfC. It creates oscillation, convergence resistance, and path-dependent memory — dynamical primitives absent from binary CfC.

### Key Insight

> *"We found computation hiding in the peripheral fabric."*
>
> PCNT was designed to count encoder pulses. PARLIO was designed for LCD interfaces. GDMA was designed for data transfer. Together, they form a geometry intersection engine that computes ternary neural network inference while the CPU does nothing.
>
> The constraint is generative: ternary {-1, 0, +1} maps to 2-bit GPIO encoding, which maps to PARLIO loopback, which maps to PCNT edge/level gating. Floating point would make none of this possible.

---

## The Reflex Becomes the ESP32-C6

On the ESP32-C6, The Reflex IS the operating system — from 12ns GPIO to an autonomous neural network:

| Layer | What | Rate | Power |
|-------|------|------|-------|
| GIE | Peripheral-fabric CfC (64 neurons) | 428 Hz | ~0 CPU |
| LP core | Geometric CfC + VDB (hand ASM) | 100 Hz | ~30uA |
| HP core | Init + monitoring | On demand | ~15mA |
| `gpio_write()` | Direct register | 12 ns | 2 cycles |

### What's on the LP Core (16KB SRAM, Every Byte Accounted For)

- **CfC neural network:** 16 neurons, 48-trit inputs, 32 INTERSECT calls per step
- **Ternary VDB:** 64 nodes, NSW graph (M=7), recall@1=95%, recall@4=90%
- **Pipeline (cmd=4):** CfC step → copy packed trits → VDB search, one wake cycle
- **Feedback (cmd=5):** CfC step → VDB search → blend best match into lp_hidden, one wake cycle
- **Assembly:** ~7KB of hand-written RISC-V. 256-byte popcount LUT. No compiler.

See [`reflex-os/`](reflex-os/) and [`docs/MILESTONE_PROGRESSION.md`](docs/MILESTONE_PROGRESSION.md).

---

## The Dreaming Swarm Cathedral

Beyond the C6, the full system includes:

| Tier | Device | Role |
|------|--------|------|
| God | Jetson AGX Thor | 100M shape echip, entropy field substrate |
| Mind | Raspberry Pi 4 | OBSBOT controller, slow time layer |
| Neurons | 3× ESP32-C6 | Sub-µs reflexes, swarm nodes |
| Eyes | 2× OBSBOT Tiny | Stereo vision, entropy-driven gaze |
| Choir | 5× Max98357 | Audio sonification of entropy |

See [`docs/HARDWARE_INVENTORY.md`](docs/HARDWARE_INVENTORY.md) for complete details.

---

## Primordial Stillness + Delta Observer

The Reflex extends beyond coordination into **awareness-inspired architecture**:

| Component | Role |
|-----------|------|
| **Primordial Stillness** | 16M voxel entropy field as awareness substrate |
| **Delta Observer** | Watches neural networks learn, captures transient clustering |
| **The Bridge** | Latent vectors become disturbances in the stillness field |

**Key insight:** The Delta Observer discovered that neural networks build **scaffolding** (transient clustering) to learn, then tear it down. Post-hoc analysis misses this entirely. The Primordial Stillness is the substrate being disturbed.

> *"The field at rest models awareness. Perception models disturbance. The architecture sidesteps infinite regress by making the observer definitionally invariant."*

See:
- [`docs/archive/concepts/PRIMORDIAL_STILLNESS.md`](docs/archive/concepts/PRIMORDIAL_STILLNESS.md) - Stillness as computational substrate
- [`docs/archive/concepts/PHILOSOPHY.md`](docs/archive/concepts/PHILOSOPHY.md) - Philosophical speculation (clearly marked)
- [`docs/archive/concepts/DELTA_STILLNESS_BRIDGE.md`](docs/archive/concepts/DELTA_STILLNESS_BRIDGE.md) - The unified architecture
- [Delta Observer (separate repo)](https://github.com/EntroMorphic/delta-observer) - The paper and code

---

## Skeptical Analysis

We challenged every claim. See [`SKEPTICAL_ANALYSIS.md`](reflex-robotics/docs/SKEPTICAL_ANALYSIS.md).

**Key findings:**
- ✅ Median 500-700ns confirmed across 5 runs
- ✅ Core isolation works (load on cores 3-7 doesn't affect 0-2)
- ✅ P99 drops from 236μs to 926ns with proper configuration
- ⚠️ Extreme outliers (50ms) remain without PREEMPT_RT kernel

---

## Falsification Results

We attempted to break our own claims under adversarial testing. They survived.

### Thor (Jetson AGX)

| Condition | Processing Time | Max Observed |
|-----------|-----------------|--------------|
| Normal | 309 ns | 1,268 ns |
| Under CPU stress | 366 ns (+18%) | Stable |

### ESP32-C6

| Condition | Avg | Max | Samples |
|-----------|-----|-----|---------|
| Ideal (interrupts off) | 87 ns | 3.2 μs | 10,000 |
| Realistic (interrupts on) | 187 ns | 5.5 μs | 100,000 |
| Spikes > 6μs | **0%** | - | - |

### Component Breakdown (C6)

| Component | Cost |
|-----------|------|
| Pure decision (threshold + GPIO) | 12 ns |
| PRNG overhead | +75 ns |
| Interrupt servicing | +100 ns |
| Channel coordination (fences) | +350 ns |

**Bottom line:** Sub-microsecond average on both platforms. Single-digit microsecond worst case. Zero catastrophic failures in 100K+ samples.

See: [`docs/FALSIFICATION_COMPLETE.md`](docs/FALSIFICATION_COMPLETE.md)

---

## Configuration for Best Results

### Minimum (Phase 1)
```c
mlockall(MCL_CURRENT | MCL_FUTURE);
sched_setscheduler(0, SCHED_FIFO, &param);
```
**Result:** P99 drops from 236μs to 2.4μs

### Recommended (Phase 4)
```bash
# Boot parameters
isolcpus=0,1,2 rcu_nocbs=0,1,2

# Runtime
sudo taskset -c 0-2 ./control_loop
```
**Result:** P99 drops to 926ns

---

## ROS2 Integration

The Reflex bridges to ROS2 via shared memory, giving you **nanosecond** processing alongside the ROS ecosystem:

```
ROS2 Topics (1kHz)          Native Controller (event-driven)
      │                              │
      ▼                              ▼
┌─────────────┐              ┌─────────────────┐
│ bridge_node │◄── shm ─────►│ reflex_force_   │
│             │              │ control         │
└─────────────┘              └─────────────────┘
      │                              │
/force_sensor              /dev/shm/reflex_*
/gripper_command           ~300ns processing
```

| Mode | Processing Time | Check Rate | Anomalies Caught |
|------|-----------------|------------|------------------|
| **REFLEX** | **~309 ns** | Event-driven | 1,127 |
| ROS2-1kHz | ~500 ns | 1 kHz | ~1,070 |
| ROS2-100Hz | ~500 ns | 100 Hz | ~113 |

**Honest assessment:** REFLEX catches ~5% more anomalies than well-tuned 1kHz polling. The significant win is over typical 100Hz systems (10x more anomalies caught). The key advantage is **event-driven** vs polling—we never miss a signal.

See [`reflex_ros_bridge/`](reflex_ros_bridge/) for full documentation.

---

## Applications

- **Robotics:** 10kHz+ control loops for manipulators, drones, legged robots
- **Autonomous Vehicles:** Sensor fusion with bounded latency
- **Industrial Automation:** PLC-replacement with deterministic timing
- **High-Frequency Trading:** Ultra-low-latency event propagation

---

## Citation

```bibtex
@misc{thereflex2026,
  title={The Reflex: Sub-Microsecond Robotics Coordination via Cache Coherency},
  author={EntroMorphic Research},
  year={2026},
  url={https://github.com/EntroMorphic/the-reflex}
}
```

---

## License

MIT License

---

**The hardware is already doing the work. We're just using it.**

*A ternary reflex arc where peripheral hardware IS the neural network, a micro-core IS the sub-conscious, and the CPU IS consciousness — computing with zero multiplication, learning by remembering, adapting from experience, verified exact on silicon.*
