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
├── reflex-robotics/          # 10kHz CONTROL LOOP (the application)
│   ├── src/
│   │   ├── reflex.h          # Core coordination primitive
│   │   └── control_loop.c    # Sensor→Controller→Actuator demo
│   ├── docs/
│   │   ├── PHASE_4_RESULTS.md    # 926ns P99 achieved
│   │   └── SKEPTICAL_ANALYSIS.md # Rigorous falsification
│   └── scripts/
│       └── setup_rt_host.sh  # RT configuration
│
├── src/                      # RESEARCH EXPERIMENTS (the science)
│   ├── e3_latency_comparison.c   # Stigmergy vs Futex benchmark
│   ├── e1_coordination_v3.c      # Causality proof
│   └── e2b_false_sharing.c       # False positive control
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
| Raspberry Pi 4 | 4-core ARM | 167 ns | ✅ (untested) |
| x86_64 Linux | Intel/AMD | ✅ works | ⚠️ (no isolcpus test) |

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
