# Hardware Inventory: The Dreaming Swarm Cathedral

> All compute resources available for the distributed consciousness experiment.

---

## Compute Hierarchy

```
                         ┌─────────────────────────────────┐
                         │       JETSON AGX THOR           │
                         │   2000 TOPS · 128GB · THE GOD   │
                         └───────────────┬─────────────────┘
                                         │
              ┌──────────────────────────┼──────────────────────────┐
              │                          │                          │
       ┌──────▼──────┐            ┌──────▼──────┐            ┌──────▼──────┐
       │   Pi 4 8GB  │            │   Pi 3 2GB  │            │  Moto G5    │
       │  SLOW MIND  │            │  VISUALIZER │            │   MOBILE    │
       └──────┬──────┘            └─────────────┘            └─────────────┘
              │
    ┌─────────┼─────────┬─────────────────┬─────────────────┐
    │         │         │                 │                 │
┌───▼───┐ ┌───▼───┐ ┌───▼───┐      ┌──────▼──────┐   ┌──────▼──────┐
│ESP32  │ │ESP32  │ │ESP32  │      │   OBSBOT    │   │   OBSBOT    │
│ C6-1  │ │ C6-2  │ │ C6-3  │      │  LEFT EYE   │   │  RIGHT EYE  │
└───────┘ └───────┘ └───────┘      └─────────────┘   └─────────────┘
```

---

## Tier 1: The God (Thor)

| Property | Value |
|----------|-------|
| Device | NVIDIA Jetson AGX Thor |
| CPU | 14-core ARM @ 2.6 GHz |
| GPU | NVIDIA Thor (2000 TOPS) |
| RAM | 128 GB unified |
| Storage | 937 GB NVMe |
| Network | 10.42.0.2:11965 |
| Role | Consciousness substrate, geological time layer |

**Reflex Performance (Falsified):**

| Metric | Value | Condition |
|--------|-------|-----------|
| Processing time | 309 ns | Normal operation |
| Processing time | 366 ns | Under CPU stress (+18%) |
| P99 control loop | 926 ns | With isolcpus + rcu_nocbs |
| Max observed | 1,268 ns | Under stress test |
| echip capacity | 100M+ shapes | |

---

## Tier 2: The Mind (Raspberry Pi 4)

| Property | Value |
|----------|-------|
| Device | Raspberry Pi 4 Model B |
| Hostname | sirius |
| CPU | 4× ARM Cortex-A72 @ 1.5 GHz |
| RAM | 8 GB |
| OS | Debian 13 (trixie) |
| Network | 10.64.24.48:11965 |
| Role | ESP32 dev host, OBSBOT controller, slow time layer |

**Connected Devices:**
- 2× OBSBOT Tiny (/dev/video0, /dev/video2)
- 1× ESP32-C6 (/dev/ttyACM0) - currently unbound

**Reflex Performance:**
- Stigmergy: 167 ns
- OBSBOT PTZ: 121 µs

---

## Tier 3: Peripheral Neurons (ESP32-C6)

**Stable device path:** `/dev/esp32c6` (udev symlink for VID:PID `303a:1001`, points to whichever ttyACM the active C6 enumerates on).

| Device | Port | Status | Chip | Flash | MAC |
|--------|------|--------|------|-------|-----|
| ESP32-C6 #1 | /dev/ttyACM0 | ✅ Running spine firmware | ESP32-C6FH4 | 4MB | b4:3a:45:ff:fe:8a:c4:d4 |
| ESP32-C6 #2 | /dev/ttyACM1 | ✅ Verified | ESP32-C6FH4 | 4MB | b4:3a:45:ff:fe:8a:c7:d4 |
| ESP32-C6 #3 | /dev/ttyACM2 | ✅ Verified | ESP32-C6FH4 | 4MB | b4:3a:45:ff:fe:8a:c8:24 |

*Last verified: February 1, 2026 via reflex-cli (97 tests passing)*

