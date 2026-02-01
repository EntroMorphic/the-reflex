# reflex_ros_bridge

Bridge between ROS2 topics and Reflex shared memory channels for **sub-microsecond** robotics control.

## Key Result

| Mode | Processing Time | Check Rate | Anomalies Caught |
|------|-----------------|------------|------------------|
| **REFLEX** | **~300 ns** | Event-driven | 1,127 |
| ROS2-1kHz | ~500 ns | 1 kHz (fair) | ~500 |
| ROS2-100Hz | ~500 ns | 100 Hz (typical) | ~113 |

**What this means:** All modes have similar processing speed (~300-500ns). The difference is *when* they check. REFLEX reacts on signal arrival. Polling modes can miss signals between checks.

> "~300 nanoseconds to process. Event-driven means we never miss a signal."

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         THE REFLEX + ROS2                            │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ROS2 Container                    Host (isolated RT cores)         │
│  ┌─────────────────┐              ┌─────────────────────┐          │
│  │                 │              │                     │          │
│  │  Force Sensor   │              │  REFLEX CONTROLLER  │          │
│  │  (1kHz pub)     │              │                     │          │
│  │       │         │              │  Event-driven:      │          │
│  │       ▼         │              │  spin-wait on cache │          │
│  │  bridge_node    │◀── shm ────▶│  line until signal  │          │
│  │       │         │              │                     │          │
│  │       ▼         │              │  432ns reaction     │          │
│  │  Gripper Cmd    │              │                     │          │
│  │                 │              │                     │          │
│  └─────────────────┘              └─────────────────────┘          │
│                                                                      │
│  Shared Memory: /dev/shm/reflex_force (64 bytes, cache-aligned)    │
│                 /dev/shm/reflex_command                             │
│                 /dev/shm/reflex_telemetry                           │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Quick Start

### On Thor (Jetson AGX)

```bash
# Terminal 1: Start ROS2 bridge
docker run -d --rm --runtime nvidia \
  --name reflex_bridge \
  --network host --ipc host \
  -v /dev/shm:/dev/shm \
  -v /home/ztflynn/the-reflex:/workspace/the-reflex \
  dustynv/ros:humble-desktop-l4t-r36.4.0 \
  bash -c "source /opt/ros/humble/install/setup.bash && \
           mkdir -p /tmp/ros_ws/src && \
           cp -r /workspace/the-reflex/reflex_ros_bridge /tmp/ros_ws/src/ && \
           cd /tmp/ros_ws && colcon build --packages-select reflex_ros_bridge && \
           source install/setup.bash && \
           export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/tmp/ros_ws/install/reflex_ros_bridge/lib && \
           ros2 run reflex_ros_bridge bridge_node"

# Terminal 2: Start force simulator
docker exec reflex_bridge bash -c "source /opt/ros/humble/install/setup.bash && \
  source /tmp/ros_ws/install/setup.bash && \
  ros2 run reflex_ros_bridge force_simulator_node"

# Terminal 3: Start Reflex controller
cd /home/ztflynn/the-reflex/reflex_ros_bridge
./reflex_force_control --reflex
```

### Using the Demo Script

```bash
cd scripts/

# Run Reflex mode (432ns reaction time)
./run_demo.sh reflex

# Run ROS2 baseline (10ms reaction time)
./run_demo.sh ros2

# Run A/B comparison
./run_demo.sh compare
```

---

## Components

### bridge_node (ROS2)

Creates shared memory channels and bridges ROS2 topics:
- `/force_sensor` (WrenchStamped) → `reflex_force` channel
- `reflex_command` channel → `/gripper_command` (Float64)
- Publishes `/reflex/anomaly` and `/reflex/latency_ns`

### force_simulator_node (ROS2)

