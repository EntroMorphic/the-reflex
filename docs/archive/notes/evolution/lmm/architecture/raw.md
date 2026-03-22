# Raw Thoughts: Architecture

## Stream of Consciousness

The architecture is "fast path alongside slow path." Not replacing ROS2, complementing it.

The stack visualization matters. People need to SEE how it fits:
- ROS2 layer on top (planning, mapping, UI)
- Fork in the middle: slow path goes to planner, fast path goes to Reflex
- Both paths merge at actuators
- Reflex can override if needed (safety)

Components are deliberately minimal:
- Channel: 64 bytes, the atom of communication
- Spline: continuous interpolation for smooth trajectories
- Reflexor: anomaly detection, the "instinct"
- Entropy Field: tracks silence and surprise (optional, advanced)

Integration patterns capture 80% of use cases:

PATTERN A: Safety Override
Reflex watches in parallel. If anomaly, STOP. ROS2 doesn't need to know until after.
This is the "first do no harm" pattern. Easy to deploy, hard to screw up.

PATTERN B: High-Bandwidth Inner Loop
ROS2 provides setpoints. Reflex does 10kHz control. Best of both worlds.
This is the "augmentation" pattern. ROS2 for planning, Reflex for execution.

PATTERN C: Anomaly Detection
Reflex watches everything. Alerts to ROS2 when something's off.
This is the "early warning" pattern. Predictive maintenance, fault detection.

The shared memory architecture is key. ROS2 and Reflex communicate through memory-mapped channels, not network. Zero-copy. Nanosecond access.

## Questions Arising

- How do we handle conflict between Reflex and ROS2?
- What happens if Reflex misbehaves?
- How do we debug across the two systems?

## First Instincts

Pattern A (Safety Override) should be the default first deployment. Low risk, high visibility, proves the value quickly.

## What Scares Me

Customers trying to do Pattern B (Inner Loop) before they understand Pattern A. Too much complexity too fast.
