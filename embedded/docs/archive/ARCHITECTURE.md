# Reflex OS Architecture

**From silicon to shapes: the complete channel stack.**

---

## Overview

Reflex OS reconceptualizes the ESP32-C6 as a **channel machine**. Instead of treating peripherals as devices to be driven, we treat them as **signal sources and sinks** that naturally emit and consume channel events.

```
┌─────────────────────────────────────────────────────────────────┐
│                     THE THREE LAYERS                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   Layer 3: ENTROPY FIELD (reflex_void.h)                       │
│   ┌─────────────────────────────────────────────────────────┐  │
│   │  Shapes ◆◆◆     Voids ░░░     Gradients →→→             │  │
│   │  Computation IS entropy flow through the field          │  │
│   └─────────────────────────────────────────────────────────┘  │
│                            │                                    │
│   Layer 2: CONTINUOUS BRIDGE (reflex_spline.h)                 │
│   ┌─────────────────────────────────────────────────────────┐  │
│   │  Discrete signals ──→ Smooth trajectories               │  │
│   │  Catmull-Rom interpolation at 137ns per evaluation      │  │
│   └─────────────────────────────────────────────────────────┘  │
│                            │                                    │
│   Layer 1: HARDWARE CHANNELS (reflex_*.h)                      │
│   ┌─────────────────────────────────────────────────────────┐  │
│   │  GPIO │ Timer │ ADC │ SPI │ WiFi │ ...                  │  │
│   │  Every peripheral speaks the same language              │  │
│   └─────────────────────────────────────────────────────────┘  │
│                            │                                    │
│   Layer 0: THE PRIMITIVE (reflex.h)                            │
│   ┌─────────────────────────────────────────────────────────┐  │
│   │  reflex_channel_t: sequence, timestamp, value, flags    │  │
│   │  reflex_signal() / reflex_wait() / reflex_read()        │  │
│   │  50 lines. 118ns. The foundation.                       │  │
│   └─────────────────────────────────────────────────────────┘  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Layer 0: The Primitive

### reflex_channel_t

The fundamental coordination structure:

```c
typedef struct {
    volatile uint32_t sequence;   // Monotonic counter
    volatile uint32_t timestamp;  // Cycle count when written
    volatile uint32_t value;      // Primary payload
    volatile uint32_t flags;      // Application-defined
    uint32_t _pad[4];             // Pad to 32 bytes (cache alignment)
} __attribute__((aligned(32))) reflex_channel_t;
```

**Key properties:**
- 32-byte aligned for cache efficiency
- Sequence number provides **ordering** and **change detection**
- Timestamp captures **when** signal occurred (6.25ns resolution at 160MHz)
- Memory fences ensure **visibility** across contexts

### Core Operations

| Operation | Latency | Description |
|-----------|---------|-------------|
| `reflex_signal()` | 118ns | Write value, increment sequence, fence |
| `reflex_wait()` | varies | Spin until sequence changes |
| `reflex_try_wait()` | ~10ns | Non-blocking check |
| `reflex_read()` | ~6ns | Read current value |

### Why This Works

On single-core ESP32-C6, the memory fence ensures compiler ordering. On multi-core systems, cache coherency propagates writes. The primitive works on both because it relies on **hardware guarantees** rather than OS services.

---

## Layer 1: Hardware Channels

Each peripheral is wrapped as a channel with consistent semantics.

### GPIO Channels (`reflex_gpio.h`)

**Concept:** Pins are digital signal channels.

```
Physical World
      │
      ▼
   ┌──────┐
   │ Pin  │ ──→ gpio_read()  → reflex_channel_t
   │      │ ◀── gpio_write() ← reflex_channel_t
   └──────┘
      │
      ▼
Physical World
```

**Implementation:** Direct register access for minimum latency.

```c
#define GPIO_OUT_W1TS_REG  0x60091008  // Write 1 to set
#define GPIO_OUT_W1TC_REG  0x6009100C  // Write 1 to clear

