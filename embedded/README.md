# Reflex OS: ESP32-C6 Ternary Compute Platform

> **This README is largely historical.** The system has evolved significantly since it was written.
> For the current technical reality, see [`../TECHNICAL_REALITY.md`](../TECHNICAL_REALITY.md).
>
> **Current state (Feb 9, 2026):** 27 milestones verified on silicon. The active code is
> `main/geometry_cfc_freerun.c` (HP entry point), `main/ulp/main.S` (LP core assembly),
> and `main/reflex_vdb.c` (VDB API). Everything below this banner describes earlier work.

---

## What is Reflex OS? (Historical)

Reflex OS is not an operating system that runs on the ESP32-C6. Reflex OS **is** the C6 - a complete reimagining of the chip as a channel machine where every peripheral, every GPIO, every timer is a signal source or sink.

```
┌─────────────────────────────────────────────────────────────────┐
│                      ESP32-C6 @ 160MHz                          │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    THE APPLICATION                       │   │
│  │   Reflexors: pattern → response at nanosecond speed     │   │
│  └─────────────────────────────────────────────────────────┘   │
│                            │                                    │
│  ┌─────────────────────────┴───────────────────────────────┐   │
│  │                   CHANNEL LAYER                          │   │
│  │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐  │   │
│  │  │ GPIO │ │ ADC  │ │Timer │ │ SPI  │ │ WiFi │ │ Void │  │   │
│  │  │  12ns│ │ 21us │ │10kHz │ │ 29us │ │ 6ms  │ │125ns │  │   │
│  │  └──────┘ └──────┘ └──────┘ └──────┘ └──────┘ └──────┘  │   │
│  └─────────────────────────────────────────────────────────┘   │
│                            │                                    │
│  ┌─────────────────────────┴───────────────────────────────┐   │
│  │                    reflex.h (118ns)                      │   │
│  │         The coordination primitive. 50 lines.            │   │
│  └─────────────────────────────────────────────────────────┘   │
│                            │                                    │
│                      RISC-V Silicon                             │
└─────────────────────────────────────────────────────────────────┘
```

---

## Benchmark Results

| Primitive | Latency | Notes |
|-----------|---------|-------|
| `gpio_write()` | **12 ns** (2 cycles) | Direct register access |
| `reflex_signal()` | **118 ns** (19 cycles) | Core coordination primitive |
| `spline_read()` | **137 ns** (22 cycles) | Catmull-Rom interpolation |
| `entropy_deposit()` | **~125 ns** | Stigmergy write |
| Channel roundtrip | **206 ns** (33 cycles) | Signal + wait + read |
| ADC conversion | **21 us** | 12-bit, full range |
| SPI byte transfer | **29 us** | 1MHz clock |
| Timer loop | **10.05 kHz** | 100us period, <1% jitter |
| WiFi connect | **~6 seconds** | Including DHCP |

---

## Neural Computation (Feb 4, 2026)

### Pulse Arithmetic Engine (Latest)

The breakthrough: PCNT counts pulses = hardware addition. PARLIO transmits in parallel = parallel computation.

| Implementation | Rate | Key Innovation |
|----------------|------|----------------|
| cfc_parallel_dot | 1249 Hz | Parallel 8-bit PARLIO + 4 PCNT units |
| cfc_dual_channel | 964 Hz | True hardware pos/neg via dual PCNT channels |
| spectral_double_cfc | 1100 Hz | Dual timescale (fast + slow networks) |
| spectral_ffn | **5679 Hz** | Self-modifying coupling via coherence |
| spectral_eqprop | 580 Hz / 274 Hz learn | **Equilibrium propagation learning** |

**Equilibrium Propagation:** The backward pass IS the forward dynamics, perturbed. No separate gradient computation. Learning emerges from contrastive perturbation.

**Learning Results:**
- 2-pattern task: 21.5/256 avg error (94.5% of target separation)
- Coupling matrix evolves from uniform 0.2 to asymmetric 0.01-0.98

See [docs/archive/PULSE_ARITHMETIC_ENGINE.md](docs/archive/PULSE_ARITHMETIC_ENGINE.md) for full details.

### Earlier Approaches

