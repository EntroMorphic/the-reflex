# Valentine's Day Demo: Next Steps

## Current Status: Day 1 Complete ✅

**Proven:**
- ROS2 Humble running on Thor
- reflex_ros_bridge builds and runs
- Shared memory communication working
- Native force controller achieving 9.69 MHz (103ns avg loop)

---

## Remaining Work

### Phase 1: Force Simulator (Day 2)

**Goal:** Create realistic force profile for demo

```
Time →
     ┌─────────────────────────────────────────────────────┐
   6N│                          ████ ANOMALY              │
     │                        ██                          │
   5N│----------------------██------- THRESHOLD ----------│
     │                     █                              │
   4N│                   ██                               │
     │                 ██                                 │
   3N│               ██                                   │
     │             ██                                     │
   2N│-----------██--------------------------------------|│ TARGET
     │          █                                         │
   1N│        ██  CONTACT                                │
     │      ██                                            │
   0N│██████     APPROACH                                 │
     └─────────────────────────────────────────────────────┘
        0s    1s    2s    3s    4s    5s    6s    7s
```

**Implementation:**
1. Create `force_simulator_node.cpp` - publishes WrenchStamped at 1kHz
2. Modes: approach → contact → grasp → anomaly → recovery
3. Add noise for realism
4. Configurable ramp rates and thresholds

**Files to create:**
- `src/force_simulator_node.cpp`
- `launch/demo.launch.py` (full demo launch)

---

### Phase 2: Telemetry Visualization (Day 3-4)

**Goal:** Real-time graphs showing Reflex in action

**Option A: Rerun SDK**
```python
import rerun as rr
rr.log("force/measured", rr.Scalar(force))
rr.log("force/threshold", rr.Scalar(5.0))
rr.log("command/position", rr.Scalar(position))
rr.log("latency/ns", rr.Scalar(latency))
```

**Option B: ROS2 + Foxglove**
- Use existing topics
- Configure Foxglove layout
- Record to MCAP for replay

**Deliverable:** Screenshot/video showing real-time force control

---

### Phase 3: A/B Comparison (Day 5-6)

**Goal:** Demonstrate the difference Reflex makes

**Mode A: ROS2-Only Control (Baseline)**
- 100 Hz control loop (typical ROS2)
- Force overshoot visible
- Latency: ~10ms

**Mode B: Reflex-Augmented**
- 10 kHz control loop
- Instant threshold response
- Latency: ~1μs

**Visual:**
```
Force Overshoot Comparison
                    
  ROS2-Only         Reflex
     │                 │
   6N│   ████          │
   5N│--████-- STOP    │------ STOP
   4N│  █              │  █
   3N│ █               │ █
   2N│█                │█
     └────────         └────────
      OVERSHOOT         NO OVERSHOOT
```

**Implementation:**
1. Add `--mode ros2_only` flag to force controller
2. In ros2_only mode: sleep 10ms between iterations
3. Record both runs
4. Side-by-side comparison video

---

### Phase 4: Polish & Recording (Day 7-10)

**Demo Script:**
1. Show empty gripper approaching object
2. Contact detected - force ramps up
3. **Split screen:** ROS2-only vs Reflex
4. Inject anomaly (sudden force spike)
5. ROS2-only: overshoots, damages object
6. Reflex: instant stop, object safe
7. Display latency numbers

**Recording Setup:**
- OBS on workstation
- Rerun viewer fullscreen
- Terminal with stats overlay
- Duration: 60-90 seconds

---

### Phase 5: Final Integration (Day 11-14)

**Optional Enhancements:**
- [ ] Isaac Sim visualization (if time permits)
- [ ] Actual gripper hardware demo
- [ ] Multiple anomaly scenarios
- [ ] Latency histogram animation

**Deliverables:**
1. Demo video (MP4, 1080p)
2. Technical summary (1 page)
3. Code repository cleaned up
4. README with reproduction steps

---

## Timeline

| Day | Date | Task | Status |
|-----|------|------|--------|
| 1 | Jan 30 | Container + Bridge + E2E Test | ✅ |
| 2 | Jan 31 | Force Simulator | 🔲 |
| 3 | Feb 1 | Telemetry Setup | 🔲 |
| 4 | Feb 2 | Telemetry Polish | 🔲 |
| 5 | Feb 3 | A/B Mode Implementation | 🔲 |
| 6 | Feb 4 | A/B Comparison Testing | 🔲 |
| 7 | Feb 5 | Demo Script | 🔲 |
| 8 | Feb 6 | Recording Setup | 🔲 |
| 9 | Feb 7 | First Recording | 🔲 |
| 10 | Feb 8 | Recording Polish | 🔲 |
| 11 | Feb 9 | Integration Testing | 🔲 |
| 12 | Feb 10 | Bug Fixes | 🔲 |
| 13 | Feb 11 | Final Polish | 🔲 |
| 14 | Feb 12 | Final Recording | 🔲 |
| 15 | Feb 13 | Buffer Day | 🔲 |
| **16** | **Feb 14** | **SHIP** | 🔲 |

---

## Quick Reference

### Start Demo Stack

```bash
# On Thor - Terminal 1: ROS2 Bridge
docker run -d --rm --runtime nvidia \
  --name reflex_bridge \
  --network host --ipc host \
  -v /dev/shm:/dev/shm \
  -v /home/ztflynn/the-reflex:/workspace/the-reflex \
  dustynv/ros:humble-desktop-l4t-r36.4.0 \
  bash -c "source /opt/ros/humble/install/setup.bash && \
           cd /tmp && mkdir -p ros_ws/src && \
           cp -r /workspace/the-reflex/reflex_ros_bridge ros_ws/src/ && \
           cd ros_ws && colcon build --packages-select reflex_ros_bridge && \
           source install/setup.bash && \
           export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/tmp/ros_ws/install/reflex_ros_bridge/lib && \
           ros2 run reflex_ros_bridge bridge_node"

# On Thor - Terminal 2: Reflex Controller
cd /home/ztflynn/the-reflex/reflex_ros_bridge
./reflex_force_control

# On Thor - Terminal 3: Force Simulator (when ready)
docker exec reflex_bridge bash -c "source /opt/ros/humble/install/setup.bash && \
  source /tmp/ros_ws/install/setup.bash && \
  ros2 run reflex_ros_bridge force_simulator_node"
```

### Monitor Topics

```bash
docker exec reflex_bridge bash -c "source /opt/ros/humble/install/setup.bash && \
  ros2 topic echo /reflex/latency_ns"
```

---

*Created: January 30, 2026*