static inline void gpio_write(uint8_t pin, bool value) {
    if (value) {
        REG_WRITE(GPIO_OUT_W1TS_REG, 1 << pin);
    } else {
        REG_WRITE(GPIO_OUT_W1TC_REG, 1 << pin);
    }
}
```

**Performance:** 12ns (2 cycles) per write.

### Timer Channels (`reflex_timer.h`)

**Concept:** Timers are periodic signal generators.

```
         ┌───────────────────────────────────────┐
         │            Timer Channel              │
         │                                       │
Time ───►│  ┌──┐  ┌──┐  ┌──┐  ┌──┐  ┌──┐       │───► Periodic signals
         │  └──┘  └──┘  └──┘  └──┘  └──┘       │
         │  100us periods (10kHz)               │
         └───────────────────────────────────────┘
```

**Implementation:** Cycle counter with period tracking.

```c
typedef struct {
    uint32_t period_cycles;  // Cycles per period
    uint32_t last_cycles;    // Last trigger time
    uint64_t count;          // Signal count
} reflex_timer_channel_t;
```

**Performance:** 10kHz deterministic loops with <1% jitter.

### ADC Channels (`reflex_adc.h`)

**Concept:** Analog inputs are continuous-to-discrete converters.

```
Analog World (continuous)
         │
         ▼
   ┌──────────┐
   │   ADC    │ ──→ 12-bit digital value
   │ Channel  │     (reflex_channel_t)
   └──────────┘
         │
    21us per sample
```

**Performance:** 21us per 12-bit conversion (ESP-IDF oneshot driver).

### SPI Channels (`reflex_spi.h`)

**Concept:** SPI is a bidirectional channel pair.

```
   ┌─────────────────────────────────────────┐
   │            SPI Channel Pair             │
   │                                         │
   │  TX Channel ──────►  ┌──────┐           │
   │                      │ SPI  │           │
   │  RX Channel ◄──────  │Device│           │
   │                      └──────┘           │
   │  Status Channel (busy/idle)             │
   └─────────────────────────────────────────┘
```

**Performance:** 29us per byte at 1MHz.

### WiFi Channels (`reflex_wifi.h`)

**Concept:** Network events are channel signals.

```
   ┌─────────────────────────────────────────┐
   │            WiFi Channel                 │
   │                                         │
   │  Status ──► IDLE → CONNECTING → GOT_IP │
   │                                         │
   │  Events signal state transitions        │
   │  Packets become channel data            │
   └─────────────────────────────────────────┘
```

---

## Layer 2: The Spline Bridge

### The Problem

Physical reality is continuous. Signals are discrete. How do we bridge them?

### The Solution

**Spline channels** that interpolate between control points.

```
Discrete Signals:     ●         ●         ●         ●
                      │         │         │         │
                      └────┐    │    ┌────┘    ┌────┘
                           │    │    │         │
Continuous Curve:    ──────╲───┴────╱─────────╱────────
                        Catmull-Rom interpolation
