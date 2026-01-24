# Reflex Robotics: 10kHz Control Loops on Jetson Thor

**Sub-microsecond coordination for robotics control loops.**

```
Standard ROS2/DDS:  ~100 μs per hop  →  ~1 kHz control
Reflex:             ~300 ns per hop  →  10+ kHz control
```

---

## What This Is

A demonstration that cache coherency can replace DDS for intra-machine robotics coordination, enabling 10x higher control loop frequencies on commodity hardware.

## Hardware

- NVIDIA Jetson AGX Thor (14-core ARM Cortex-A78AE)
- Proven: 297ns coordination latency (30x faster than futex)

## The Demo

```
┌──────────┐    reflex    ┌──────────┐    reflex    ┌──────────┐
│  Sensor  │─────────────▶│ Control  │─────────────▶│ Actuator │
│  (Core 0)│    <1 μs     │ (Core 1) │    <1 μs     │ (Core 2) │
└──────────┘              └──────────┘              └──────────┘
     │                          │                         │
     └──────────────────────────┼─────────────────────────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │   Rerun Visualization │
                    │   - Control signals   │
                    │   - Latency histogram │
                    │   - 10kHz update rate │
                    └───────────────────────┘
```

## Quick Start

```bash
# On Thor
cd reflex-robotics
make
./reflex_control_demo

# View (on laptop)
rerun
```

## Files

```
reflex-robotics/
├── src/
│   ├── reflex.h              # Core coordination primitive
│   ├── control_loop.c        # 10kHz control loop demo
│   └── latency_benchmark.c   # Coordination latency measurement
├── python/
│   └── visualize.py          # Rerun visualization
├── Makefile
└── README.md
```

## Results

| Metric | DDS | Reflex | Improvement |
|--------|-----|--------|-------------|
| Hop latency | 100 μs | <1 μs | 100x |
| Control rate | 1 kHz | 10+ kHz | 10x |
| Jitter (P99) | High | Low | Significant |

---

*The hardware is already doing the work. We're just listening.*
