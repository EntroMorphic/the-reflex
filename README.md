# The Reflex: Sub-Microsecond Robotics Coordination

**Cache coherency signaling for 10kHz robotics control with 926ns P99 latency.**

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
| **ESP32-C6** | Sub-CPU ALU | **59/59 tests** (CPU-orchestrated gates) |
| **ESP32-C6** | Autonomous ALU | **9/9 tests** (CPU in NOP loop during evaluation) |
| **ESP32-C6** | Ternary TMUL | **9/9 tests** (ternary multiply via PCNT gating) |
| **ESP32-C6** | 256-Trit Dot Product | **10/10 tests** (descriptor chain accumulation) |
| **ESP32-C6** | Multi-Neuron Layer | **6/6 tests** (2-layer network, 108.8K trit-MACs/s) |
| **ESP32-C6** | Ternary CfC | **6/6 tests** (liquid neural network, 3 blend modes, oscillation confirmed) |
| **ESP32-C6** | Signal Path | GDMA → PARLIO(2-bit) → GPIO 4,5 → PCNT(agree/disagree) |

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
├── reflex-robotics/          # 10kHz CONTROL LOOP (Jetson Thor)
│   ├── src/
│   │   ├── reflex.h          # Core coordination primitive
│   │   └── control_loop.c    # Sensor→Controller→Actuator demo
│   ├── docs/
│   │   ├── PHASE_4_RESULTS.md    # 926ns P99 achieved
│   │   └── SKEPTICAL_ANALYSIS.md # Rigorous falsification
│   └── scripts/
│       └── setup_rt_host.sh  # RT configuration
│
├── reflex_ros_bridge/        # ROS2 INTEGRATION (NEW)
│   ├── src/
│   │   ├── bridge_node.cpp   # ROS2 topics ↔ shared memory
│   │   └── channel.cpp       # SharedChannel class
│   ├── reflex_force_control.c # Native 10kHz force controller
│   └── README.md             # Integration guide
│
├── reflex-os/                # THE REFLEX BECOMES THE ESP32-C6
│   ├── main/
│   │   ├── geometry_layer.c  # Milestone 6: Multi-neuron layer (6/6 verified)
│   │   ├── geometry_dot.c    # Milestone 5: 256-trit dot product (10/10 verified)
│   │   ├── ternary_alu.c     # Milestone 4: Ternary TMUL (9/9 verified)
│   │   ├── autonomous_alu.c  # Milestone 3: Autonomous ALU (9/9 verified)
│   │   ├── alu_fabric.c      # Milestone 1: Sub-CPU ALU (59/59 verified)
│   │   └── raid_etm_fabric.c # Milestone 2: Autonomous fabric (5/5 verified)
│   ├── include/
│   │   ├── reflex.h          # Core primitive (50 lines)
│   │   ├── reflex_gpio.h     # GPIO channels (12ns)
│   │   ├── reflex_timer.h    # Timer channels (10kHz)
│   │   ├── reflex_spline.h   # Catmull-Rom interpolation (137ns)
│   │   ├── reflex_void.h     # Entropy field for TriX echips
│   │   ├── reflex_echip.h    # Self-composing processor
│   │   ├── reflex_obsbot.h   # OBSBOT PTZ camera control
│   │   └── reflex_c6.h       # Master header
│   ├── tools/                # Linux host tools
│   │   ├── obsbot_test.c     # Camera test utility
│   │   └── stereo_demo.c     # Synchronized stereo vision
│   └── docs/
│       ├── ARCHITECTURE.md       # Channel model + entropy field
│       ├── API.md                # Complete API reference
│       ├── BENCHMARKS.md         # Performance measurements
│       ├── RAID_ETM_FABRIC.md    # Autonomous fabric architecture
│       ├── HARDWARE_ERRATA.md    # C6 constraints & workarounds
│       ├── FLASH_GUIDE.md        # Flash & serial procedure
│       └── REGISTER_REFERENCE.md # Bare-metal register addresses
│
├── pulse-arithmetic-lab/     # TEACHING LAB: PCNT + PARLIO neural computation
│   ├── firmware/01-05/       # 5 progressive demos
│   ├── docs/                 # Theory, hardware, ETM formalization
│   └── CLAIMS.md             # 7 falsifiable scientific claims
│
├── src/                      # RESEARCH EXPERIMENTS (the science)
│   ├── e3_latency_comparison.c   # Stigmergy vs Futex benchmark
│   ├── e1_coordination_v3.c      # Causality proof
│   └── e2b_false_sharing.c       # False positive control
│
├── docs/                     # DOCUMENTATION
│   ├── HARDWARE_INVENTORY.md     # All compute resources
│   ├── PI4_SETUP.md              # Raspberry Pi 4 setup
│   ├── LINCOLN_MANIFOLD_*.md     # Design analysis documents
│   └── ...                       # Lincoln Manifold phases
│
└── notebooks/
    └── stigmergy_demo.ipynb  # One-click Colab demo
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