```

### Implementation

```c
typedef struct {
    int32_t values[4];          // Control points (circular buffer)
    uint32_t times[4];          // Timestamps
    uint8_t head, count;
    reflex_channel_t base;
} reflex_spline_channel_t;
```

**Key insight:** You signal control points. You read smooth values.

### Operations

| Operation | Latency | Description |
|-----------|---------|-------------|
| `spline_signal()` | ~120ns | Add control point |
| `spline_read()` | 137ns | Interpolated value at current time |
| `spline_velocity()` | ~80ns | Current rate of change |
| `spline_predict()` | ~150ns | Extrapolate future value |

### Use Cases

- **Motor trajectories:** Signal waypoints, read smooth positions
- **Sensor filtering:** Signal samples, read filtered values
- **Animation:** Signal keyframes, read interpolated states

---

## Layer 3: The Entropy Field

### The Core Insight

**Shapes are information. Time is information. The space between shapes is ALSO information.**

### Entropy as Structure

| Concept | Representation | Meaning |
|---------|----------------|---------|
| Shape | Low entropy | Structure, certainty |
| Void | High entropy | Potential, possibility |
| Gradient | Entropy difference | Direction of flow |
| Crystallization | Entropy → 0 | Spontaneous structure |

### The Entropic Channel

Standard channels track **signals**. Entropic channels also track **silence**.

```c
typedef struct {
    reflex_channel_t base;      // Standard channel
    uint32_t last_signal_time;  // When last signaled
    uint32_t entropy;           // Accumulated silence
    uint32_t capacity;          // Crystallization threshold
    uint8_t state;              // EMPTY → CHARGING → CRITICAL → SHAPE
} reflex_entropic_channel_t;
```

**Key insight:** Silence accumulates entropy. Signals collapse it.

### The Entropy Field

A 2D grid of void cells where computation happens through diffusion.

```c
typedef struct {
    reflex_void_cell_t* cells;  // Grid of cells
    uint16_t width, height;
    uint32_t tick;
    uint16_t diffusion_rate;    // How fast entropy spreads
    uint16_t decay_rate;        // How fast entropy dissipates
} reflex_entropy_field_t;
```

### Field Evolution

Each tick of `entropy_field_tick()`:

1. **Decay:** Entropy slowly dissipates
2. **Diffusion:** Entropy spreads to neighbors
3. **Gradient:** Flow direction computed
4. **Crystallization:** Critical cells become shapes

```
Tick 0:           Tick 5:           Tick 10:
░░░░░░░░         ░░..░░░░         ░.....░░
░░@@░░░░   →     ░..@@..░   →     ░..@@..░
░░@@░░░░         ░..@@..░         ░..@@..░
░░░░░░░░         ░░..░░░░         ░.....░░

@ = high entropy deposit
. = diffused entropy
░ = empty void
```

### Stigmergy

**Indirect communication through the environment.**

```c
// Write: leave a trace
stigmergy_write(&field, x, y, amount);

// Read: sense the environment
stigmergy_sense_t sense = stigmergy_read(&field, x, y);

