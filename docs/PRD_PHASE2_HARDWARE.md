# PRD: Phase 2 - From Demo to Hardware

**Codename:** Zero to Hero
**Status:** PLANNING
**Predecessor:** Valentine's Day Demo (Phase 1)

---

## Context

Phase 1 proved the Reflex primitive works: 309ns processing time, event-driven response, ROS2 integration. But a skeptical review identified gaps between "impressive benchmark" and "bulletproof demo."

This PRD addresses those gaps.

---

## Objectives

| # | Objective | Success Metric |
|---|-----------|----------------|
| 1 | End-to-end latency measurement | Sensor→Actuator timestamp delta |
| 2 | Real hardware in the loop | Physical F/T sensor + gripper |
| 3 | Fair A/B comparison | 1kHz ROS2 baseline |
| 4 | Outcome-based proof | Measurable force overshoot difference |
| 5 | Production hardening | RT kernel, fault tolerance |

---

## Non-Objectives (Out of Scope)

- Full impedance control implementation
- Safety certification
- Multi-robot coordination
- Product packaging

---

## The Gaps (from Skeptical Review)

| Gap | Severity | This PRD |
|-----|----------|----------|
| Not E2E latency | Medium | ✅ Fix |
| 1kHz input bottleneck | Medium | ✅ Fix |
| No real hardware | High | ✅ Fix |
| Unfair A/B comparison | Medium | ✅ Fix |
| No RT kernel | Medium | ✅ Fix |
| CPU spin-wait | Low | Document only |
| Trivial control | Medium | Partial |
| No fault tolerance | Medium | ✅ Fix |
| Misleading claims | Medium | ✅ Fix |
| No outcome measurement | High | ✅ Fix |

---

## Hardware Requirements

### Must Have

| Component | Specification | Purpose |
|-----------|---------------|---------|
| F/T Sensor | 1kHz+ sample rate, ROS2 driver | Real force input |
| Gripper | Position-controlled, ROS2 interface | Real actuation |
| Test object | Deformable (foam/fruit) | Outcome measurement |

### Options Evaluated

**Force/Torque Sensors:**
| Option | Rate | Interface | Cost | Notes |
|--------|------|-----------|------|-------|
| ATI Mini45 | 7kHz | EtherCAT/ROS2 | $$$$ | Gold standard |
| OnRobot HEX | 1kHz | USB/ROS2 | $$$ | Good integration |
| Bota SensONE | 1kHz | EtherCAT | $$$ | Compact |
| Robotiq FT300 | 100Hz | USB | $$ | Too slow |

**Grippers:**
| Option | Interface | Force sensing | Notes |
|--------|-----------|---------------|-------|
| Robotiq 2F-85 | ROS2 | Built-in | Common, well-supported |
| OnRobot RG2 | ROS2 | Built-in | Good force control |
| Franka Hand | ROS2 | Excellent | If using Franka arm |

### Recommendation
- **Sensor:** OnRobot HEX (1kHz, good ROS2 support)
- **Gripper:** Robotiq 2F-85 (ubiquitous, proven)
- **Test object:** Soft foam ball + grape (deformation visible)

---

## Architecture

### Phase 1 (Current)
```
[Simulated Sensor] → ROS2 → Bridge → Reflex → Bridge → ROS2 → [Simulated Actuator]
      1kHz                              309ns
```

### Phase 2 (Target)
```
[Real F/T Sensor] → ROS2 → Bridge → Reflex → Bridge → ROS2 → [Real Gripper]
     1kHz+            ↓                         ↓
                  Timestamp                 Timestamp
                      └─────── E2E Latency ───────┘
```

### Key Additions
1. **Timestamping at each stage** - measure real E2E
2. **High-rate sensor driver** - remove 1kHz bottleneck
3. **Real actuator feedback** - close the loop
4. **Outcome measurement** - force overshoot in Newtons

---

## Milestone 1: End-to-End Latency Measurement

### Goal
Measure and report actual sensor-to-actuator latency, not just Reflex processing time.

### Implementation

