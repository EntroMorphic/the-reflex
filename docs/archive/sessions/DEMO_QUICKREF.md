# Valentine's Day Demo: Quick Reference

## The Pitch

> "309 nanoseconds. Event-driven means we never miss a threshold breach."

---

## Key Numbers

| Mode | Processing | Check Rate | Anomalies |
|------|------------|------------|-----------|
| **REFLEX** | **~309 ns** | Event-driven | 1,127 |
| ROS2-1kHz | ~500 ns | 1 kHz | 1,070 |
| ROS2-100Hz | ~500 ns | 100 Hz | ~113 |

**Honest take:** REFLEX catches ~5% more anomalies than well-tuned 1kHz ROS2. The significant win is over typical 100Hz systems (10x more anomalies caught).

---

## Run the Demo

### On Thor (SSH)

```bash
ssh -p 11965 ztflynn@10.42.0.2
cd /home/ztflynn/the-reflex/reflex_ros_bridge/scripts

# Full A/B comparison
./run_demo.sh compare

# Or run individually
./run_demo.sh reflex   # ~309ns processing
./run_demo.sh ros2     # 10ms polling baseline
```

### Manual Start

```bash
# Terminal 1: Bridge (if not running)
docker ps | grep reflex_bridge || docker run -d --rm --runtime nvidia \
  --name reflex_bridge --network host --ipc host \
  -v /dev/shm:/dev/shm -v /home/ztflynn/the-reflex:/workspace/the-reflex \
  dustynv/ros:humble-desktop-l4t-r36.4.0 \
  bash -c "source /opt/ros/humble/install/setup.bash && \
    mkdir -p /tmp/ros_ws/src && \
    cp -r /workspace/the-reflex/reflex_ros_bridge /tmp/ros_ws/src/ && \
    cd /tmp/ros_ws && colcon build --packages-select reflex_ros_bridge >/dev/null 2>&1 && \
    source install/setup.bash && \
    export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/tmp/ros_ws/install/reflex_ros_bridge/lib && \
    ros2 run reflex_ros_bridge bridge_node"

# Terminal 2: Force Simulator
docker exec reflex_bridge bash -c "source /opt/ros/humble/install/setup.bash && \
  source /tmp/ros_ws/install/setup.bash && \
  ros2 run reflex_ros_bridge force_simulator_node"

# Terminal 3: Reflex Controller
cd /home/ztflynn/the-reflex/reflex_ros_bridge
./reflex_force_control --reflex
```

---

## What to Show

### Grasp Cycle (14 seconds)

| Phase | Time | Force | What Happens |
|-------|------|-------|--------------|
| APPROACH | 0-2s | 0N | Gripper closing |
| CONTACT | 2-4s | 0→2N | Object detected |
| GRASP | 4-8s | 2N | Stable hold |
| **ANOMALY** | 8-9s | **7N** | **Slip/impact** |
| RECOVERY | 9-10s | 7→2N | Reflex responds |
| STABLE | 10-12s | 2N | Back to normal |
| RELEASE | 12-14s | 2→0N | Let go |

### Key Moment: ANOMALY Phase

- Force spikes to 7N (threshold is 5N)
- **REFLEX mode:** Processes in ~309ns, catches 1,127 anomalies (event-driven)
- **ROS2-100Hz mode:** Checks every 10ms, catches only ~113 anomalies
- **ROS2-1kHz mode:** Checks every 1ms, catches ~1,070 anomalies (fair baseline)

---

## Expected Output

### REFLEX Mode

```
╔═══════════════════════════════════════════════════════════════╗
║       FORCE CONTROL: STATISTICS (REFLEX MODE)                 ║
╚═══════════════════════════════════════════════════════════════╝
  Mode:           REFLEX
  Loop count:     15,992
  Anomaly count:  1,127
  
  Processing time:  309 ns average, 1,268 ns max
  
  ✓ Event-driven - responds on every signal arrival
```

### ROS2-1kHz Mode (Fair Baseline)

```
╔═══════════════════════════════════════════════════════════════╗
║       FORCE CONTROL: STATISTICS (ROS2-1kHz MODE)              ║
╚═══════════════════════════════════════════════════════════════╝
  Mode:           ROS2-1kHz
  Loop count:     ~16,000
  Anomaly count:  ~1,070
  
  Check interval: 1 ms
  
  ✓ Well-tuned baseline - catches most anomalies
```

### ROS2-100Hz Mode (Typical Baseline)

```
╔═══════════════════════════════════════════════════════════════╗
║       FORCE CONTROL: STATISTICS (ROS2-100Hz MODE)             ║
╚═══════════════════════════════════════════════════════════════╝
  Mode:           ROS2-100Hz
  Loop count:     ~1,600
  Anomaly count:  ~113
  
  Check interval: 10 ms
  
  ✗ Typical polling - misses anomalies between checks
```

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Bridge not starting | Check `docker logs reflex_bridge` |
| No shared memory | Verify `/dev/shm/reflex_*` files exist |
| "Permission denied" | Shared memory should be mode 666 |
| sched_setscheduler error | Normal without sudo, doesn't affect demo |

---

## The Technology

**Why it's fast:**
1. No syscalls - direct cache line access
2. No locks - atomic operations only
3. No polling - hardware wakes us via cache coherency
4. 64-byte channels - fits in L1 cache

**The core primitive:**
```c
// Spin until sequence changes (hardware does the waiting)
while (ch->sequence == last_seq) { }
// Process: ~309ns from signal arrival to command sent
```

---

## Falsification Results

These numbers have been verified under adversarial testing:

| Platform | Normal | Under Stress | Worst Case |
|----------|--------|--------------|------------|
| Thor | 309 ns | 366 ns (+18%) | 1.3 μs |
| C6 | 87 ns (ideal) | 187 ns (realistic) | 5.5 μs |

Zero catastrophic failures (>6μs) in 100K+ samples on either platform.

See: [FALSIFICATION_COMPLETE.md](FALSIFICATION_COMPLETE.md)

---

*February 14, 2026 - Ship it.*