| System | Rate | Neurons | Memory | CPU | Notes |
|--------|------|---------|--------|-----|-------|
| Yinsen Q15 CfC | **90 kHz** | 8 | 64 KB | 100% | Zero floating-point |
| Yinsen Q15 CfC | **18 kHz** | 32 | 128 KB | 100% | Sparse ternary weights |
| Yinsen Q15 CfC | **6.9 kHz** | 64 | 256 KB | 100% | Full hidden state |
| ETM Fabric CfC | **551 Hz** | 64 | 304 KB | ~11% | Near-zero CPU |
| **Silicon Grail** | **TBD** | 64 | **640 B** | **~0%** | **Turing Complete** |

### The Silicon Grail: Turing Complete ETM Fabric

**GDMA M2M can write to peripheral registers.** This changes everything.

- **GDMA** writes pulse patterns directly to RMT memory (0x60006100)
- **ETM** chains: Timer → GDMA → RMT → PCNT → branch decision → loop
- **Timer race + GDMA priority** = conditional branching WITHOUT CPU
- **Splined mixer**: 256 KB → 640 bytes (410x compression)
- **Power**: ~17 μW = **RF harvestable at 2.4 GHz**

See [docs/archive/SILICON_GRAIL.md](docs/archive/SILICON_GRAIL.md) for the full architecture.

### ETM Fabric: Hardware Neural Network

The ETM Fabric runs neural inference with **near-zero CPU involvement**:

- **PCNT** counts pulses = hardware addition
- **RMT** generates pulse trains = value encoding
- **Splined LUT** = 640 bytes (was 256 KB)
- **2 RMT calls** per inference (not 128!)

See [docs/archive/ETM_FABRIC_CFC.md](docs/archive/ETM_FABRIC_CFC.md) for details.

### Holographic Intelligence: Distributed Brain

Multiple nodes form a mesh that thinks through **interference patterns**:

- Three $5 chips = one emergent mind
- Neurons **crystallize** when neighbors agree
- Fault-tolerant: remove a node, others compensate
- 6.9 kHz tick rate with 64 neurons per node

See [docs/archive/HOLOGRAPHIC_INTELLIGENCE.md](docs/archive/HOLOGRAPHIC_INTELLIGENCE.md) for details.

---

## THE SUMMIT: Zero External Dependencies (Feb 1, 2026)

**We stripped everything.** The Reflex now runs with zero libc, zero ESP-IDF HAL.

| What We Stripped | Bare Metal Replacement |
|------------------|------------------------|
| `esp_cpu.h` | Direct CSR 0x7e2 read |
| `driver/gpio.h` | Direct GPIO registers (0x60091000) |
| `stdio.h` / `printf` | Direct USB Serial JTAG (0x6000F000) |

### Bare Metal Results

```
================================================================
                    THE SUMMIT ACHIEVED                         
================================================================

  This binary uses ZERO libc functions.
  This binary uses ZERO ESP-IDF HAL functions.

  +-----------------------------------------+
  |  PURE DECISION LATENCY: 12 NANOSECONDS  |
  +-----------------------------------------+

  Baseline:        34 cy = 212 ns
  No GPIO:         11 cy =  68 ns  
  Pure decision:    2 cy =  12 ns   <-- THE MONEY
  With channel:    73 cy = 456 ns
  
  Adversarial (100K samples, interrupts ON):
    Avg: 200 ns | Min: 68 ns | Max: 5.6 μs
================================================================
```

### What Remains

- `<stdint.h>` - types only, no functions
- `<stdbool.h>` - types only, no functions  
- ESP-IDF bootloader - to be replaced in Phase 4

---

## Quick Start

### Prerequisites

- ESP-IDF v5.x installed
- ESP32-C6 DevKitC-1 (or compatible)
- USB-C cable

### Build and Flash

```bash
# Source ESP-IDF environment
source ~/esp/esp-idf/export.sh

# Navigate to reflex-os
cd /path/to/the-reflex/reflex-os

# Build (spine_summit.c is the default - zero dependencies)
idf.py build

# Flash and monitor
idf.py -p /dev/ttyACM0 flash monitor
```

### Expected Output

