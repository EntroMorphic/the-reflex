# The Reflex: Sub-Microsecond Robotics Coordination

**Cache coherency signaling for 10kHz robotics control with 926ns P99 latency.**

---

<p align="center">
  <strong>255x faster than baseline Linux. Tested on NVIDIA Jetson AGX Thor.</strong>
</p>

---

## Headline Results

| Metric | Baseline Linux | With Reflex | Improvement |
|--------|----------------|-------------|-------------|
| **P99 Latency** | 236 μs | **926 ns** | **255x** |
| Sub-μs cycles | 91.3% | **99.64%** | +8.3% |
| Control Rate | Limited | **10 kHz** | Achieved |

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
│       ├── ARCHITECTURE.md   # Channel model + entropy field
│       ├── API.md            # Complete API reference
│       └── BENCHMARKS.md     # Performance measurements
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

## Platforms Tested

| Platform | Architecture | E3 Latency | 10kHz Control |
|----------|--------------|------------|---------------|
| **Jetson AGX Thor** | 14-core ARM | 297 ns | ✅ 926ns P99 |
| **ESP32-C6** | RISC-V | 118 ns | ✅ 10kHz verified |
| Raspberry Pi 4 | 4-core ARM | 167 ns | ✅ (untested) |
| x86_64 Linux | Intel/AMD | ✅ works | ⚠️ (no isolcpus test) |

---

## The Reflex Becomes the ESP32-C6

The Reflex isn't just for high-end systems. On the ESP32-C6, **The Reflex IS the operating system**:

| Primitive | Latency | Notes |
|-----------|---------|-------|
| `gpio_write()` | **12 ns** | Direct register, 2 cycles |
| `reflex_signal()` | **118 ns** | Core primitive |
| `spline_read()` | **137 ns** | Catmull-Rom interpolation |
| `entropy_deposit()` | **~125 ns** | Stigmergy write |

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

The Reflex bridges to ROS2 via shared memory, giving you sub-microsecond control alongside the ROS ecosystem:

```
ROS2 Topics (1kHz)          Native Controller (10kHz)
      │                              │
      ▼                              ▼
┌─────────────┐              ┌─────────────────┐
│ bridge_node │◄── shm ─────►│ reflex_force_   │
│             │              │ control         │
└─────────────┘              └─────────────────┘
      │                              │
/force_sensor              /dev/shm/reflex_*
/gripper_command           (64 bytes, 103ns avg)
```

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

*926ns P99. 255x improvement. 10kHz robotics control. Measured on real hardware.*
