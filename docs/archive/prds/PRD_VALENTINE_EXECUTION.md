# PRD: Valentine's Day Execution Plan

## The Reflex + Isaac ROS on Jetson AGX Thor

**Target Date:** February 14, 2026 (14 days)
**Codename:** Moneyball
**Status:** ACTIVE EXECUTION

---

## The Deliverable

A working demo that shows:
1. ROS2 Humble running on Thor
2. reflex_ros_bridge connecting ROS2 topics to Reflex channels
3. 10kHz control loop with 926ns latency
4. Side-by-side comparison: ROS2-only (slow) vs Reflex-augmented (fast)
5. Rerun visualization streaming telemetry

---

## Hardware

| Component | Specification | Status |
|-----------|---------------|--------|
| Platform | NVIDIA Jetson AGX Thor | ✅ Available |
| GPU | Blackwell | ✅ Confirmed |
| Memory | 128 GB unified LPDDR5X | ✅ Confirmed |
| CUDA | 13.0 | ✅ Confirmed |
| JetPack | R38.2.2 (JetPack 7) | ✅ Confirmed |
| Driver | 580.00 | ✅ Confirmed |
| SSH Access | `ssh -p 11965 ztflynn@10.42.0.2` | ✅ Confirmed |

---

## Software Stack

```
┌─────────────────────────────────────────────────────────────────────┐
│                    VALENTINE DEMO STACK                              │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │    isaac-ros:manipulator-4.0-humble-desktop CONTAINER       │   │
│  │                                                              │   │
│  │    • ROS2 Humble Desktop                                    │   │
│  │    • Isaac ROS 4.0                                          │   │
│  │    • cuMotion (manipulation)                                │   │
│  │    • NITROS (GPU messaging)                                 │   │
│  │                                                              │   │
│  │    + reflex_ros_bridge (we build)                           │   │
│  │    + Rerun SDK (from entromorphic-dev-viz)                  │   │
│  │                                                              │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                 SHARED MEMORY CHANNELS                       │   │
│  │                                                              │   │
│  │    force_in (ROS2 → Reflex)                                 │   │
│  │    command_out (Reflex → ROS2)                              │   │
│  │    telemetry (Reflex → Rerun)                               │   │
│  │                                                              │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                 REFLEX CORE (isolated RT cores)              │   │
│  │                                                              │   │
│  │    10kHz control loop                                       │   │
│  │    926ns P99 latency                                        │   │
│  │    Force threshold detection                                │   │
│  │                                                              │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Execution Plan

### Phase 1: Container Build (Days 1-3)

#### Task 1.1: Build Isaac ROS Manipulator Container
```bash
ssh -p 11965 ztflynn@10.42.0.2
cd ~/jetson-containers
./jetson-containers build isaac-ros:manipulator-4.0-humble-desktop
```

**Expected time:** 2-4 hours (pulls pre-built layers)
**Success criteria:** Container runs, `ros2 topic list` works

#### Task 1.2: Verify ROS2 Humble
```bash
./jetson-containers run isaac-ros:manipulator-4.0-humble-desktop \
  ros2 topic list
```

**Success criteria:** Returns `/parameter_events`, `/rosout`

#### Task 1.3: Verify GPU Access in Container
```bash
./jetson-containers run isaac-ros:manipulator-4.0-humble-desktop \
  nvidia-smi
```

**Success criteria:** Shows Thor GPU

---

### Phase 2: reflex_ros_bridge (Days 4-7)

#### Task 2.1: Create Package Structure

```
reflex_ros_bridge/
├── CMakeLists.txt
├── package.xml
├── include/reflex_ros_bridge/
│   ├── channel.hpp           # Shared memory channel wrapper
│   ├── bridge_node.hpp       # Main bridge node
│   └── telemetry.hpp         # Rerun telemetry publisher
├── src/
│   ├── channel.cpp
│   ├── bridge_node.cpp
│   ├── telemetry.cpp
│   └── main.cpp
├── launch/
│   └── bridge.launch.py
├── config/
│   └── bridge_config.yaml
└── test/
    └── test_bridge.cpp
```

#### Task 2.2: Implement Shared Memory Channel

```cpp
// include/reflex_ros_bridge/channel.hpp
#pragma once

#include <cstdint>
#include <atomic>
#include <sys/mman.h>
#include <fcntl.h>

