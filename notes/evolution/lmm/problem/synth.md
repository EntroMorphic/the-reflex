# Synthesis: The Problem

## The Problem Statement

**Robots are too slow to react because robotics middleware was designed for coordination, not reflexes.**

---

## The Latency Stack

| Layer | Typical Latency | Cumulative |
|-------|-----------------|------------|
| Application Logic | 1-10 ms | 1-10 ms |
| ROS2 Node | 100-500 μs | 1.1-10.5 ms |
| DDS Serialization | 50-200 μs | 1.15-10.7 ms |
| Network/IPC | 100-1000 μs | 1.25-11.7 ms |
| DDS Deserialization | 50-200 μs | 1.3-11.9 ms |
| Subscriber Callback | 100-500 μs | 1.4-12.4 ms |
| **Per Hop Total** | **1.4-12.4 ms** | |
| **3-Hop Total** | **4-37 ms** | |

---

## The Capability Gap

| Scenario | Required | Achievable | Gap |
|----------|----------|------------|-----|
| Collision avoidance | < 1 ms | 4-12 ms | 4-12x |
| Force feedback | < 100 μs | 1-3 ms | 10-30x |
| Balance recovery | < 500 μs | 4-12 ms | 8-24x |
| Human-safe interaction | < 50 μs | 1-3 ms | 20-60x |

**These aren't edge cases. These are core robotics requirements.**

---

## Why Current Solutions Fail

### ROS2/DDS
- ✓ Great ecosystem
- ✓ Flexible architecture
- ✗ Too slow for reflexes
- ✗ Non-deterministic latency

### RTOS (VxWorks, QNX)
- ✓ Fast, deterministic
- ✗ No learning capability
- ✗ No ecosystem
- ✗ Expensive licensing

### Custom Real-Time Code
- ✓ Fast, tailored
- ✗ Maintenance burden (two systems)
- ✗ No ecosystem benefits
- ✗ Expert-dependent

---

## The Industry Workaround

**The dirty secret:** High-performance robotics teams maintain two parallel systems.

```
System 1: ROS2
├── Planning
├── Mapping
├── Visualization
└── Coordination

System 2: Custom RT
├── Control loops
├── Safety responses
└── Sensor fusion
```

**This is unsustainable.** Double maintenance. Integration hell. Expertise fragmentation.

---

## The Forcing Function

The problem gets **worse** over time:

- More capable robots → higher speeds
- Higher speeds → tighter timing
- Tighter timing → current architecture can't keep up

Early Reflex adoption = compounding advantage.

---

## The Market Implication

**This is not a niche problem.** Every robot that:
- Moves fast
- Touches things
- Works near humans
- Operates autonomously

...needs sub-millisecond reflexes. The current architecture cannot provide them.

The market isn't "robots that need to be faster." It's "all robots."
