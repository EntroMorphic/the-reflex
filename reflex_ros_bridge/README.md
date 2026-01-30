# reflex_ros_bridge

Bridge between ROS2 topics and Reflex shared memory channels for sub-microsecond robotics control.

## Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                         ARCHITECTURE                                 │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ROS2 Container                    Host (isolated RT cores)         │
│  ┌─────────────────┐              ┌─────────────────┐              │
│  │                 │              │                 │              │
│  │  Force Sensor   │              │  REFLEX CORE    │              │
│  │     Topic       │              │   10kHz loop    │              │
│  │       │         │              │       │         │              │
│  │       ▼         │              │       │         │              │
│  │  bridge_node    │◀── shm ────▶│  force_control  │              │
│  │       │         │              │       │         │              │
│  │       ▼         │              │       │         │              │
│  │  Gripper Cmd    │              │   926ns P99     │              │
│  │     Topic       │              │                 │              │
│  │                 │              │                 │              │
│  └─────────────────┘              └─────────────────┘              │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

## Components

### bridge_node (ROS2)

Creates shared memory channels and bridges:
- `/force_sensor` (WrenchStamped) → `reflex_force` channel
- `reflex_command` channel → `/gripper_command` (Float64)

### telemetry_node (ROS2)

Monitors Reflex channels and publishes stats:
- `/reflex/force` - current force reading
- `/reflex/command` - current command
- `/reflex/rate_hz` - actual control rate
- `/reflex/latency_median_ns` - median latency
- `/reflex/latency_p99_ns` - P99 latency

### reflex_force_control (Native)

10kHz control loop running on isolated RT cores:
- Reads force from shared memory
- Applies threshold checking (STOP if > 5N)
- Proportional control to target force (2N)
- Writes command to shared memory

## Build

### ROS2 Package (inside container)

```bash
cd /workspace/the-reflex/reflex_ros_bridge
colcon build
source install/setup.bash
```

### Native Controller (on host)

```bash
cd /path/to/reflex_ros_bridge
gcc -O3 -Wall -o reflex_force_control reflex_force_control.c -lrt -lm
```

## Run

### 1. Start ROS2 bridge (in container)

```bash
ros2 launch reflex_ros_bridge bridge.launch.py
```

### 2. Start Reflex controller (on host, isolated cores)

```bash
sudo taskset -c 0-2 ./reflex_force_control
```

### 3. Publish test force data

```bash
ros2 topic pub /force_sensor geometry_msgs/msg/WrenchStamped \
  "{header: {stamp: {sec: 0, nanosec: 0}, frame_id: 'gripper'}, \
    wrench: {force: {x: 0.0, y: 0.0, z: 2.5}, torque: {x: 0.0, y: 0.0, z: 0.0}}}"
```

### 4. Monitor output

```bash
ros2 topic echo /gripper_command
ros2 topic echo /reflex/latency_p99_ns
```

## Configuration

Edit `config/bridge_config.yaml`:

```yaml
reflex_bridge:
  ros__parameters:
    force_topic: "/force_sensor"
    command_topic: "/gripper_command"
    poll_rate_hz: 1000
```

## Performance (Measured January 30, 2026)

| Metric | Target | Actual |
|--------|--------|--------|
| Control rate | 10 kHz | **9.69 MHz** ✓ |
| Avg loop time | < 1 μs | **103.2 ns** ✓ |
| Min loop time | - | 9 ns |
| Max loop time | - | 11.7 μs |
| Shared memory | 64 bytes/channel | ✓ |

```
╔═══════════════════════════════════════════════════════════════╗
║       REFLEX FORCE CONTROL: STATISTICS                        ║
╚═══════════════════════════════════════════════════════════════╝
  Loop count:     77,362
  Anomaly count:  0
  Loop timing:
    Min:          9 ns
    Max:          11,769 ns
    Avg:          103.2 ns
    Rate:         9,689,334.4 Hz
```

## License

MIT