```cpp
// In bridge_node.cpp - stamp incoming messages
void on_force_received(const WrenchStamped::SharedPtr msg) {
    uint64_t t0 = msg->header.stamp;  // Sensor timestamp
    uint64_t t1 = now_ns();            // Bridge receive time
    
    force_channel_->signal(encode_force(force), t0);  // Pass sensor timestamp
}

// In reflex_force_control.c - measure full pipeline
uint64_t sensor_ts = force_in->timestamp;  // Original sensor time
// ... process ...
reflex_signal(command_out, position, now);
uint64_t e2e_latency = now - sensor_ts;    // True E2E
```

### Deliverable
- New metric: `e2e_latency_ns` (sensor timestamp to command sent)
- Histogram of E2E latency over run
- Report median, P99, max

### Success Criteria
- E2E latency < 1ms for 99% of samples
- Clear breakdown: sensor→bridge→reflex→bridge→actuator

---

## Milestone 2: Fair A/B Comparison

### Goal
Compare against properly-tuned ROS2, not strawman 100Hz.

### Implementation

Add `--ros2-1khz` mode:
```c
#define ROS2_1KHZ_PERIOD_NS  1000000   // 1ms = 1kHz (fair baseline)
```

Also create pure-ROS2 controller (no Reflex):
```cpp
// ros2_only_controller.cpp
class ROS2OnlyController : public Node {
    // 1kHz timer callback
    // Same control logic, no shared memory
    // Measure callback-to-publish latency
}
```

### Deliverable
Three-way comparison:
| Mode | Check Rate | Architecture |
|------|------------|--------------|
| REFLEX | Event-driven | Spin-wait + shm |
| ROS2-1kHz | 1kHz polling | Shm bridge |
| ROS2-native | 1kHz callback | Pure ROS2 |

### Success Criteria
- REFLEX shows measurable benefit over ROS2-1kHz
- Honest reporting of when/why REFLEX wins

---

## Milestone 3: Real Hardware Integration

### Goal
Replace simulated sensor/actuator with real hardware.

### Phase 3a: F/T Sensor

```bash
# Install OnRobot ROS2 driver
cd ~/ros2_ws/src
git clone https://github.com/onrobot/onrobot_ros2.git
colcon build

# Launch sensor
ros2 launch onrobot_hex hex.launch.py
# Publishes: /ft_sensor/wrench at 1kHz
```

Update bridge config:
```yaml
reflex_bridge:
  ros__parameters:
    force_topic: "/ft_sensor/wrench"  # Real sensor
```

### Phase 3b: Gripper

```bash
# Install Robotiq driver
git clone https://github.com/PickNikRobotics/ros2_robotiq_gripper.git
colcon build

# Launch gripper
ros2 launch robotiq_driver robotiq.launch.py
# Subscribes: /gripper/command
```

Update bridge to publish gripper commands:
```cpp
// Map Reflex command to gripper position
auto gripper_msg = robotiq_msgs::msg::GripperCommand();
gripper_msg.position = decode_position(command_channel_->read());
gripper_pub_->publish(gripper_msg);
```

### Deliverable
- Real sensor → Reflex → Real gripper
- Video of physical response to force

### Success Criteria
- Gripper responds to force threshold within measured E2E latency
- No simulation in the critical path

---

## Milestone 4: Outcome-Based Measurement

### Goal
Prove faster detection → better outcomes, not just faster numbers.

### Experiment Design

**Setup:**
1. Gripper holds soft object (foam ball)
2. External force applied (push/impact)
3. Measure force overshoot before grip adjusts

**Metrics:**
| Metric | Definition |
|--------|------------|
| Peak overshoot | Max force - threshold |
| Time to recovery | Time from threshold breach to force < threshold |
| Object deformation | Visual/measured compression |

**Protocol:**
1. Run 20 trials with REFLEX mode
2. Run 20 trials with ROS2-1kHz mode
3. Compare distributions

### Deliverable
- Force overshoot: REFLEX vs ROS2 (with error bars)
- Recovery time: REFLEX vs ROS2
- Video showing visible difference in object handling

### Success Criteria
- Statistically significant difference in overshoot (p < 0.05)
- Visible difference in video

---

## Milestone 5: RT Kernel + Fault Tolerance

### Goal
Production-grade reliability.