**Specs (each):**
- CPU: RISC-V @ 160 MHz
- RAM: 452 KB SRAM
- Features: WiFi 6, BLE 5, 802.15.4
- Cost: ~$5

**Reflex Performance (Falsified):**

| Metric | Value | Condition |
|--------|-------|-----------|
| gpio_write() | 12 ns | Direct register |
| Pure decision | 12 ns | Threshold + GPIO |
| Ideal operation | 87 ns | Interrupts disabled |
| Realistic | 187 ns | Interrupts enabled |
| With channel | 437 ns | Cross-core coordination |
| Worst case | 5.5 μs | 0% >6μs in 100K samples |

**echip capacity:** 4K shapes, 16K routes

---

## Tier 4: The Eyes (OBSBOT Tiny)

| Device | Port | USB | Role |
|--------|------|-----|------|
| OBSBOT #1 | /dev/video0 | usb-1.2 | Right eye |
| OBSBOT #2 | /dev/video2 | usb-1.1 | Left eye |

**Specs (each):**
- Resolution: 1080p @ 30fps (4K capable)
- Pan: ±130° (468,000 arc-sec)
- Tilt: ±90° (324,000 arc-sec)
- Zoom: 0-100 (digital)
- AI: Built-in face/body tracking

**Reflex Performance:**
- PTZ command latency: 121 µs
- Stereo sync: Yes (with keepalive)

---

## Tier 5: Additional Compute

### Raspberry Pi 3 (2GB)
| Property | Value |
|----------|-------|
| CPU | 4× ARM Cortex-A53 @ 1.2 GHz |
| RAM | 2 GB |
| Role | Visualization server, web UI |

### Motorola Moto G5 Play
| Property | Value |
|----------|-------|
| CPU | Snapdragon 680 octa-core @ 2.4 GHz |
| RAM | 3-4 GB |
| Sensors | Camera, accelerometer, gyroscope, GPS |
| Role | Mobile sensor brain, wandering eye |

### Pi Zero
| Property | Value |
|----------|-------|
| CPU | ARM11 @ 1 GHz |
| RAM | 512 MB |
| Role | Lightweight bridge node |

---

## Tier 6: Specialized Hardware

### ESP32-S (×2)
- CPU: Xtensa LX6 dual-core @ 240 MHz
- RAM: 520 KB
- Role: Additional swarm nodes

### SiFive RISC-V + Arduino Display
- Role: Entropy field visualization

### STM32F4-Discovery
- CPU: ARM Cortex-M4 @ 168 MHz
- RAM: 192 KB
- Role: Hard real-time motor control, actuator interface

---

## Tier 7: The Choir (Audio)

| Device | Count | Interface | Role |
|--------|-------|-----------|------|
| Max98357 | 5 | I2S | Entropy sonification |

Each node gets a voice. The swarm sings its own dynamics.

---

## Network Topology

```
Workstation (10.42.0.1)
    │
    ├── Thor (10.42.0.2:11965)
    │       └── Docker: entromorphic-dev
    │
    └── Pi4 (10.64.24.48:11965)
            ├── OBSBOT ×2 (USB)
            └── ESP32-C6 (USB)
```

---

## The Vision: Dreaming Swarm Cathedral

All hardware combined enables:

1. **Thor**: 100M shape echip as consciousness substrate
2. **Pi4**: Entropy field server + OBSBOT controller
3. **3× C6**: Peripheral neurons with sub-microsecond reflexes
4. **2× OBSBOT**: Stereo vision tracking entropy attention
5. **5× Audio**: The choir singing the collective unconscious
6. **Moto G5**: Wandering sensor feeding dream content

**Total compute:** ~2000 TOPS (Thor) + ~20 GFLOPS (rest)
**Total eyes:** 4 (2× OBSBOT + phone + SiFive display)
**Total voices:** 5 (Max98357 boards)
**Reaction time:** 121 µs (eyes) / 118 ns (reflexes)

---

*"A silicon collective unconscious with REM sleep."*