Generates realistic 14-second grasp profile:
- **APPROACH** (0-2s): No force
- **CONTACT** (2-4s): Force ramps to target
- **GRASP** (4-8s): Steady at 2N
- **ANOMALY** (8-9s): Spike to 7N (exceeds 5N threshold)
- **RECOVERY** (9-10s): Return to safe level
- **STABLE** (10-12s): Back at target
- **RELEASE** (12-14s): Force decreases

### reflex_force_control (Native C)

Event-driven force controller:

```bash
./reflex_force_control --reflex   # 432ns reaction (spin-wait)
./reflex_force_control --ros2     # 10ms reaction (polling baseline)
```

**How it works:**
1. Spin-waits on cache line (`reflex_wait`)
2. Cache coherency wakes us when signal arrives
3. Process force, check threshold, send command
4. Total reaction time: **432 ns average**

---

## A/B Comparison Results

Measured January 31, 2026 on Jetson AGX Thor:

```
╔═══════════════════════════════════════════════════════════════╗
║       FORCE CONTROL: STATISTICS (REFLEX MODE)                 ║
╚═══════════════════════════════════════════════════════════════╝
  Mode:           REFLEX
  Loop count:     15,992
  Anomaly count:  1,127
  
  Reaction time:  432 ns average, 1,037 ns max
  
  ✓ Responds within nanoseconds of threshold breach
```

```
╔═══════════════════════════════════════════════════════════════╗
║       FORCE CONTROL: STATISTICS (ROS2 MODE)                   ║
╚═══════════════════════════════════════════════════════════════╝
  Mode:           ROS2
  Loop count:     1,591
  Anomaly count:  113
  
  Reaction time:  Up to 10 ms worst case
  
  ✗ May miss threshold breach for up to 10 ms
```

---

## File Structure

```
reflex_ros_bridge/
├── CMakeLists.txt
├── package.xml
├── README.md                      # This file
├── reflex_force_control.c         # Native controller (A/B modes)
├── include/reflex_ros_bridge/
│   └── channel.hpp                # SharedChannel C++ class
├── src/
│   ├── channel.cpp                # Shared memory implementation
│   ├── bridge_node.cpp            # ROS2 ↔ shm bridge
│   ├── force_simulator_node.cpp   # Grasp profile generator
│   ├── telemetry_node.cpp         # Stats publisher
│   └── main.cpp
├── launch/
│   └── bridge.launch.py
├── config/
│   └── bridge_config.yaml
├── scripts/
│   ├── run_demo.sh                # Demo launcher
│   └── telemetry_dashboard.py     # Rerun visualization
└── demo/
    └── force_demo.py              # Python demo (alternative)
```

---

## Build

### ROS2 Package (inside container)

```bash
source /opt/ros/humble/install/setup.bash
mkdir -p /tmp/ros_ws/src
cp -r reflex_ros_bridge /tmp/ros_ws/src/
cd /tmp/ros_ws
colcon build --packages-select reflex_ros_bridge
source install/setup.bash
```

### Native Controller (on host)

```bash
gcc -O3 -Wall -o reflex_force_control reflex_force_control.c -lrt -lm
```

---

## The Core Primitive

The Reflex uses cache coherency for signaling:

```c
// Producer signals by incrementing sequence
static inline void reflex_signal(reflex_channel_t* ch, uint64_t value) {
    ch->value = value;
    ch->timestamp = get_time_ns();
    __atomic_fetch_add(&ch->sequence, 1, __ATOMIC_SEQ_CST);
    __asm__ volatile("dmb sy" ::: "memory");  // ARM barrier
}

// Consumer spins until sequence changes
static inline uint64_t reflex_wait(reflex_channel_t* ch, uint64_t last_seq) {
    uint64_t seq;
    while ((seq = __atomic_load_n(&ch->sequence, __ATOMIC_ACQUIRE)) == last_seq) {
        __asm__ volatile("" ::: "memory");
    }
    return seq;
}
```

No syscalls. No locks. No polling. The hardware wakes us up.

---

## License

MIT

---

*432 nanoseconds. The hardware is already doing the work. We're just using it.*
