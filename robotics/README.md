# Reflex Robotics: 10kHz Control Loops on Jetson Thor

**Sub-microsecond coordination for robotics control loops.**

```
Standard ROS2/DDS:  ~100 μs per hop  →  ~1 kHz control
Reflex:             ~300 ns per hop  →  10+ kHz control
```

---

## What This Is

A demonstration that cache coherency can replace DDS for intra-machine robotics coordination, enabling 10x higher control loop frequencies on commodity hardware.

---

## Benchmark Results (Falsified)

| Metric | Value | Condition |
|--------|-------|-----------|
| **Processing time** | **309 ns** | Normal operation |
| Processing time | 366 ns | Under CPU stress (+18%) |
| P99 Latency | 926 ns | With isolcpus + rcu_nocbs |
| Max observed | 1,268 ns | Under stress test |
| Baseline Linux | 236 μs | Stock kernel |
| **Improvement** | **255x** | vs baseline |

All numbers verified under adversarial testing. Zero catastrophic failures in 100K+ samples.

---

## Hardware

| Component | Spec |
|-----------|------|
| Platform | NVIDIA Jetson AGX Thor |
| CPU | 14-core ARM Cortex-A78AE @ 2.6 GHz |
| GPU | NVIDIA Thor (2000 TOPS) |
| RAM | 128 GB unified |
| Tested | January 31, 2026 |

---

## The Demo

```
┌──────────┐    reflex    ┌──────────┐    reflex    ┌──────────┐
│  Sensor  │─────────────▶│ Control  │─────────────▶│ Actuator │
│  (Core 0)│    324 ns    │ (Core 1) │    240 ns    │ (Core 2) │
└──────────┘              └──────────┘              └──────────┘
                                │
                    Total: 620 ns median
```

---

## Quick Start

### Build

```bash
cd reflex-robotics
make all
```

### Run (requires root for SCHED_FIFO)

```bash
# Best results with isolated cores
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

## Configuration for Best Results

### Minimum (Phase 1)
```c
mlockall(MCL_CURRENT | MCL_FUTURE);
sched_setscheduler(0, SCHED_FIFO, &param);
```
**Result:** P99 drops from 236μs to 2.4μs

### Recommended (Phase 4)
```bash
# Boot parameters (add to /boot/extlinux/extlinux.conf)
isolcpus=0,1,2 rcu_nocbs=0,1,2

# Runtime
sudo taskset -c 0-2 ./build/control_loop
```
**Result:** P99 drops to 926ns

---

## Files

```
reflex-robotics/
├── src/
│   ├── reflex.h              # Core coordination primitive (64 bytes)
│   ├── control_loop.c        # 10kHz control loop demo
│   └── latency_benchmark.c   # Coordination latency measurement
├── docs/
│   ├── PHASE_4_RESULTS.md    # 926ns P99 achieved
│   ├── SKEPTICAL_ANALYSIS.md # Rigorous falsification
│   └── THOR_BENCHMARK_RESULTS.md
├── scripts/
│   └── setup_rt_host.sh      # RT configuration
├── Makefile
└── README.md
```

---

## The Core Primitive

```c
// reflex.h - 64-byte cache-aligned channel
typedef struct {
    volatile uint64_t sequence;   // Monotonically increasing
    volatile uint64_t timestamp;  // Producer's timestamp
    volatile uint64_t value;      // Optional payload
    char padding[40];             // Pad to cache line
} __attribute__((aligned(64))) reflex_channel_t;

// Signal: ~100ns
static inline void reflex_signal(reflex_channel_t* ch, uint64_t ts) {
    ch->timestamp = ts;
    ch->sequence++;
    __asm__ volatile("dsb sy" ::: "memory");
}

// Wait: spins until signal arrives
static inline uint64_t reflex_wait(reflex_channel_t* ch, uint64_t last_seq) {
    while (ch->sequence == last_seq) {
        __asm__ volatile("" ::: "memory");
    }
    return ch->sequence;
}
```

---

## Why It's Fast

| Mechanism | Path | Latency |
|-----------|------|---------|
| **Reflex** | Cache write → snoop → invalidate → reload | ~300 ns |
| Futex | User → kernel → scheduler → wake → user | ~9,000 ns |
| DDS | Serialize → network stack → deserialize | ~100,000 ns |

The hardware already maintains cache coherency. We're just using it.

---

## Related Documentation

- [Main README](../README.md) - Project overview
- [Falsification Results](../docs/FALSIFICATION_COMPLETE.md) - Adversarial testing
- [CNS Topology](../docs/CNS_TOPOLOGY.md) - Hardware-agnostic architecture
- [ROS2 Bridge](../reflex_ros_bridge/README.md) - ROS2 integration

---

*309ns processing. 926ns P99. 255x improvement. Measured on real hardware.*
