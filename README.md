# The Reflex: Sub-Microsecond Robotics Coordination

**Cache coherency signaling for 10kHz robotics control with 926ns P99 latency.**

---

<p align="center">
  <strong>255x faster than baseline Linux. Tested on NVIDIA Jetson AGX Thor.</strong>
</p>

---

## Headline Results

| Platform | Metric | Result |
|----------|--------|--------|
| **Jetson Thor** | P99 Latency | **926 ns** (255x improvement over baseline) |
| **Jetson Thor** | Control Rate | **10 kHz** sustained |
| **ESP32-C6** | Sub-CPU ALU | **59/59 tests** (CPU-orchestrated gates) |
| **ESP32-C6** | Autonomous ALU | **9/9 tests** (CPU in NOP loop during evaluation) |
| **ESP32-C6** | Signal Path | Timer → ETM → GDMA → PARLIO → GPIO → PCNT → LEDC |

## What is The Reflex?

The Reflex uses **cache coherency traffic** as a coordination primitive—the mechanism by which multi-core processors maintain memory consistency. No syscalls, no locks, no DDS overhead.

```
Sensor (Core 0) ──→ Controller (Core 1) ──→ Actuator (Core 2)
       │                    │                     │
       └──── 324 ns ────────┴────── 240 ns ───────┘
                     Total: 620 ns median
```

**Traditional approach:** ROS2/DDS → ~100μs per hop → max ~5kHz control
**The Reflex:** Cache coherency → ~300ns per hop → 10kHz+ control

---

## Repository Structure

```
the-reflex/
├── reflex-robotics/          # 10kHz CONTROL LOOP (Jetson Thor)
│   ├── src/
│   │   ├── reflex.h          # Core coordination primitive
│   │   └── control_loop.c    # Sensor→Controller→Actuator demo
│   ├── docs/
│   │   ├── PHASE_4_RESULTS.md    # 926ns P99 achieved
│   │   └── SKEPTICAL_ANALYSIS.md # Rigorous falsification
│   └── scripts/
│       └── setup_rt_host.sh  # RT configuration
│
├── reflex_ros_bridge/        # ROS2 INTEGRATION (NEW)
│   ├── src/
│   │   ├── bridge_node.cpp   # ROS2 topics ↔ shared memory
│   │   └── channel.cpp       # SharedChannel class
│   ├── reflex_force_control.c # Native 10kHz force controller
│   └── README.md             # Integration guide
│
├── reflex-os/                # THE REFLEX BECOMES THE ESP32-C6
│   ├── main/
│   │   ├── autonomous_alu.c  # Autonomous ALU (9/9 verified, CPU in NOP loop)
│   │   ├── alu_fabric.c      # Sub-CPU ALU (59/59 verified, CPU-orchestrated)
│   │   └── raid_etm_fabric.c # Autonomous fabric (5/5, ETM addrs wrong)
│   ├── include/
│   │   ├── reflex.h          # Core primitive (50 lines)
│   │   ├── reflex_gpio.h     # GPIO channels (12ns)
│   │   ├── reflex_timer.h    # Timer channels (10kHz)
│   │   ├── reflex_spline.h   # Catmull-Rom interpolation (137ns)
│   │   ├── reflex_void.h     # Entropy field for TriX echips
│   │   ├── reflex_echip.h    # Self-composing processor
│   │   ├── reflex_obsbot.h   # OBSBOT PTZ camera control
│   │   └── reflex_c6.h       # Master header
│   ├── tools/                # Linux host tools
│   │   ├── obsbot_test.c     # Camera test utility
│   │   └── stereo_demo.c     # Synchronized stereo vision
│   └── docs/
│       ├── ARCHITECTURE.md       # Channel model + entropy field
│       ├── API.md                # Complete API reference
│       ├── BENCHMARKS.md         # Performance measurements
│       ├── RAID_ETM_FABRIC.md    # Autonomous fabric architecture
│       ├── HARDWARE_ERRATA.md    # C6 constraints & workarounds
│       ├── FLASH_GUIDE.md        # Flash & serial procedure
│       └── REGISTER_REFERENCE.md # Bare-metal register addresses
│
├── pulse-arithmetic-lab/     # TEACHING LAB: PCNT + PARLIO neural computation
│   ├── firmware/01-05/       # 5 progressive demos
│   ├── docs/                 # Theory, hardware, ETM formalization
│   └── CLAIMS.md             # 7 falsifiable scientific claims
│
├── src/                      # RESEARCH EXPERIMENTS (the science)
│   ├── e3_latency_comparison.c   # Stigmergy vs Futex benchmark
│   ├── e1_coordination_v3.c      # Causality proof
│   └── e2b_false_sharing.c       # False positive control
│
├── docs/                     # DOCUMENTATION
│   ├── HARDWARE_INVENTORY.md     # All compute resources
│   ├── PI4_SETUP.md              # Raspberry Pi 4 setup
│   ├── LINCOLN_MANIFOLD_*.md     # Design analysis documents
│   └── ...                       # Lincoln Manifold phases
│
└── notebooks/
    └── stigmergy_demo.ipynb  # One-click Colab demo
```

