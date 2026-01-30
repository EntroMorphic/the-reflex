# PRD: Valentine's Day Demo

## Isaac Sim + The Reflex on Jetson AGX Thor

**Target Date:** February 14, 2026
**Codename:** Moneyball

---

## The One-Liner

**"Here's a gripper crushing an egg at 100Hz. Here's the same gripper with The Reflex at 10kHz. Watch the difference."**

---

## Why This Demo

| Factor | Without This | With This |
|--------|--------------|-----------|
| Credibility | "Nice benchmarks" | "Holy shit, it works" |
| Sales conversation | Explaining numbers | Showing video |
| Technical validation | Self-reported | Physics-validated |
| Target audience | Skeptics | Believers |

The 926ns number is abstract. A gripper NOT crushing an egg is visceral.

---

## Success Criteria

| Criterion | Target | Measurement |
|-----------|--------|-------------|
| Demo runs | End-to-end without crash | Visual |
| Control rate | 10kHz verified | Telemetry |
| Latency | < 1μs sensor-to-actuator | Instrumentation |
| A/B comparison | Visible difference | Video recording |
| ROS2 bridge | Telemetry flowing | Topic echo |
| Reproducible | Someone else can run it | Documentation |

---

## The Demo Scenario

### Scenario: Force-Controlled Grasp

```
PHASE 1: WITHOUT REFLEX (100Hz ROS2 control)
├── Robot arm approaches object
├── Gripper closes
├── Contact detected (too late)
├── Force exceeds threshold
└── Object deforms/crushes

PHASE 2: WITH REFLEX (10kHz control)
├── Robot arm approaches object
├── Gripper closes
├── Contact detected (sub-ms)
├── Force limited instantly
└── Object held gently, intact
```

### The Objects

| Object | Fragility | Visual Impact |
|--------|-----------|---------------|
| Egg | High | Cracks/doesn't crack |
| Grape | Medium | Squish/no squish |
| Foam cube | Low | Deformation visible |
| Force sensor | N/A | Numerical comparison |

**Recommendation:** Start with foam cube (forgiving), graduate to grape, hero shot is egg.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                      JETSON AGX THOR                                 │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                      ISAAC SIM                               │   │
│  │                                                              │   │
│  │   ┌──────────────┐              ┌──────────────┐            │   │
│  │   │  Robot Arm   │              │   Gripper    │            │   │
│  │   │  (UR10/Franka)│              │  (2-finger)  │            │   │
│  │   └──────┬───────┘              └──────┬───────┘            │   │
│  │          │                             │                     │   │
│  │          │    ┌──────────────┐         │                     │   │
│  │          └───▶│ Force Sensor │◀────────┘                     │   │
│  │               │  (simulated) │                               │   │
│  │               └──────┬───────┘                               │   │
│  │                      │                                       │   │
│  └──────────────────────┼───────────────────────────────────────┘   │
│                         │                                            │
│                         ▼                                            │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │                   ROS2 HUMBLE                                 │   │
│  │                                                               │   │
│  │   /force_sensor ──────┐      ┌────── /gripper_cmd            │   │
│  │   (1kHz from sim)     │      │       (to sim)                │   │
│  │                       │      │                                │   │
│  └───────────────────────┼──────┼────────────────────────────────┘   │
│                          │      │                                    │
│           ┌──────────────┴──────┴──────────────┐                    │
│           │                                     │                    │
│           ▼                                     ▼                    │
│  ┌─────────────────┐                   ┌─────────────────┐          │
│  │  reflex_ros     │                   │  REFLEX CORE    │          │
│  │    _bridge      │◀─────────────────▶│  (isolated RT   │          │
│  │                 │   shared memory   │   cores 0-2)    │          │
│  └─────────────────┘                   └─────────────────┘          │
│                                                │                     │
│                                                ▼                     │
│                                        ┌─────────────┐              │
│                                        │  Reflexor   │              │
│                                        │  (force)    │              │
│                                        │  926ns P99  │              │
│                                        └─────────────┘              │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Components

### 1. Isaac Sim Scene

**File:** `isaac_sim/force_grasp_demo.usd`

| Element | Specification |
|---------|---------------|
| Robot | Franka Panda or UR10 (both included in Isaac) |
| Gripper | Parallel jaw, 2-finger |
| Object | Deformable sphere/cube (soft body physics) |
| Force sensor | Contact sensor on gripper fingers |
| Camera | Fixed view for recording |

### 2. ROS2 Nodes

**Package:** `reflex_demo`

```
reflex_demo/
├── launch/
│   └── valentine_demo.launch.py
├── src/
│   ├── force_bridge_node.cpp      # Isaac force → Reflex channel
│   ├── gripper_bridge_node.cpp    # Reflex channel → Isaac gripper
│   ├── demo_controller_node.py    # Orchestrates the demo
│   └── telemetry_node.cpp         # Publishes Reflex stats to ROS2
├── config/
│   ├── reflex_config.yaml         # Channel mappings
│   └── demo_params.yaml           # Thresholds, timing
├── CMakeLists.txt
└── package.xml
```