## The Silicon Grail: Autonomous Hardware Computation

The deepest layer of The Reflex: **boolean logic gates evaluated entirely by peripheral hardware** while the CPU sits in a NOP loop.

### Seven Milestones Verified on Silicon

Each milestone built on the last. Each was verified on a physical ESP32-C6FH4 (QFN32) rev v0.2 board. See [`docs/MILESTONE_PROGRESSION.md`](docs/MILESTONE_PROGRESSION.md) for the full narrative.

#### Milestones 1-3: Boolean ALU → Autonomous Computation

| Milestone | Tests | What It Proved | File |
|-----------|-------|----------------|------|
| M1: Sub-CPU ALU | 59/59 | PCNT level-gated edge counting implements AND, OR, XOR, NOT, SHL/SHR, ADD, MUL | `alu_fabric.c` |
| M2: Autonomous Fabric | 5/5 | Peripheral loop runs without CPU (ETM crossbar wiring). *Note: used wrong ETM addrs, corrected in M3.* | `raid_etm_fabric.c` |
| M3: Autonomous ALU | 9/9 | CPU enters NOP loop. GDMA descriptor chains execute gate sequences autonomously. | `autonomous_alu.c` |

#### Milestone 4: Ternary TMUL (9/9 tests)

Transitioned from boolean gates to **ternary arithmetic**. PARLIO 2-bit mode drives two GPIOs (X_pos, X_neg) representing trit values {-1, 0, +1}. Two PCNT units count agree/disagree edges gated by static Y levels. Result = agree - disagree = ternary multiply.

**Key decision:** Switched from 4-bit PARLIO (M1-3) to 2-bit PARLIO. Eliminated the nibble-boundary glitch problem entirely.

**File:** `reflex-os/main/ternary_alu.c` | **Commit:** `66469ce`

#### Milestone 5: 256-Trit Dot Product (10/10 tests)

Scaled from single-trit operations to **128-256 trit vector dot products** via DMA descriptor chains. Zero-interleave encoding: each trit occupies 2 dibit slots (value + silence), guaranteeing one clean rising edge per non-zero trit. 64 bytes = 128 trits per buffer. Chains of 2-4 buffers accumulate across descriptors.

**Key errata:** GDMA with `ETM_EN` won't auto-follow linked lists. Solution: use normal GDMA mode (no `ETM_EN`), gate output with PARLIO `TX_START`.

**File:** `reflex-os/main/geometry_dot.c`

#### Milestone 6: Multi-Neuron Layer Evaluation (6/6 tests)

Full **neural network layer** evaluation. CPU pre-multiplies W[i] * X[i] (ternary multiply = sign flip), encodes products into DMA buffers, hardware sums via PCNT. Ternary activation: sign(dot) → {-1, 0, +1}. Verified a **2-layer feedforward network** (8→4 neurons) end-to-end against CPU reference.

```
Throughput (32 neurons, dim=256):
  425 neurons/s | 108.8K trit-MACs/s | 1525 us/neuron (hardware)
```

**File:** `reflex-os/main/geometry_layer.c`

#### Milestone 7: Ternary CfC (6/6 tests)

The first **fully-ternary CfC** (Closed-form Continuous-time) liquid neural network. Everything is {-1, 0, +1}: weights, inputs, hidden state, activations. Three blend modes: UPDATE (f=+1), HOLD (f=0), INVERT (f=-1). The inversion mode is unique — it creates natural inhibition and oscillatory dynamics that binary CfC cannot express.

**Key finding:** Under constant input, the ternary CfC resists convergence (still evolving at step 15), unlike binary CfC which converges to a fixed point quickly. The sustained high-energy, uncommitted state resembles biological pluripotency — the stem cell analogy.

```
CfC update: h_new = (f==0) ? h_old : f * g    (ternary multiply)
Performance: 6.7 Hz at 1MHz PARLIO, ~67 Hz projected at 10MHz
```

**File:** `reflex-os/main/geometry_cfc.c`

### The Signal Path (Current: Milestone 7)