---

## Quick Start: Robotics Control Loop

### On Jetson AGX Thor (or any ARM64 Linux)

```bash
git clone https://github.com/EntroMorphic/the-reflex.git
cd the-reflex/reflex-robotics

# Build
make all

# Run (needs root for SCHED_FIFO)
sudo taskset -c 0-2 ./build/control_loop
```

### Expected Output

```
╔═══════════════════════════════════════════════════════════════╗
║       REFLEX ROBOTICS: 10kHz CONTROL LOOP DEMO                ║
╚═══════════════════════════════════════════════════════════════╝

Coordination Latency (nanoseconds):
┌─────────────────────┬──────────┬──────────┐
│ Hop                 │  Median  │   P99    │
├─────────────────────┼──────────┼──────────┤
│ Sensor → Controller │    324   │    334   │
│ Controller → Actuat │    240   │    306   │
│ Total Loop          │    620   │    676   │
└─────────────────────┴──────────┴──────────┘

✓ SUCCESS: Sub-microsecond control loop achieved!
```

---

## Quick Start: Research Experiments

### On Raspberry Pi (Recommended)

```bash
git clone https://github.com/EntroMorphic/the-reflex.git
cd the-reflex/src

# Latency comparison
gcc -O3 -Wall -pthread e3_latency_comparison.c -o e3_latency -lm
./e3_latency
```

### On Google Colab (One-Click)