### 3. Reflex Core

**File:** `reflex-robotics/src/force_reflex.c`

```c
// Runs on isolated cores 0-2
// Configured via setup_rt_host.sh

reflex_channel_t force_in;      // From Isaac via bridge
reflex_channel_t gripper_out;   // To Isaac via bridge
reflex_channel_t telemetry;     // To ROS2 for visualization

#define FORCE_THRESHOLD 5.0     // Newtons
#define GRASP_FORCE 2.0         // Gentle grasp target

void force_reflex_loop(void) {
    uint64_t last_seq = 0;
    
    while (running) {
        // Wait for force reading
        last_seq = reflex_wait(&force_in, last_seq);
        uint64_t now = rdtsc();
        
        double force = decode_force(force_in.value);
        
        // REFLEX: If force exceeds threshold, stop immediately
        if (force > FORCE_THRESHOLD) {
            reflex_signal(&gripper_out, GRIPPER_HOLD, now);
            reflex_signal(&telemetry, ANOMALY_FORCE, now);
            continue;
        }
        
        // Normal: Proportional force control
        double error = GRASP_FORCE - force;
        double cmd = gripper_position + (KP * error);
        cmd = clamp(cmd, 0.0, 1.0);
        
        reflex_signal(&gripper_out, encode_position(cmd), now);
        reflex_signal(&telemetry, encode_telemetry(force, cmd), now);
        
        // Loop time: ~620ns
    }
}
```

### 4. reflex_ros_bridge

**Package:** `reflex_ros_bridge`

The actual bridge that connects ROS2 topics to Reflex channels via shared memory.

```cpp
// force_bridge_node.cpp
class ForceBridgeNode : public rclcpp::Node {
public:
    ForceBridgeNode() : Node("force_bridge") {
        // Subscribe to Isaac Sim force sensor
        force_sub_ = create_subscription<geometry_msgs::msg::WrenchStamped>(
            "/gripper/force_sensor", 10,
            [this](const geometry_msgs::msg::WrenchStamped::SharedPtr msg) {
                // Write to shared memory channel
                double force_magnitude = std::sqrt(
                    msg->wrench.force.x * msg->wrench.force.x +
                    msg->wrench.force.y * msg->wrench.force.y +
                    msg->wrench.force.z * msg->wrench.force.z
                );
                reflex_signal(&force_channel_, encode_force(force_magnitude));
            });
        
        // Publish gripper commands from Reflex
        gripper_pub_ = create_publisher<std_msgs::msg::Float64>(
            "/gripper/command", 10);
        
        // Timer to read gripper channel and publish
        timer_ = create_wall_timer(100us, [this]() {
            if (gripper_channel_.sequence != last_gripper_seq_) {
                last_gripper_seq_ = gripper_channel_.sequence;
                auto msg = std_msgs::msg::Float64();
                msg.data = decode_position(gripper_channel_.value);
                gripper_pub_->publish(msg);
            }
        });
    }
    
private:
    reflex_channel_t force_channel_;
    reflex_channel_t gripper_channel_;
    // ...
};
```

---

## A/B Comparison Mode

### Mode A: ROS2-Only (Baseline)

```python
# demo_controller_node.py - Mode A
class BaselineController(Node):
    def __init__(self):
        # Standard ROS2 control at ~100Hz
        self.force_sub = self.create_subscription(
            WrenchStamped, '/gripper/force_sensor', 
            self.force_callback, 10)
        self.gripper_pub = self.create_publisher(
            Float64, '/gripper/command', 10)
        self.timer = self.create_timer(0.01, self.control_loop)  # 100Hz
    
    def control_loop(self):
        if self.current_force > THRESHOLD:
            self.gripper_pub.publish(Float64(data=HOLD))
        else:
            # Simple proportional control
            cmd = self.position + KP * (TARGET - self.current_force)
            self.gripper_pub.publish(Float64(data=cmd))
```

**Result:** Force overshoots. Object deforms/breaks.

### Mode B: Reflex-Augmented

```
Same scene, same object, same approach trajectory.
Reflex intercepts force at 10kHz.
Force never exceeds threshold.
Object intact.
```

**Result:** Clean grasp. No damage.

---

## Recording & Visualization

### Live Visualization