namespace reflex_ros_bridge {

struct alignas(64) ReflexChannel {
    std::atomic<uint64_t> sequence;
    std::atomic<uint64_t> timestamp;
    std::atomic<uint64_t> value;
    char padding[40];
};

class SharedChannel {
public:
    SharedChannel(const std::string& name, bool create = false);
    ~SharedChannel();
    
    void signal(uint64_t value);
    uint64_t wait(uint64_t last_seq);
    uint64_t read() const;
    uint64_t sequence() const;
    
private:
    ReflexChannel* channel_;
    int fd_;
    std::string name_;
};

}  // namespace reflex_ros_bridge
```

#### Task 2.3: Implement Bridge Node

```cpp
// src/bridge_node.cpp
#include "reflex_ros_bridge/bridge_node.hpp"
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64.hpp>
#include <geometry_msgs/msg/wrench_stamped.hpp>

namespace reflex_ros_bridge {

class BridgeNode : public rclcpp::Node {
public:
    BridgeNode() : Node("reflex_bridge") {
        // ROS2 → Reflex: Force sensor
        force_sub_ = create_subscription<geometry_msgs::msg::WrenchStamped>(
            "/force_sensor", 10,
            [this](const geometry_msgs::msg::WrenchStamped::SharedPtr msg) {
                double force = std::sqrt(
                    msg->wrench.force.x * msg->wrench.force.x +
                    msg->wrench.force.y * msg->wrench.force.y +
                    msg->wrench.force.z * msg->wrench.force.z
                );
                force_channel_.signal(encode_force(force));
            });
        
        // Reflex → ROS2: Command output
        command_pub_ = create_publisher<std_msgs::msg::Float64>("/gripper_command", 10);
        
        // Poll Reflex channel at 1kHz (ROS2 side)
        timer_ = create_wall_timer(
            std::chrono::microseconds(1000),
            [this]() {
                uint64_t seq = command_channel_.sequence();
                if (seq != last_command_seq_) {
                    last_command_seq_ = seq;
                    auto msg = std_msgs::msg::Float64();
                    msg.data = decode_command(command_channel_.read());
                    command_pub_->publish(msg);
                }
            });
        
        RCLCPP_INFO(get_logger(), "Reflex bridge initialized");
    }
    
private:
    SharedChannel force_channel_{"reflex_force", false};
    SharedChannel command_channel_{"reflex_command", false};
    
    rclcpp::Subscription<geometry_msgs::msg::WrenchStamped>::SharedPtr force_sub_;
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr command_pub_;
    rclcpp::TimerBase::SharedPtr timer_;
    uint64_t last_command_seq_ = 0;
    
    static uint64_t encode_force(double force) {
        return static_cast<uint64_t>(force * 1000000.0);  // μN precision
    }
    
    static double decode_command(uint64_t value) {
        return static_cast<double>(value) / 1000000.0;
    }
};

}  // namespace reflex_ros_bridge
```

#### Task 2.4: Implement Reflex Core (C)

```c
// reflex_force_control.c
// Runs OUTSIDE container on isolated RT cores

#include "reflex.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <sched.h>

#define FORCE_THRESHOLD 5000000   // 5N in μN
#define TARGET_FORCE    2000000   // 2N in μN
#define KP 100

static reflex_channel_t* force_in;
static reflex_channel_t* command_out;
static reflex_channel_t* telemetry;

void setup_rt(void) {
    struct sched_param param = { .sched_priority = 99 };
    sched_setscheduler(0, SCHED_FIFO, &param);
    mlockall(MCL_CURRENT | MCL_FUTURE);
}