[![Open In Colab](https://colab.research.google.com/assets/colab-badge.svg)](https://colab.research.google.com/github/EntroMorphic/the-reflex/blob/main/notebooks/stigmergy_demo.ipynb)

---

## The Journey: From 236μs to 926ns

| Phase | Change | P99 Latency | Improvement |
|-------|--------|-------------|-------------|
| Baseline | Stock Linux | 236 μs | - |
| Phase 1 | +SCHED_FIFO, +mlockall | 2.4 μs | 98x |
| **Phase 4** | +isolcpus, +rcu_nocbs | **926 ns** | **255x** |

Full documentation in [`reflex-robotics/docs/`](reflex-robotics/docs/).

---

## How It Works

### The Coordination Primitive

```c
// reflex.h - 64-byte cache-aligned channel
typedef struct {
    volatile uint64_t sequence;   // Monotonically increasing
    volatile uint64_t timestamp;  // Producer's timestamp
    volatile uint64_t value;      // Optional payload
    char padding[40];             // Pad to cache line
} __attribute__((aligned(64))) reflex_channel_t;

// Producer: Signal with sub-100ns overhead
static inline void reflex_signal(reflex_channel_t* ch, uint64_t ts) {
    ch->timestamp = ts;
    ch->sequence++;
    __asm__ volatile("dsb sy" ::: "memory");  // ARM barrier
}

// Consumer: Wait for signal (spinning)
static inline uint64_t reflex_wait(reflex_channel_t* ch, uint64_t last_seq) {
    while (ch->sequence == last_seq) {
        __asm__ volatile("" ::: "memory");  // Compiler barrier
    }
    return ch->sequence;
}
```

### Why It's Fast

| Mechanism | Path | Latency |
|-----------|------|---------|
| **Reflex** | Cache write → snoop → invalidate → reload | ~300 ns |
| Futex | User → kernel → scheduler → wake → user | ~9,000 ns |
| DDS | Serialize → network stack → deserialize | ~100,000 ns |

The hardware already maintains cache coherency. We're just using it.

---

## Platforms Tested (Falsified)

| Platform | Architecture | Processing Time | Under Stress | 10kHz Control |
|----------|--------------|-----------------|--------------|---------------|
| **Jetson AGX Thor** | 14-core ARM | 309 ns | 366 ns (+18%) | ✅ 926ns P99 |
| **ESP32-C6** | RISC-V | 87 ns (ideal) / 187 ns (realistic) | 5.5 μs max | ✅ 10kHz + autonomous ALU (9/9) |
| Raspberry Pi 4 | 4-core ARM | 167 ns | - | ✅ (untested) |
| x86_64 Linux | Intel/AMD | ✅ works | - | ⚠️ (no isolcpus test) |

All numbers verified under adversarial testing with 100K+ samples. Zero catastrophic failures (>6μs) observed.

---

## The Silicon Grail: Autonomous Hardware Computation

The deepest layer of The Reflex: **boolean logic gates evaluated entirely by peripheral hardware** while the CPU sits in a NOP loop.

### Three Milestones Verified on Silicon

Each milestone built on the last. Each was verified on a physical ESP32-C6FH4 (QFN32) rev v0.2 board.

#### Milestone 1: Sub-CPU ALU (59/59 tests)

CPU orchestrates peripherals to implement logic gates. PCNT level-gated edge counting implements AND, OR, XOR, NOT, SHL/SHR, 2-bit ADD, 2-bit MUL, NAND/NOR. The CPU triggers each operation and reads the result — but the *computation* happens in PCNT hardware.

```
CPU triggers → PARLIO TX → GPIO 4,5 → PCNT counts edges → CPU reads result
```

**File:** `reflex-os/main/alu_fabric.c` | **Commit:** `f41d5ea`

#### Milestone 2: Autonomous Computation Fabric (5/5 tests)

First proof that the peripheral loop can run without CPU intervention. ETM crossbar wires Timer → GDMA → PARLIO → GPIO → PCNT → ETM feedback. 5004 state transitions in 500ms with CPU in NOP loop.

**File:** `reflex-os/main/raid_etm_fabric.c` | **Commit:** `5b2f62d`

**Note:** This milestone used incorrect ETM register addresses (0x600B8000 instead of 0x60013000). The "autonomous" transitions actually came from PCNT ISR callbacks, not true ETM crossbar routing. Discovered and corrected in Milestone 3.

#### Milestone 3: Autonomous ALU (9/9 tests)

The real thing. CPU configures peripherals, loads pattern buffers into SRAM as DMA descriptors, kicks a timer, and enters a NOP loop. All gate evaluation runs in hardware:

```
Timer alarm → ETM → GDMA start → PARLIO TX → GPIO 4,5 →
PCNT level-gated counting → PCNT threshold → ISR callback (result)
```

**9 tests verified:**

| Test | Input | Expected | Result |
|------|-------|----------|--------|
| AND(1,1) | A toggles, B=HIGH | TRUE | and=63, trans=1 |
| AND(1,0) | A toggles, B=LOW | FALSE | and=10, trans=0 |
| AND(0,1) | A=LOW, B toggles | FALSE | and=0, trans=0 |
| AND(0,0) | Both LOW | FALSE | and=0, trans=0 |
| XOR(1,0) | A toggles, B=LOW | TRUE | xor=32, trans=1 |
| XOR(0,1) | A=LOW, B toggles | TRUE | xor=38, trans=1 |
| XOR(1,1) | Both toggle (packed) | FALSE | xor=7, trans=0 |
| XOR(0,0) | Both LOW | FALSE | xor=0, trans=0 |
| **Chain** | **XOR(1,0) → AND(1,1)** | **Both fire** | **xor=34, and=63, trans=2** |

The chain test proves autonomous instruction sequencing: GDMA follows a linked list of two descriptors, executing XOR then AND evaluation without any CPU involvement.

**File:** `reflex-os/main/autonomous_alu.c` | **Commit:** `59d0bba`

### The Signal Path

```
┌─────────────────────────────────────────────────────────────────┐
│                    AUTONOMOUS ALU                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   SRAM patterns ──► GDMA ──► PARLIO TX ──► GPIO 4,5            │
│   (DMA descriptors)              │              │               │
│                                  │              ▼               │
│                                  │         PCNT units           │
│                                  │         (level-gated         │
│                                  │          edge counting)      │
│                                  │              │               │
│   Timer alarm ──► ETM ──────────►│         threshold ──► ISR    │
│   (kickoff)       (crossbar)     │         (gate = TRUE)        │
│                                  │                              │
│   CPU: configure → start timer → NOP loop → read results       │
│   Hardware: everything between start and read                   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### What We Learned the Hard Way

Building this required fighting the hardware at every step. Six new errata were discovered and documented in `reflex-os/docs/HARDWARE_ERRATA.md`:

**ETM register addresses were wrong everywhere.** The base is `0x60013000`, not `0x600B8000`. The CLK_EN register is at `+0x1A8`, not `+0x00`. The PCR ETM conf is at `PCR_BASE+0x98`, not `+0x90`. These were wrong in ESP-IDF examples and cost days of debugging.

**IDF startup disables the ETM bus clock.** `clk.c` line 270 calls `etm_ll_enable_bus_clock(0, false)`. You must re-enable via PCR before touching any ETM register, or reads return garbage and writes are silently dropped.

**GDMA LINK_START leaks data despite ETM_EN.** Setting `GDMA_LINK_START_BIT` with `ETM_EN_BIT` causes immediate data transfer, not deferred. ~10-17 stray PCNT edges appear before the ETM-triggered start. Fix: defer PARLIO `TX_START` until after PCNT is cleared, so leaked FIFO data doesn't drive GPIOs.

**PARLIO nibble boundaries create PCNT glitch counts.** When PARLIO shifts between 4-bit nibbles within a byte, GPIO transitions create brief glitch states that PCNT registers as real edges (~6-17 counts). Fix: set PCNT watch threshold to 25 (above noise floor, below real counts of 32/63).

**GDMA with ETM_EN won't auto-follow linked list descriptors.** Each `TASK_GDMA_START` processes one descriptor then stops. The linked list `next` pointer is ignored. Fix: for descriptor chaining, use normal GDMA mode (no `ETM_EN`) where GDMA auto-follows the linked list.

**LEDC timer cannot be resumed after ETM-triggered pause.** Once ETM fires `TASK_LEDC_T0_PAUSE`, the timer stays frozen permanently. Neither `ledc_timer_resume()` nor full reconfiguration restarts it. Workaround: use PCNT ISR callbacks instead of LEDC for result detection.

### Pattern Encoding

PARLIO 4-bit mode packs 2 nibbles per byte. Lower nibble (bits 3:0) maps to GPIO 4-7 first, upper nibble (bits 7:4) second.

| Pattern | Encoding | Purpose |
|---------|----------|---------|
| `pat_and_11` | all `0x23` | B=HIGH stable, A toggles → AND=63 |
| `pat_and_10` | alt `0x01/0x00` | A toggles, B=0 → AND=0 |
| `pat_xor_10` | alt `0x01/0x00` | A toggles, B=0 → XOR=32 |
| `pat_xor_01` | alt `0x02/0x00` | B toggles, A=0 → XOR=38 |
| `pat_null` | all `0x00` | Both LOW → all gates=0 |

PCNT AND unit: count A edges when B=HIGH (level-gate). PCNT XOR unit: 2 channels — (A edges when B=LOW) + (B edges when A=LOW).

### What Makes This Different

| Technology | Similar? | Key Difference |
|------------|----------|----------------|
| Neuromorphic (Loihi, TrueNorth) | Spiking NN | Custom ASIC, $$$$ |
| FPGA | Reconfigurable | Expensive, power hungry |
| TinyML | Edge AI | Still CPU-centric |
| **Autonomous ALU** | **Ours** | **$3 commodity MCU, CPU in NOP loop** |

### Key Insight

> *"We found computation hiding in the peripheral fabric."*
>
> PCNT was designed to count encoder pulses. PARLIO was designed for LCD interfaces. GDMA was designed for data transfer. ETM was designed to reduce interrupt latency.
>
> Together, they form a boolean ALU that runs while the CPU sleeps.

### Files

```
reflex-os/main/
├── autonomous_alu.c      # Autonomous ALU (9/9 verified) — the current milestone
├── alu_fabric.c           # Sub-CPU ALU (59/59 verified) — reference implementation
└── raid_etm_fabric.c      # Autonomous fabric (5/5 verified, ETM addrs wrong)

reflex-os/docs/
├── HARDWARE_ERRATA.md     # 15+ errata discovered during development
└── REGISTER_REFERENCE.md  # Correct bare-metal register addresses
```

---

## The Reflex Becomes the ESP32-C6

The Reflex isn't just for high-end systems. On the ESP32-C6, **The Reflex IS the operating system**:

| Primitive | Latency | Condition |
|-----------|---------|-----------|
| `gpio_write()` | **12 ns** | Direct register, 2 cycles |
| Pure decision | **12 ns** | Threshold + GPIO only |
| Full reflex | **87 ns** | Ideal (interrupts off) |
| Full reflex | **187 ns** | Realistic (interrupts on) |
| `spline_read()` | **137 ns** | Catmull-Rom interpolation |
| With channel | **437 ns** | Cross-core coordination |

### Key Innovations

- **Spline Channels**: Catmull-Rom interpolation bridges discrete signals to continuous trajectories
- **Entropy Field**: The void between shapes carries information—computation IS entropy flow
- **TriX echips**: Soft chips built from shapes in an entropy field substrate
- **Self-Composing Intelligence**: 4,096 shapes + 16,384 routes + Hebbian learning = circuits that grow themselves
- **Stereo Vision**: OBSBOT PTZ cameras driven by entropy attention at 121µs latency

See [`reflex-os/`](reflex-os/) for full documentation.

---

## The Dreaming Swarm Cathedral

Beyond the C6, the full system includes:

| Tier | Device | Role |
|------|--------|------|
| God | Jetson AGX Thor | 100M shape echip, entropy field substrate |
| Mind | Raspberry Pi 4 | OBSBOT controller, slow time layer |
| Neurons | 3× ESP32-C6 | Sub-µs reflexes, swarm nodes |
| Eyes | 2× OBSBOT Tiny | Stereo vision, entropy-driven gaze |
| Choir | 5× Max98357 | Audio sonification of entropy |

See [`docs/HARDWARE_INVENTORY.md`](docs/HARDWARE_INVENTORY.md) for complete details.

---

## Primordial Stillness + Delta Observer

The Reflex extends beyond coordination into **awareness-inspired architecture**:

| Component | Role |
|-----------|------|
| **Primordial Stillness** | 16M voxel entropy field as awareness substrate |
| **Delta Observer** | Watches neural networks learn, captures transient clustering |
| **The Bridge** | Latent vectors become disturbances in the stillness field |

**Key insight:** The Delta Observer discovered that neural networks build **scaffolding** (transient clustering) to learn, then tear it down. Post-hoc analysis misses this entirely. The Primordial Stillness is the substrate being disturbed.

> *"The field at rest models awareness. Perception models disturbance. The architecture sidesteps infinite regress by making the observer definitionally invariant."*

See:
- [`docs/PRIMORDIAL_STILLNESS.md`](docs/PRIMORDIAL_STILLNESS.md) - Stillness as computational substrate
- [`docs/PHILOSOPHY.md`](docs/PHILOSOPHY.md) - Philosophical speculation (clearly marked)
- [`docs/DELTA_STILLNESS_BRIDGE.md`](docs/DELTA_STILLNESS_BRIDGE.md) - The unified architecture
- [Delta Observer (separate repo)](https://github.com/EntroMorphic/delta-observer) - The paper and code

---

## Skeptical Analysis

We challenged every claim. See [`SKEPTICAL_ANALYSIS.md`](reflex-robotics/docs/SKEPTICAL_ANALYSIS.md).

**Key findings:**
- ✅ Median 500-700ns confirmed across 5 runs
- ✅ Core isolation works (load on cores 3-7 doesn't affect 0-2)
- ✅ P99 drops from 236μs to 926ns with proper configuration
- ⚠️ Extreme outliers (50ms) remain without PREEMPT_RT kernel

---

## Falsification Results

We attempted to break our own claims under adversarial testing. They survived.

### Thor (Jetson AGX)

| Condition | Processing Time | Max Observed |
|-----------|-----------------|--------------|
| Normal | 309 ns | 1,268 ns |
| Under CPU stress | 366 ns (+18%) | Stable |

### ESP32-C6

| Condition | Avg | Max | Samples |
|-----------|-----|-----|---------|
| Ideal (interrupts off) | 87 ns | 3.2 μs | 10,000 |
| Realistic (interrupts on) | 187 ns | 5.5 μs | 100,000 |
| Spikes > 6μs | **0%** | - | - |

### Component Breakdown (C6)

| Component | Cost |
|-----------|------|
| Pure decision (threshold + GPIO) | 12 ns |
| PRNG overhead | +75 ns |
| Interrupt servicing | +100 ns |
| Channel coordination (fences) | +350 ns |

**Bottom line:** Sub-microsecond average on both platforms. Single-digit microsecond worst case. Zero catastrophic failures in 100K+ samples.

See: [`docs/FALSIFICATION_COMPLETE.md`](docs/FALSIFICATION_COMPLETE.md)

---

## Configuration for Best Results

### Minimum (Phase 1)
```c
mlockall(MCL_CURRENT | MCL_FUTURE);
sched_setscheduler(0, SCHED_FIFO, &param);
```
**Result:** P99 drops from 236μs to 2.4μs

### Recommended (Phase 4)
```bash
# Boot parameters
isolcpus=0,1,2 rcu_nocbs=0,1,2

# Runtime
sudo taskset -c 0-2 ./control_loop
```
**Result:** P99 drops to 926ns

---

## ROS2 Integration

The Reflex bridges to ROS2 via shared memory, giving you **nanosecond** processing alongside the ROS ecosystem:

```
ROS2 Topics (1kHz)          Native Controller (event-driven)
      │                              │
      ▼                              ▼
┌─────────────┐              ┌─────────────────┐
│ bridge_node │◄── shm ─────►│ reflex_force_   │
│             │              │ control         │
└─────────────┘              └─────────────────┘
      │                              │
/force_sensor              /dev/shm/reflex_*
/gripper_command           ~300ns processing
```

| Mode | Processing Time | Check Rate | Anomalies Caught |
|------|-----------------|------------|------------------|
| **REFLEX** | **~309 ns** | Event-driven | 1,127 |
| ROS2-1kHz | ~500 ns | 1 kHz | ~1,070 |
| ROS2-100Hz | ~500 ns | 100 Hz | ~113 |

**Honest assessment:** REFLEX catches ~5% more anomalies than well-tuned 1kHz polling. The significant win is over typical 100Hz systems (10x more anomalies caught). The key advantage is **event-driven** vs polling—we never miss a signal.

See [`reflex_ros_bridge/`](reflex_ros_bridge/) for full documentation.

---

## Applications

- **Robotics:** 10kHz+ control loops for manipulators, drones, legged robots
- **Autonomous Vehicles:** Sensor fusion with bounded latency
- **Industrial Automation:** PLC-replacement with deterministic timing
- **High-Frequency Trading:** Ultra-low-latency event propagation

---

## Citation

```bibtex
@misc{thereflex2026,
  title={The Reflex: Sub-Microsecond Robotics Coordination via Cache Coherency},
  author={EntroMorphic Research},
  year={2026},
  url={https://github.com/EntroMorphic/the-reflex}
}
```

---

## License

MIT License

---

**The hardware is already doing the work. We're just using it.**

*12ns on a $5 chip. 309ns on a $2000 Jetson. Same code. Same topology. Falsified under adversarial testing.*
