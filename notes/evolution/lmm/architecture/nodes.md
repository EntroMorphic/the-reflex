# Nodes: Architecture

## Node 1: Fast Path + Slow Path
Not replacing ROS2. Complementing it.
**Visualization:** Fork in the middle, merge at actuators.

## Node 2: The Stack Layers
ROS2 → Fork → Slow (Planner) / Fast (Reflex) → Actuators.
**Why it matters:** People need to SEE how it fits.

## Node 3: Component Minimalism
Channel, Spline, Reflexor, Entropy Field. Four things.
**Elegance:** Minimal surface area.

## Node 4: Pattern A - Safety Override
Reflex watches in parallel. Anomaly = STOP. Easy first deployment.
**Use case:** Human-robot collaboration.

## Node 5: Pattern B - High-Bandwidth Inner Loop
ROS2 provides setpoints. Reflex does 10kHz control.
**Use case:** Force control, balance.

## Node 6: Pattern C - Anomaly Detection
Reflex watches. Alerts to ROS2. Predictive maintenance.
**Use case:** Fault detection, early warning.

## Node 7: Shared Memory Model
Memory-mapped channels. Zero-copy. Nanosecond access.
**Why it matters:** No serialization overhead.

## Node 8: Conflict Resolution
What if Reflex and ROS2 disagree?
**Answer:** Reflex wins on safety. Configurable otherwise.

## Node 9: Debugging Across Systems
How do you debug when two systems interact?
**Tools:** Unified telemetry, correlation IDs, replay.

## Node 10: The Complexity Gradient
Pattern A (simple) → Pattern B (medium) → Pattern C (advanced).
**Guidance:** Start with A. Graduate to B/C with experience.

## Node 11: Future Patterns
What patterns haven't we discovered yet?
**Opportunity:** Let customers teach us.