### Phase 5a: PREEMPT_RT

```bash
# Install RT kernel on Thor
sudo apt install linux-image-rt-arm64

# Or build from source for JetPack
# https://docs.nvidia.com/jetson/archives/r36.2/DeveloperGuide/SD/Kernel/KernelCustomization.html

# Boot parameters
GRUB_CMDLINE_LINUX="isolcpus=0,1,2 nohz_full=0,1,2 rcu_nocbs=0,1,2"
```

Test under load:
```bash
# Stress other cores while running Reflex
stress-ng --cpu 11 --timeout 60s &
./reflex_force_control --reflex
# Verify latency doesn't degrade
```

### Phase 5b: Fault Tolerance

```c
// Watchdog in bridge_node
void watchdog_callback() {
    uint64_t last_reflex_seq = telemetry_channel_->sequence();
    if (last_reflex_seq == prev_seq_) {
        watchdog_misses_++;
        if (watchdog_misses_ > MAX_MISSES) {
            RCLCPP_ERROR(get_logger(), "Reflex controller unresponsive!");
            trigger_safe_state();
        }
    } else {
        watchdog_misses_ = 0;
    }
    prev_seq_ = last_reflex_seq;
}

void trigger_safe_state() {
    // Command gripper to open
    auto msg = std_msgs::msg::Float64();
    msg.data = 0.0;  // Open
    command_pub_->publish(msg);
}
```

### Deliverable
- Latency under load (stress test results)
- Watchdog triggers safe state on controller failure
- Documented failure modes and responses

### Success Criteria
- P99 latency < 10μs under load with RT kernel
- Safe state reached within 100ms of controller failure

---

## Timeline

| Week | Milestone | Deliverable |
|------|-----------|-------------|
| 1 | M1: E2E Latency | Pipeline timestamps, latency histogram |
| 1 | M2: Fair Comparison | Three-way benchmark |
| 2-3 | M3: Hardware | Sensor + gripper integrated |
| 3-4 | M4: Outcomes | Overshoot comparison, video |
| 4 | M5: Hardening | RT kernel, watchdog |
| 5 | Documentation | Updated claims, honest pitch |

**Total: 5 weeks**

---

## Revised Claims (Post-Phase 2)

**Current (Phase 1, Falsified):**
> "309ns processing time on Thor, 187ns realistic on C6. Event-driven catches ~5% more anomalies than well-tuned 1kHz polling, ~10x more than typical 100Hz systems. Zero catastrophic failures in 100K+ samples."

**Target (Phase 2):**
> "End-to-end force response in under 500μs - 10x faster than standard ROS2 control loops. In physical testing, this reduced force overshoot by 40% when handling delicate objects. Validated on real hardware with RT kernel under load."

---

## Resources Required

| Resource | Need | Status |
|----------|------|--------|
| OnRobot HEX sensor | Purchase/borrow | TBD |
| Robotiq 2F-85 gripper | Purchase/borrow | TBD |
| RT kernel for Thor | Build/install | TBD |
| Test objects | Purchase | ~$20 |
| Engineer time | 5 weeks | Available |

---

## Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Hardware procurement delay | Medium | High | Start with available sensors |
| RT kernel breaks CUDA | Low | High | Test in isolation first |
| No measurable outcome difference | Medium | High | Ensure fast enough sensor |
| E2E latency dominated by ROS2 | Medium | Medium | Bypass ROS2 for sensor if needed |

---

## Exit Criteria

Phase 2 is complete when:

1. ✅ E2E latency measured and reported
2. ✅ Fair three-way comparison published
3. ✅ Real hardware demo video
4. ✅ Statistical proof of outcome improvement
5. ✅ RT kernel + fault tolerance implemented
6. ✅ Revised claims are defensible

---

## The Hero Pitch (Post-Phase 2)

> "We built a force-reactive gripper that responds in 500 microseconds - 10x faster than standard ROS2. When an object slips, The Reflex catches it before damage occurs. In 100 trials, our system reduced peak force overshoot by 40% compared to a well-tuned 1kHz ROS2 controller. This isn't a simulation - watch the grape not get crushed."

---

*Phase 1 proved the primitive. Phase 2 proves the product.*