```
============================================================
     THE REFLEX BECOMES THE C6 - ESP32-C6 @ 160MHz
============================================================

Every peripheral is a channel. Hardware already thinks in signals.

=== PRIMITIVE BENCHMARKS ===

gpio_write() latency (direct register):
  min=2 cycles (12 ns)
  avg=2 cycles (12 ns)

reflex_signal() latency:
  min=19 cycles (118 ns)
  avg=19 cycles (118 ns)

=== ENTROPY FIELD (THE VOID) ===

Entropy as structure. The space between shapes IS information.
...
```

---

## Architecture

See [docs/archive/ARCHITECTURE.md](docs/archive/ARCHITECTURE.md) for full details.

### The Channel Model

Everything is a channel:
- **GPIO pins** → input/output channels (12ns latency)
- **ADC inputs** → analog-to-digital channels (21us)
- **Timers** → periodic signal generators (10kHz)
- **SPI/I2C** → protocol channels (bidirectional)
- **WiFi** → network event channels
- **Entropy field** → computational substrate for TriX echips

### The Void

The space between signals carries information:
- **Silence** → entropy accumulates
- **Entropy gradients** → direction of information flow
- **Crystallization** → void becomes shape when threshold exceeded
- **Stigmergy** → indirect communication through the field

---

## File Structure

```
reflex-os/
├── include/
│   ├── reflex.h           # Core primitive - direct CSR 0x7e2
│   ├── reflex_c6.h        # Master header - includes all
│   ├── reflex_gpio.h      # GPIO as channels (12ns) - direct registers
│   ├── reflex_uart.h      # USB Serial JTAG (bare metal printf)
│   ├── reflex_timer.h     # Timer as channels (10kHz)
│   ├── reflex_adc.h       # ADC as channels (21us)
│   ├── reflex_spline.h    # Catmull-Rom interpolation (137ns)
│   ├── reflex_spi.h       # SPI as channels (29us)
│   ├── reflex_wifi.h      # WiFi as channels
│   ├── reflex_void.h      # Entropy field for echips
│   ├── reflex_echip.h     # Self-composing processor (echip)
│   ├── reflex_obsbot.h    # OBSBOT PTZ camera control (Linux)
│   │
│   │   # === THE SILICON GRAIL (Bare Metal, Zero ESP-IDF) ===
│   ├── reflex_etm.h             # ETM crossbar (50 channels)
│   ├── reflex_gdma.h            # GDMA M2M to peripheral space!
│   ├── reflex_pcnt.h            # Pulse counter (4 units)
│   ├── reflex_rmt.h             # Pulse generation
│   ├── reflex_timer_hw.h        # Hardware timers
│   ├── reflex_turing_complete.h # Turing Complete fabric
│   ├── reflex_spline_mixer.h    # 410x compressed LUTs (640 B!)
│   └── reflex_spline_verify.h   # Accuracy verification
├── main/
│   ├── spine_summit.c     # THE SUMMIT - zero dependencies (default)
│   ├── spine_bare.c       # Bare metal with printf (transitional)
│   ├── spine_main.c       # Pure spine CNS demo (ESP-IDF)
│   └── main.c             # Full benchmark suite
├── shared/
│   └── channels.h         # Shared channel definitions
├── tools/                 # Linux host tools (Pi4, Thor)
│   ├── obsbot_test.c      # OBSBOT camera test utility
│   ├── stereo_demo.c      # Synchronized stereo vision
│   └── Makefile           # Build for Linux hosts
├── docs/
│   ├── ARCHITECTURE.md    # System architecture
│   ├── API.md             # API reference
│   └── BENCHMARKS.md      # Detailed performance data
└── README.md              # This file
```

---

## API Overview

### Core Primitive (`reflex.h`)

```c
// Signal: write value and increment sequence
void reflex_signal(reflex_channel_t* ch, uint32_t value);

// Wait: spin until sequence changes
uint32_t reflex_wait(reflex_channel_t* ch, uint32_t last_seq);

// Read: get current value
uint32_t reflex_read(reflex_channel_t* ch);
```

### GPIO Channels (`reflex_gpio.h`)

```c
void gpio_set_output(uint8_t pin);
void gpio_write(uint8_t pin, bool value);  // 12ns
void gpio_toggle(uint8_t pin);
bool gpio_read(uint8_t pin);
```

### Spline Channels (`reflex_spline.h`)