```
┌─────────────────────────────────────────────────────────────────┐
│              GEOMETRY INTERSECTION ENGINE                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│   CPU pre-multiplies P[i] = W[i] × X[i] (ternary, ~200 cycles) │
│   Encodes P into DMA buffers (1 dibit = 1 trit)                 │
│                                                                  │
│   SRAM buffers ──► GDMA ──► PARLIO TX (2-bit, 1MHz)             │
│   (descriptor chain)              │                              │
│                                   ▼                              │
│                            GPIO 4 (X_pos)                        │
│                            GPIO 5 (X_neg)                        │
│                                   │                              │
│   GPIO 6 (Y_pos) = HIGH ─────────┤                              │
│   GPIO 7 (Y_neg) = LOW  ─────────┤                              │
│                                   ▼                              │
│                          PCNT Unit 0 (agree)                     │
│                          PCNT Unit 1 (disagree)                  │
│                                   │                              │
│                          dot = agree - disagree                  │
│                          sign(dot) → {-1, 0, +1}                │
│                                   │                              │
│                          GPIO 8 (positive) ──► next layer input  │
│                          GPIO 9 (negative)                       │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Pattern Encoding (Milestone 5-6: Zero-Interleave)

PARLIO 2-bit mode on GPIO 4 (X_pos) and GPIO 5 (X_neg):

```
Trit +1 → dibit 01 (X_pos rises), then dibit 00 (silence)
Trit -1 → dibit 10 (X_neg rises), then dibit 00 (silence)
Trit  0 → dibit 00, dibit 00 (silence)
```

Each trit occupies 2 dibit slots = 4 bits = half byte. Each byte encodes 2 trits. 64 bytes = 128 trits per DMA buffer. Descriptor chains link buffers for larger vectors (256 trits = 2 buffers).

The zero-interleave guarantees exactly one clean rising edge per non-zero trit, eliminating the nibble-boundary glitch problem from Milestones 1-3.

### Key Insight

> *"We found computation hiding in the peripheral fabric."*
>
> PCNT was designed to count encoder pulses. PARLIO was designed for LCD interfaces. GDMA was designed for data transfer. ETM was designed to reduce interrupt latency.
>
> Together, they form a geometry intersection engine that computes ternary dot products — the fundamental operation of neural network inference — while the CPU does almost nothing.

### Files

```
reflex-os/main/
├── geometry_cfc.c         # M7: Ternary CfC liquid network (6/6 verified, current)
├── geometry_layer.c       # M6: Multi-neuron layer (6/6 verified)
├── geometry_dot.c         # M5: 256-trit dot product (10/10 verified)
├── ternary_alu.c          # M4: Ternary TMUL (9/9 verified)
├── autonomous_alu.c       # M3: Autonomous ALU (9/9 verified)
├── alu_fabric.c           # M1: Sub-CPU ALU (59/59 verified)
└── raid_etm_fabric.c      # M2: Autonomous fabric (5/5 verified)

reflex-os/docs/
├── HARDWARE_ERRATA.md     # 20+ errata discovered during development
├── REGISTER_REFERENCE.md  # Correct bare-metal register addresses
└── FLASH_GUIDE.md         # Build, flash, and serial capture procedure
```

---

## The Reflex Becomes the ESP32-C6

The Reflex isn't just for high-end systems. On the ESP32-C6, **The Reflex IS the operating system**:

| Primitive | Latency | Condition |
|-----------|---------|-----------|
| `gpio_write()` | **12 ns** | Direct register, 2 cycles |
| Pure decision | **12 ns** | Threshold + GPIO only |
| Full reflex | **87 ns** | Ideal (interrupts off) |
| Full reflex | **187 ns** | Realistic (interrupts on) |
| `spline_read()` | **137 ns** | Catmull-Rom interpolation |
| With channel | **437 ns** | Cross-core coordination |

### Key Innovations

- **Spline Channels**: Catmull-Rom interpolation bridges discrete signals to continuous trajectories
- **Entropy Field**: The void between shapes carries information—computation IS entropy flow
- **TriX echips**: Soft chips built from shapes in an entropy field substrate
- **Self-Composing Intelligence**: 4,096 shapes + 16,384 routes + Hebbian learning = circuits that grow themselves
- **Stereo Vision**: OBSBOT PTZ cameras driven by entropy attention at 121µs latency

See [`reflex-os/`](reflex-os/) for full documentation.

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
- [`docs/PRIMORDIAL_STILLNESS.md`](docs/PRIMORDIAL_STILLNESS.md) - Stillness as computational substrate
- [`docs/PHILOSOPHY.md`](docs/PHILOSOPHY.md) - Philosophical speculation (clearly marked)
- [`docs/DELTA_STILLNESS_BRIDGE.md`](docs/DELTA_STILLNESS_BRIDGE.md) - The unified architecture
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

*12ns on a $5 chip. 309ns on a $2000 Jetson. Same code. Same topology. Falsified under adversarial testing.*
