# Reflect: Architecture

## Core Insight

**Fast path + slow path = complete system.**

We're not replacing ROS2. We're completing it. ROS2 handles what it's good at (coordination, ecosystem). Reflex handles what ROS2 can't (speed, reflexes).

The fork-and-merge architecture makes this visual and intuitive.

---

## Resolved Tensions

### Node 8 (Conflict Resolution)
**Resolution:** Clear hierarchy:
1. **Safety:** Reflex always wins. If Reflex says STOP, stop.
2. **Control:** Configurable. Usually Reflex for inner loop, ROS2 for setpoints.
3. **Coordination:** ROS2 wins. Reflex doesn't do planning.

Conflicts are rare because the systems handle different concerns.

### Node 9 (Cross-System Debugging)
**Resolution:** Unified observability:
- Correlation IDs across systems
- Telemetry bridge to ROS2 ecosystem
- Replay capability for post-mortem
- Shared timeline visualization

Make the two systems look like one for debugging purposes.

### Node 10 (Complexity Gradient)
**Resolution:** Documented progression:
- Pattern A first (always)
- Pattern B after Pattern A is stable
- Pattern C when ready for advanced use

Force customers through the progression. No skipping.

---

## The Three Patterns Detailed

### Pattern A: Safety Override
```
ROS2 Planner ───────────────┐
                            ↓
Sensors ──▶ [REFLEX] ──▶ Safety Gate ──▶ Actuators
              │
              └── Anomaly? → STOP
```
**Deploy this first.** Low risk, high visibility, proves value.
**Customer learns:** Reflex detects anomalies reliably.

### Pattern B: High-Bandwidth Inner Loop
```
ROS2: Setpoint ──────────────────────────┐
                                         ↓
Sensors ──▶ [REFLEX: 10kHz Control] ──▶ Actuators
              │
              └── Telemetry → ROS2
```
**Deploy second.** Requires trust from Pattern A.
**Customer learns:** Reflex can control, not just detect.

### Pattern C: Anomaly Detection + Coordination
```
Sensor A ──▶ Reflexor A ──┐
Sensor B ──▶ Reflexor B ──┼──▶ Coordinator ──▶ ROS2 Alerts
Sensor C ──▶ Reflexor C ──┘
```
**Deploy last.** System-wide awareness.
**Customer learns:** Multiple Reflexors work together.

---

## The Shared Memory Model

```
┌─────────────────────────────────────────────────────┐
│                  SHARED MEMORY                       │
├─────────────────────────────────────────────────────┤
│  Channel 0: Sensor A (Reflex writes, ROS2 reads)   │
│  Channel 1: Sensor B (Reflex writes, ROS2 reads)   │
│  Channel 2: Setpoint (ROS2 writes, Reflex reads)   │
│  Channel 3: Command (Reflex writes, Actuator reads)│
│  Channel 4: Telemetry (Reflex writes, ROS2 reads)  │
│  Channel 5: Anomaly (Reflex writes, ROS2 reads)    │
└─────────────────────────────────────────────────────┘
```

All communication through memory. Zero serialization. Nanosecond access.

---

## What I Now Understand

The architecture is the SELLING POINT for technical buyers. They see:
1. Clean separation of concerns
2. Defined integration points
3. Progressive complexity
4. Unified observability

The patterns provide a roadmap. Customers know where they are and where they're going. This reduces fear of the unknown.

The shared memory model explains WHY it's fast without requiring deep technical knowledge. "Both systems read/write the same memory. No translation needed."
