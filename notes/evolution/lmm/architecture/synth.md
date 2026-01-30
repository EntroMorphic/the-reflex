# Synthesis: Architecture

## The Mental Model

**Fast path + slow path = complete system.**

```
                    ┌─────────────────────┐
                    │      ROS2 LAYER     │
                    │  Planning, Mapping  │
                    └──────────┬──────────┘
                               │
              ┌────────────────┴────────────────┐
              │                                  │
              ▼                                  ▼
    ┌─────────────────┐                ┌─────────────────┐
    │   SLOW PATH     │                │   FAST PATH     │
    │   (Planner)     │                │   (Reflex)      │
    │   ~10-100 ms    │                │   ~926 ns       │
    └────────┬────────┘                └────────┬────────┘
              │                                  │
              └────────────────┬────────────────┘
                               ▼
                    ┌─────────────────┐
                    │    ACTUATORS    │
                    └─────────────────┘
```

**ROS2 for coordination. Reflex for reaction.**

---

## Component Specifications

| Component | Size | Latency | Function |
|-----------|------|---------|----------|
| **Channel** | 64 B | 50 ns | Lock-free signaling |
| **Spline** | 256 B | 137 ns | Continuous interpolation |
| **Reflexor** | 4 KB | 300 ns | Anomaly detection |
| **Entropy Field** | Config | 200 ns | Silence/surprise tracking |

---

## The Three Patterns

### Pattern A: Safety Override (Deploy First)

```
Sensors ──┬──▶ ROS2 Planner ──┐
          │                    ▼
          └──▶ REFLEX ──▶ Safety Gate ──▶ Actuators
                │
                └── Anomaly? → STOP
```

**Use case:** Human-robot collaboration, hazard response
**Risk:** Low
**Value:** Immediate safety improvement

### Pattern B: High-Bandwidth Inner Loop (Deploy Second)

```
ROS2: Setpoint ───────────────────────┐
                                      ▼
Sensors ──▶ REFLEX (10kHz control) ──▶ Actuators
              │
              └── Telemetry → ROS2
```

**Use case:** Force control, balance, stabilization
**Risk:** Medium (Reflex controls actuators)
**Value:** Capability impossible without Reflex

### Pattern C: Distributed Anomaly Detection (Deploy Third)

```
Sensor A ──▶ Reflexor A ──┐
Sensor B ──▶ Reflexor B ──┼──▶ Coordinator ──▶ ROS2
Sensor C ──▶ Reflexor C ──┘
```

**Use case:** System-wide awareness, predictive maintenance
**Risk:** Medium (coordination complexity)
**Value:** Holistic system intelligence

---

## Shared Memory Architecture

```
┌─────────────────────────────────────────────────────────┐
│                     SHARED MEMORY                        │
├─────────────────────────────────────────────────────────┤
│  Ch 0: Sensor A        (Reflex writes, ROS2 reads)     │
│  Ch 1: Sensor B        (Reflex writes, ROS2 reads)     │
│  Ch 2: Setpoint        (ROS2 writes, Reflex reads)     │
│  Ch 3: Command         (Reflex writes, Actuator reads) │
│  Ch 4: Telemetry       (Reflex writes, ROS2 reads)     │
│  Ch 5: Anomaly         (Reflex writes, ROS2 reads)     │
└─────────────────────────────────────────────────────────┘
```

**Zero serialization. Nanosecond access. Zero copy.**

---

## Conflict Resolution

| Domain | Winner | Rationale |
|--------|--------|-----------|
| Safety | Reflex | Non-negotiable |
| Inner loop control | Reflex | Speed requirement |
| Setpoints | ROS2 | Planning authority |
| Coordination | ROS2 | Ecosystem integration |

**Clear hierarchy prevents conflicts.**

---

## Debugging Across Systems

| Tool | Purpose |
|------|---------|
| Correlation IDs | Track events across systems |
| Unified timeline | See both systems together |
| Telemetry bridge | ROS2 tools see Reflex data |
| Replay capability | Post-mortem analysis |

**Make two systems look like one for debugging.**

---

## Implementation Sequence

```
1. Deploy Pattern A (Safety Override)
   └── Prove: Reflex detects anomalies reliably
   
2. Expand to Pattern B (Inner Loop)
   └── Prove: Reflex can control, not just detect
   
3. Expand to Pattern C (Distributed)
   └── Prove: Multiple Reflexors coordinate
```

**No skipping. Each pattern builds trust for the next.**