// Follow: move along gradient
int8_t direction = stigmergy_follow(&field, x, y, toward_high);
```

---

## Layer 4: The Self-Reconfiguring Processor

The complete TriX echip implementation: `reflex_echip.h`

### Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│               SELF-RECONFIGURING SOFT PROCESSOR                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   FROZEN SHAPES (the nouns)                                    │
│   ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐       │
│   │ NAND │ │LATCH │ │ MUX  │ │ ADD  │ │NEURON│ │ OSC  │       │
│   └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘       │
│      │        │        │        │        │        │             │
│   ───┴────────┴────────┴────────┴────────┴────────┴───         │
│                    MUTABLE ROUTES (the verbs)                   │
│   • Carry signals between shapes                                │
│   • Weights strengthen with use (Hebbian)                       │
│   • Dissolve back to void when unused                          │
│   ────────────────────────────────────────────────────         │
│                            │                                    │
│   ┌────────────────────────▼───────────────────────────────┐   │
│   │              ENTROPY FIELD (the grammar)                │   │
│   │                                                         │   │
│   │   ░░░░░░░░    Tracks route usage                       │   │
│   │   ░░##░░░░    # = active shape                         │   │
│   │   ░░..░░░░    . = used route (low entropy)             │   │
│   │   ░░░░░░░░    ░ = void (accumulating entropy)          │   │
│   │                                                         │   │
│   │   High entropy voids → crystallize into new shapes     │   │
│   │   Unused routes → dissolve back to void                │   │
│   └─────────────────────────────────────────────────────────┘   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Shape Types

| Category | Types | Purpose |
|----------|-------|---------|
| Logic | NAND, NOR, XOR, NOT, BUFFER | Universal computation |
| Memory | LATCH, TOGGLE | State storage |
| Arithmetic | ADD, SUB, MUL, CMP | Numeric operations |
| Routing | MUX, DEMUX, FANOUT | Signal steering |
| Interface | INPUT, OUTPUT | External I/O |
| Special | NEURON, OSCILLATOR | Adaptive and temporal |

### Self-Composition Behaviors

1. **Hebbian Learning**: Routes that carry correlated signals strengthen
2. **Activity Decay**: Unused routes gradually weaken
3. **Pruning**: Routes below activity threshold dissolve to void
4. **Crystallization**: High-entropy voids spawn new shapes
5. **Growth**: The chip literally evolves new circuits

### The Tick Cycle

Each `echip_tick()`:
1. Propagate signals through active routes
2. Evaluate shape logic (NAND, ADD, NEURON, etc.)
3. Apply Hebbian weight updates
4. Update entropy field (every N ticks)
5. Check for crystallization (void → shape)
6. Prune weak routes (route → void)

### Capacity

| Configuration | Shapes | Routes | Memory |
|---------------|--------|--------|--------|
| Minimal | 64 | 128 | ~4 KB |
| Standard | 1,024 | 4,096 | ~60 KB |
| Maximum | 4,096 | 16,384 | ~240 KB |

---

## TriX echips: The Vision

### Shapes as Circuits

TriX builds **soft chips** from geometric shapes. With Reflex OS:

- **Vertices** → channel endpoints
- **Edges** → signal pathways
- **Faces** → computational regions
- **The void** → entropy field substrate

### Computation Model

```
Traditional:  Logic gates → Boolean operations → Output
TriX echip:   Shapes in field → Entropy flow → Emergent computation
```

### Properties

| Traditional Chip | TriX echip |
|------------------|------------|
| Fixed topology | Morphable geometry |
| Boolean logic | Continuous manifolds |
| Clock-driven | Event-driven (spikes) |
| Design-time | Runtime reconfigurable |

---

## Memory Map

### ESP32-C6 Resources

| Resource | Size | Usage |
|----------|------|-------|
| SRAM | 452 KB | Channels, entropy field, application |
| Flash | 4-16 MB | Code, data |
| RTC SRAM | 8 KB | Persistent state |

### Channel Memory

```
reflex_channel_t:           32 bytes
reflex_spline_channel_t:    64 bytes
reflex_entropic_channel_t:  48 bytes
reflex_void_cell_t:         16 bytes