void* open_shared_channel(const char* name) {
    int fd = shm_open(name, O_RDWR, 0666);
    if (fd < 0) {
        fd = shm_open(name, O_CREAT | O_RDWR, 0666);
        ftruncate(fd, sizeof(reflex_channel_t));
    }
    return mmap(NULL, sizeof(reflex_channel_t), 
                PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
}

int main(void) {
    setup_rt();
    
    force_in = open_shared_channel("/reflex_force");
    command_out = open_shared_channel("/reflex_command");
    telemetry = open_shared_channel("/reflex_telemetry");
    
    uint64_t last_seq = 0;
    int64_t position = 500000;  // Start at 0.5 (mid-grip)
    
    printf("Reflex force control started (10kHz)\n");
    
    while (1) {
        // Wait for new force reading
        last_seq = reflex_wait(force_in, last_seq);
        uint64_t now = rdtsc();
        
        int64_t force = (int64_t)reflex_read(force_in);
        
        // REFLEX: Instant threshold response
        if (force > FORCE_THRESHOLD) {
            // STOP - force exceeded
            reflex_signal(command_out, position, now);
            reflex_signal(telemetry, 1, now);  // Anomaly flag
            continue;
        }
        
        // Normal: Proportional control
        int64_t error = TARGET_FORCE - force;
        position += (KP * error) >> 20;
        
        // Clamp
        if (position < 0) position = 0;
        if (position > 1000000) position = 1000000;
        
        reflex_signal(command_out, (uint64_t)position, now);
        reflex_signal(telemetry, 0, now);  // Normal
    }
    
    return 0;
}
```

---

### Phase 3: Demo Application (Days 8-11)

#### Task 3.1: Force Sensor Simulator Node

```python
#!/usr/bin/env python3
# force_simulator.py - Simulates force sensor for demo

import rclpy
from rclpy.node import Node
from geometry_msgs.msg import WrenchStamped
import math
import time

class ForceSimulator(Node):
    def __init__(self):
        super().__init__('force_simulator')
        self.pub = self.create_publisher(WrenchStamped, '/force_sensor', 10)
        self.timer = self.create_timer(0.001, self.publish_force)  # 1kHz
        
        self.t = 0.0
        self.mode = 'approach'  # approach, contact, grasp
        self.contact_time = None
        
    def publish_force(self):
        msg = WrenchStamped()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = 'gripper'
        
        # Simulate force profile
        if self.mode == 'approach':
            # No force during approach
            force = 0.0
            self.t += 0.001
            if self.t > 2.0:
                self.mode = 'contact'
                self.contact_time = time.time()
                
        elif self.mode == 'contact':
            # Force ramps up on contact
            dt = time.time() - self.contact_time
            force = min(10.0, dt * 20.0)  # 20 N/s ramp
            
            if force > 8.0:
                self.mode = 'grasp'
                
        else:  # grasp
            # Steady state with noise
            force = 2.0 + 0.1 * math.sin(time.time() * 10)
        
        msg.wrench.force.z = force
        self.pub.publish(msg)

def main():
    rclpy.init()
    node = ForceSimulator()
    rclpy.spin(node)

if __name__ == '__main__':
    main()
```

#### Task 3.2: Telemetry Dashboard (Rerun)

```python
#!/usr/bin/env python3
# telemetry_dashboard.py - Real-time visualization

import rerun as rr
import numpy as np
import time
from reflex_ros_bridge import SharedChannel  # Python wrapper

def main():
    rr.init("reflex_demo", spawn=True)
    
    force_ch = SharedChannel("/reflex_force", create=False)
    command_ch = SharedChannel("/reflex_command", create=False)
    telemetry_ch = SharedChannel("/reflex_telemetry", create=False)
    
    force_history = []
    command_history = []
    latency_history = []
    
    last_seq = 0
    
    while True:
        seq = telemetry_ch.sequence()
        if seq != last_seq:
            last_seq = seq
            
            force = force_ch.read() / 1000000.0  # Back to N
            command = command_ch.read() / 1000000.0
            anomaly = telemetry_ch.read()
            
            force_history.append(force)
            command_history.append(command)
            
            # Keep last 1000 samples
            if len(force_history) > 1000:
                force_history.pop(0)
                command_history.pop(0)
            
            # Log to Rerun
            rr.log("force/current", rr.Scalar(force))
            rr.log("command/current", rr.Scalar(command))
            rr.log("force/history", rr.SeriesLine(np.array(force_history)))
            rr.log("command/history", rr.SeriesLine(np.array(command_history)))
            
            if anomaly:
                rr.log("status", rr.TextLog("ANOMALY: Force threshold exceeded", level="WARN"))
        
        time.sleep(0.0001)  # 10kHz poll

if __name__ == '__main__':
    main()
```

#### Task 3.3: A/B Comparison Script

```bash
#!/bin/bash
# run_demo.sh

MODE=${1:-reflex}  # 'reflex' or 'ros2'

echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║       THE REFLEX: VALENTINE'S DAY DEMO                        ║"
echo "║       Mode: $MODE                                              ║"
echo "╚═══════════════════════════════════════════════════════════════╝"

if [ "$MODE" == "reflex" ]; then
    echo "Starting Reflex core on isolated cores..."
    sudo taskset -c 0-2 ./reflex_force_control &
    REFLEX_PID=$!
    sleep 1
fi

echo "Starting ROS2 nodes..."
ros2 launch reflex_ros_bridge demo.launch.py mode:=$MODE &

echo "Starting telemetry dashboard..."
python3 telemetry_dashboard.py &

echo "Starting force simulator..."
ros2 run reflex_demo force_simulator

# Cleanup
kill $REFLEX_PID 2>/dev/null
```

---

### Phase 4: Integration & Polish (Days 12-14)

#### Task 4.1: End-to-End Test
- [ ] Container builds successfully
- [ ] reflex_ros_bridge compiles
- [ ] Shared memory channels work
- [ ] Reflex core runs on isolated cores
- [ ] ROS2 nodes communicate
- [ ] Telemetry streams to Rerun
- [ ] A/B comparison shows clear difference

#### Task 4.2: Record Demo Video
- [ ] Screen capture setup
- [ ] Script the demo flow
- [ ] Record Mode A (ROS2 only) - force overshoots
- [ ] Record Mode B (Reflex) - force controlled
- [ ] Side-by-side comparison edit

#### Task 4.3: Documentation
- [ ] README for reflex_ros_bridge
- [ ] Demo run instructions
- [ ] Troubleshooting guide

---

## Success Metrics

| Metric | Target | Measurement |
|--------|--------|-------------|
| Container builds | Yes | Build completes |
| ROS2 bridge works | Yes | Topics flowing |
| Reflex latency | < 1μs P99 | Instrumentation |
| Control rate | 10kHz | Telemetry |
| A/B visible difference | Yes | Video evidence |
| Demo runs without crash | Yes | 5 consecutive runs |

---

## Risk Mitigation

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Container build fails | Medium | High | Fall back to ros:humble-desktop |
| Shared memory issues | Low | High | Use existing reflex.h pattern |
| CUDA version mismatch | Low | Medium | Container handles this |
| Time overrun | Medium | Medium | Cut telemetry dashboard if needed |

---

## Daily Checkpoints

| Day | Date | Checkpoint |
|-----|------|------------|
| 1 | Jan 31 | Container build started |
| 2 | Feb 1 | Container build complete |
| 3 | Feb 2 | ROS2 verified in container |
| 4 | Feb 3 | reflex_ros_bridge structure |
| 5 | Feb 4 | Shared memory working |
| 6 | Feb 5 | Bridge node compiles |
| 7 | Feb 6 | Bridge node runs |
| 8 | Feb 7 | Reflex core integrated |
| 9 | Feb 8 | Force simulator done |
| 10 | Feb 9 | Telemetry dashboard done |
| 11 | Feb 10 | End-to-end working |
| 12 | Feb 11 | A/B comparison working |
| 13 | Feb 12 | Polish and debug |
| 14 | Feb 13 | Record video |
| **15** | **Feb 14** | **SHIP** |

---

## Commands Reference

### Build Container
```bash
ssh -p 11965 ztflynn@10.42.0.2
cd ~/jetson-containers
./jetson-containers build isaac-ros:manipulator-4.0-humble-desktop
```

### Run Container
```bash
./jetson-containers run \
  --volume ~/the-reflex:/workspace/the-reflex \
  --shm-size=8g \
  isaac-ros:manipulator-4.0-humble-desktop
```

### Inside Container: Build reflex_ros_bridge
```bash
cd /workspace/the-reflex/reflex_ros_bridge
colcon build
source install/setup.bash
```

### Outside Container: Run Reflex Core
```bash
cd ~/the-reflex/reflex-robotics
sudo taskset -c 0-2 ./build/reflex_force_control
```

---

## The Pitch (Ready for Feb 14)

"This is ROS2 Humble on a Jetson AGX Thor with a Blackwell GPU.

Watch the force sensor. I'm simulating a gripper closing on an object.

**Mode A: ROS2 only, 100Hz control.**
See how the force overshoots? By the time ROS2 reacts, it's too late.

**Mode B: Reflex augmented, 10kHz control.**
Same scenario. Watch the force. It hits the threshold and stops instantly.

926 nanoseconds. That's the difference between crushing and cradling.

The code is open source. Clone it. Run it. Tell me if I'm wrong."

---

*Ship date: February 14, 2026*
*Codename: Moneyball*
*Status: EXECUTING*