```
┌─────────────────────────────────────────────────────────────┐
│                    DEMO DASHBOARD                            │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────────┐    ┌─────────────────────┐        │
│  │                     │    │   FORCE (N)         │        │
│  │    ISAAC SIM        │    │   ┌────────────┐    │        │
│  │    CAMERA VIEW      │    │   │████████    │ 5N │        │
│  │                     │    │   └────────────┘    │        │
│  │   [robot grasping]  │    │   Current: 2.3N    │        │
│  │                     │    │   Threshold: 5.0N   │        │
│  └─────────────────────┘    └─────────────────────┘        │
│                                                              │
│  ┌─────────────────────────────────────────────────┐       │
│  │  LATENCY (ns)              CONTROL RATE (Hz)    │       │
│  │  ┌─────────────────┐       ┌─────────────────┐  │       │
│  │  │ P99: 926        │       │ 10,247 Hz       │  │       │
│  │  │ Median: 620     │       │ ████████████    │  │       │
│  │  └─────────────────┘       └─────────────────┘  │       │
│  └─────────────────────────────────────────────────┘       │
│                                                              │
│  MODE: [A] ROS2 Only    [B] REFLEX AUGMENTED              │
│                         ▲▲▲ ACTIVE ▲▲▲                      │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### Video Recording

| Asset | Purpose | Format |
|-------|---------|--------|
| Side-by-side | A vs B comparison | MP4, 1080p |
| Slow motion | Show the moment of contact | MP4, 240fps |
| Telemetry overlay | Numbers on screen | MP4 with burned-in text |
| Raw telemetry | For skeptics | CSV, rosbag |

---

## Timeline

### Week 1 (Jan 30 - Feb 5)

| Day | Task | Deliverable |
|-----|------|-------------|
| Thu-Fri | Isaac Sim scene setup | Robot + gripper + object + force sensor |
| Sat-Sun | ROS2 bridge baseline | Force topic → gripper topic at 100Hz |
| Mon | Verify baseline works | Object crushes reliably |
| Tue-Wed | reflex_ros_bridge skeleton | Shared memory + basic pub/sub |

### Week 2 (Feb 6 - Feb 13)

| Day | Task | Deliverable |
|-----|------|-------------|
| Thu-Fri | Reflex force controller | 10kHz loop running on Thor |
| Sat-Sun | Integration | Isaac ↔ ROS2 ↔ Reflex ↔ ROS2 ↔ Isaac |
| Mon | A/B comparison working | Mode switch, visible difference |
| Tue | Recording setup | Dashboard, video capture |
| Wed | Polish & practice | Smooth demo, no hiccups |
| Thu (14th) | **DEMO DAY** | Record final video |

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Isaac Sim performance | Pre-test scene complexity, reduce if needed |
| ROS2 bridge latency | Shared memory, not topics for hot path |
| Force sensor noise | Low-pass filter in Reflex, not ROS2 |
| Demo day crashes | Practice run Feb 13, backup recording |
| Object physics unrealistic | Tune soft body params, or use real force numbers |

---

## Hardware Checklist

| Item | Status | Notes |
|------|--------|-------|
| Jetson AGX Thor | ✓ Available | Primary platform |
| Isaac Sim license | ? | Verify active |
| ROS2 Humble | ? | Verify installed on Thor |
| isolcpus configured | ? | From Phase 4 work |
| Display for recording | ? | Need 1080p+ output |
| Screen recording software | ? | OBS or similar |

---

## Definition of Done

**The demo is complete when:**

1. [ ] Isaac Sim scene loads with robot, gripper, deformable object
2. [ ] Mode A (ROS2 only) shows object deformation/damage
3. [ ] Mode B (Reflex) shows clean grasp, object intact
4. [ ] Live dashboard shows latency, control rate, force
5. [ ] Telemetry confirms 10kHz rate, < 1μs latency
6. [ ] Video recorded: side-by-side comparison
7. [ ] Video recorded: slow motion contact moment
8. [ ] README documents how to reproduce
9. [ ] Someone else has run it successfully (external validation)

---

## The Pitch (Post-Demo)

"This is a Franka Panda in Isaac Sim on a Jetson AGX Thor.

Watch what happens at 100Hz ROS2 control. [Object crushes]

Same robot. Same object. Same approach. Now with The Reflex at 10kHz. [Object intact]

926 nanoseconds. The difference between crushing and cradling.

Clone the repo. Run it yourself. Tell me if I'm wrong."

---

## Post-Demo: What Happens Next

| If Demo Works | Action |
|---------------|--------|
| Video posted | Share on robotics forums, ROS Discourse |
| Code released | reflex_demo package open sourced |
| Outreach begins | Contact "approachable robotics firms" |
| Pilots discussed | Those who respond get demos |

| If Demo Fails | Action |
|---------------|--------|
| Identify failure mode | Debug, document |
| Reduce scope | Simpler object, longer timeline |
| Pivot to C6 motor | Fall back to hardware demo |

---

## Appendix: Isaac Sim Resources

- [Isaac Sim Documentation](https://docs.omniverse.nvidia.com/isaacsim/latest/)
- [ROS2 Bridge for Isaac](https://docs.omniverse.nvidia.com/isaacsim/latest/installation/install_ros.html)
- [Franka Panda in Isaac](https://docs.omniverse.nvidia.com/isaacsim/latest/features/robots_simulation/ext_omni_isaac_franka.html)
- [Contact/Force Sensors](https://docs.omniverse.nvidia.com/isaacsim/latest/features/sensors_simulation/isaac_sim_sensors_contact.html)

---

*"The difference between crushing and cradling is 926 nanoseconds."*

**Ship date: February 14, 2026**