```c
void spline_signal(reflex_spline_channel_t* sp, int32_t value);
int32_t spline_read(reflex_spline_channel_t* sp);   // 137ns
int32_t spline_velocity(reflex_spline_channel_t* sp);
int32_t spline_predict(reflex_spline_channel_t* sp, uint32_t future_cycles);
```

### Entropy Field (`reflex_void.h`)

```c
void entropy_deposit(field, x, y, amount);  // Stigmergy write
uint32_t entropy_read(field, x, y);         // Stigmergy read
void entropy_field_tick(field);             // One tick of computation
int8_t stigmergy_follow(field, x, y, toward_high);  // Follow gradient
```

See [docs/API.md](docs/API.md) for complete reference.

---

## The Vision: Self-Reconfiguring Soft Processor

Reflex OS includes a complete **self-composing, addressable intelligence** in `reflex_echip.h`:

```
┌─────────────────────────────────────────────────────────────────┐
│              THE CHIP THAT WATCHES ITSELF THINK                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   ~4,000 FROZEN SHAPES (the nouns)                             │
│   NAND │ LATCH │ MUX │ ADD │ NEURON │ OSCILLATOR │ ...         │
│                                                                 │
│   ~15,000 MUTABLE ROUTES (the verbs)                           │
│   • Strengthen with use (Hebbian learning)                     │
│   • Dissolve when unused (pruning)                             │
│   • Spawn from void (crystallization)                          │
│                                                                 │
│   ENTROPY FIELD (the grammar)                                  │
│   • Tracks silence and activity                                │
│   • High entropy → new shapes crystallize                      │
│   • The chip literally GROWS new circuits                      │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Self-Composition Behaviors

| Behavior | Mechanism | Effect |
|----------|-----------|--------|
| Hebbian Learning | Correlated firing → weight increase | Routes that work get stronger |
| Pruning | Low activity → dissolution | Unused routes return to void |
| Crystallization | Critical entropy → new shape | Void becomes structure |
| Growth | All of the above | The chip evolves its own circuitry |

This is not simulation. This is a chip that rewires itself based on what it computes.

---

## Linux Host Tools: The Cathedral's Eyes

Reflex OS includes tools that run on Linux hosts (Pi4, Thor) to control external hardware:

### OBSBOT PTZ Camera Control

```bash
cd tools && make
./obsbot_test /dev/video0 --sweep    # Pan/tilt sweep test
./obsbot_test /dev/video0 --demo     # Interactive arrow-key control
./obsbot_test /dev/video0 --latency  # Measure command latency
```

**Performance:** 121 µs average PTZ command latency via V4L2 ioctl.

### Synchronized Stereo Vision

```bash
./stereo_demo /dev/video0 /dev/video2   # Control both eyes together
```

- Keepalive threads prevent USB suspend
- Arrow keys move both cameras in sync
- Vergence support for depth-based attention

### Entropy-Driven Gaze

The cameras can track the entropy field's attention spotlight:

```c
obsbot_stereo_t eyes;
obsbot_stereo_init(&eyes, &left, &right, 150);  // 150mm baseline

// Cameras physically turn to look where entropy is lowest
obsbot_track_entropy(&eyes, &entropy_field);
```

Low entropy = certainty = **LOOK AT THIS**.

---

## Related Documentation

- [Pulse Arithmetic Engine](docs/archive/PULSE_ARITHMETIC_ENGINE.md) - **5679 Hz inference, equilibrium propagation learning**
- [The Silicon Grail](docs/archive/SILICON_GRAIL.md) - Turing Complete ETM Fabric
- [ETM Fabric CfC](docs/archive/ETM_FABRIC_CFC.md) - 551 Hz hardware neural network
- [Holographic Intelligence](docs/archive/HOLOGRAPHIC_INTELLIGENCE.md) - Distributed mesh brain
- [Lincoln Manifold: Reflex Becomes C6](../docs/LINCOLN_MANIFOLD_REFLEX_BECOMES_C6_SYNTH.md) - Design synthesis
- [The Reflex (main)](../README.md) - Original 926ns P99 on Jetson Thor
- [Architecture](docs/archive/ARCHITECTURE.md) - Detailed system design
- [API Reference](docs/API.md) - Complete API documentation

---

## License

MIT License

---

**The hardware is already a channel machine. We're just speaking its language.**

*12ns GPIO. 118ns signals. 137ns splines. Entropy IS the computation.*