8x8 entropy field:          1 KB
32x32 entropy field:        16 KB
64x64 entropy field:        64 KB
```

### Capacity Estimates

With 400KB available for application:
- ~12,000 standard channels
- ~6,000 spline channels
- ~25,000 void cells (158x158 field)
- ~5,000 spiking neurons (for neural network applications)

---

## Timing Model

### Clock Domains

| Domain | Frequency | Cycle Time | Use |
|--------|-----------|------------|-----|
| CPU | 160 MHz | 6.25 ns | All computation |
| APB | 80 MHz | 12.5 ns | Peripherals |
| XTAL | 40 MHz | 25 ns | Reference |

### Latency Budget

For 10kHz control loop (100us period):

```
┌────────────────────────────────────────────────────────────┐
│                    100 us budget                           │
├────────────────────────────────────────────────────────────┤
│ Read sensors    │ Compute │ Actuate │ Entropy tick │ Slack │
│     21us        │  10us   │   1us   │     5us      │ 63us  │
└────────────────────────────────────────────────────────────┘
```

Plenty of margin for complex reflexors and entropy field computation.

---

## Future Directions

### Not Yet Implemented

- **reflex_uart.h** - UART as channels
- **reflex_i2c.h** - I2C as channels
- **reflex_system.h** - HP/LP core coordination
- **LP core sentinel** - Low-power monitoring

### Planned Extensions

- **reflex_neuron.h** - Spiking neural network primitives
- **reflex_shape.h** - TriX shape primitives
- **reflex_usb.h** - USB-C bridge to Thor

---

## Design Principles

1. **Hardware is already a channel machine** - We're just exposing it
2. **Every peripheral speaks the same language** - Unified channel semantics
3. **Time is first-class** - Timestamps on everything
4. **Silence carries information** - Entropy from absence
5. **Computation is flow** - Gradients, not gates
6. **No RTOS on hot path** - Bare metal determinism (see below)

---

## RTOS Relationship

**Claim:** "No RTOS on hot path."

**Reality:** The Reflex uses ESP-IDF which includes FreeRTOS. Here's what that means:

### What IS Bare Metal (Hot Path)

These primitives have **zero RTOS dependency**:

| Primitive | File | RTOS Calls |
|-----------|------|------------|
| `reflex_signal()` | reflex.h | None |
| `reflex_wait()` | reflex.h | None |
| `reflex_read()` | reflex.h | None |
| `gpio_write()` | reflex_c6.h | None |
| `gpio_read()` | reflex_c6.h | None |
| `reflex_cycles()` | reflex_c6.h | None |
| `spline_read()` | reflex_spline.h | None |
| `entropy_deposit()` | reflex_void.h | None |
| `echip_tick()` | reflex_echip.h | None |

A control loop using only these primitives runs on bare metal with deterministic timing.

### What Uses FreeRTOS (Support Path)

| Component | Why | Impact |
|-----------|-----|--------|
| WiFi stack | ESP-IDF WiFi requires FreeRTOS | Don't call from hot path |
| `vTaskDelay()` in demos | Convenient for non-critical delays | Not used in production hot path |
| ADC driver | ESP-IDF ADC uses task blocking | ~21us latency, acceptable |
| SPI driver | ESP-IDF SPI transaction API | ~29us latency, acceptable |

### The Rule

```
┌─────────────────────────────────────────────────────────────────┐
│                        HOT PATH                                  │
│  for (;;) {                                                     │
│      timer_wait(&loop);        // Bare metal spin               │
│      val = reflex_read(&in);   // Bare metal read               │
│      out = compute(val);       // Your code                     │
│      reflex_signal(&out, val); // Bare metal write              │
│      gpio_write(PIN, bit);     // Bare metal GPIO               │
│  }                                                               │
│                                                                  │
│  DO NOT CALL: vTaskDelay, xQueueSend, WiFi APIs, printf         │
└─────────────────────────────────────────────────────────────────┘
```

### Why Not Pure Bare Metal?

We could eliminate FreeRTOS entirely, but:
- WiFi would require reimplementing the 802.11 stack (~100k lines)
- USB-Serial debug output uses FreeRTOS internally
- The benefit (slightly smaller binary) doesn't justify the cost

The hot path is bare metal. The support infrastructure uses FreeRTOS. This is the pragmatic choice.

---

## Prior Art

The Reflex builds on decades of research in concurrent systems, neural computation, and cellular automata. This section acknowledges the intellectual lineage and explains how The Reflex relates to these foundations.

### Communicating Sequential Processes (Hoare, 1978)

**Citation:** C.A.R. Hoare. "Communicating Sequential Processes." *Communications of the ACM*, 21(8):666-677, 1978.

**Relationship:** The Reflex channels are similar to CSP channels in that they provide a primitive for inter-process communication. *Differs because:* CSP channels are synchronous (sender blocks until receiver is ready); Reflex channels are asynchronous with sequence numbers for non-blocking coordination. CSP is a formal algebra; The Reflex is an implementation-first primitive optimized for sub-microsecond latency.

### Lamport's Work on Concurrent Systems

**Citations:**
- L. Lamport. "Time, Clocks, and the Ordering of Events in a Distributed System." *Communications of the ACM*, 21(7):558-565, 1978.
- L. Lamport. "The Part-Time Parliament." *ACM TOCS*, 16(2):133-169, 1998. (Paxos)

**Relationship:** The Reflex uses Lamport-style sequence numbers for ordering events without global clocks. The timestamp + sequence number pair in `reflex_channel_t` echoes Lamport's logical clock approach. *Differs because:* We operate on a single chip (or tightly coupled multi-core) where cache coherency provides strong ordering guarantees. We don't need consensus protocols—hardware memory fences suffice.

### Actor Model (Hewitt, 1973)

**Citation:** C. Hewitt, P. Bishop, and R. Steiger. "A Universal Modular ACTOR Formalism for Artificial Intelligence." *IJCAI*, 1973.

**Relationship:** The Reflex shapes (especially in echip) resemble actors: autonomous computational entities that receive messages and can create new actors. *Differs because:* Actor mailboxes are queues; Reflex channels hold only the latest value. Actor semantics are message-passing; Reflex semantics are signal/read with no message queuing. The Reflex emphasizes spatial embedding (entropy fields) which actors do not.

### Lock-Free Data Structures

**Citations:**
- M. Herlihy. "Wait-Free Synchronization." *ACM TOPLAS*, 13(1):124-149, 1991.
- M. Michael and M. Scott. "Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms." *PODC*, 1996.

**Relationship:** `reflex_channel_t` is a lock-free single-writer multi-reader structure. The sequence number provides linearizability without CAS loops. *Builds on:* The insight that single-writer structures can be made wait-free with careful memory ordering. *Differs because:* We trade off generality (no multi-writer support) for extreme simplicity (50 lines) and speed (118ns).

### Hebbian Learning (Hebb, 1949)

**Citation:** D.O. Hebb. *The Organization of Behavior*. Wiley, 1949.

**Relationship:** The echip's route strengthening implements Hebbian learning: "Neurons that fire together, wire together." Routes that carry correlated signals have their weights increased. *Builds on:* The foundational insight that learning is connection modification. *Differs because:* Traditional Hebbian learning operates on neural networks; The Reflex applies it to arbitrary computational graphs with mutable topology.

### Cellular Automata (von Neumann, Conway)

**Citations:**
- J. von Neumann. *Theory of Self-Reproducing Automata*. University of Illinois Press, 1966.
- M. Gardner. "Mathematical Games: The fantastic combinations of John Conway's new solitaire game 'Life'." *Scientific American*, 223:120-123, 1970.

**Relationship:** The entropy field is a cellular automaton: cells update based on neighbor states according to local rules (diffusion, decay, crystallization). *Builds on:* The demonstration that local rules can produce global computation. *Differs because:* Cellular automata have uniform rules everywhere; The entropy field hosts heterogeneous "shapes" (frozen structures) that break symmetry. The field is a substrate for computation, not the computation itself.

### Stigmergy (Grassé, 1959)

**Citation:** P.-P. Grassé. "La reconstruction du nid et les coordinations interindividuelles chez Bellicositermes natalensis et Cubitermes sp." *Insectes Sociaux*, 6:41-80, 1959.

**Relationship:** The entropy field enables stigmergic communication: indirect coordination through environmental modification. Agents "write" entropy deposits and "read" gradients, coordinating without direct communication. *Builds on:* Observations of termite mound construction. *Differs because:* We formalize stigmergy as a computational primitive with explicit APIs (`stigmergy_write`, `stigmergy_read`, `stigmergy_follow`).

### What's New in The Reflex

The Reflex's contribution is **not** in any single primitive, but in their **composition**:

1. **Channel-centric hardware abstraction**: Treating every peripheral as a signal source/sink
2. **Spline interpolation as a channel operation**: Bridging discrete signals to continuous trajectories
3. **Entropy as first-class data**: Silence accumulates; information flows uphill
4. **Mutable topology over entropy substrate**: Shapes freeze, routes dissolve, the field crystallizes
5. **Sub-microsecond latency without RTOS**: 118ns signals, 12ns GPIO, bare metal hot path

The novelty is not in the parts. The novelty is in treating them as a unified computational model that runs on a $5 microcontroller at 118ns per signal.

---

*The Reflex doesn't run on the C6. The Reflex IS how the C6 knows itself.*
