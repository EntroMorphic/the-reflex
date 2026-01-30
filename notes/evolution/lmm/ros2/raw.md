# Raw Thoughts: ROS2 Integration

## Stream of Consciousness

ROS2 is the elephant in the room. Most robotics teams use it. If we're not compatible, we're irrelevant.

But ROS2 is also the PROBLEM. It's too slow. So we have this weird dynamic: "We work with ROS2, but we bypass it for the important stuff."

The framing matters: "The Reflex complements ROS2, not replaces it."

What ROS2 does well:
- Distributed coordination
- Message passing
- Ecosystem (simulation, visualization, tools)
- Community (packages, support)
- Abstraction (hardware independence)

What ROS2 does poorly:
- Real-time performance
- Deterministic latency
- High-frequency control
- Nanosecond coordination

The Reflex does what ROS2 doesn't. They're complementary, not competitive.

The bridge package (reflex_ros_bridge) is critical:
- ReflexChannelPublisher: ROS2 → Reflex (setpoints)
- ReflexChannelSubscriber: Reflex → ROS2 (telemetry)
- TelemetryNode: Exposes Reflexor health to ROS2 ecosystem

The shared memory model:
- Channels live in shared memory (mmap)
- ROS2 nodes access via bridge wrappers
- Reflex runtime accesses directly
- No serialization, no copying

Example flow (force-controlled gripper):
1. ROS2 node sets force setpoint via ReflexChannelPublisher
2. This writes to shared memory channel
3. Reflex runtime (on RT core) reads channel, runs 10kHz control
4. If anomaly, Reflex writes to anomaly channel
5. ROS2 bridge picks up anomaly, publishes to ROS2 topic
6. Application handles anomaly (abort grasp, alert operator)

## Questions Arising

- Do we support ROS1? (Probably not, it's dying)
- Which ROS2 distros? (Humble, Iron, Jazzy?)
- What about micro-ROS for embedded?

## First Instincts

Start with ROS2 Humble (LTS). Add others based on demand. Don't over-engineer the bridge.

## What Scares Me

ROS2 politics. The community can be defensive. Need to position as ally, not threat.
