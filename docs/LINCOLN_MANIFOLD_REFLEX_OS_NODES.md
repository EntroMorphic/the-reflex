# Nodes of Interest: The Reflex as OS for ESP32-C6

> Phase 2: Identify the grain. Find where the wood wants to split.

---

## Node 1: Asymmetric Cores as Feature

The ESP32-C6's HP+LP split is not a limitation—it's the natural architecture for real-time systems.

- HP = Data plane (control loop, sensors, actuators)
- LP = Control plane (comms, logging, configuration)

**Why it matters:** This matches the Reflex philosophy. Don't fight the hardware; use it.

---

## Node 2: OS = Coordination

Traditional OS responsibilities on microcontroller:

| Responsibility | Needed on ESP32-C6? |
|----------------|---------------------|
| Virtual memory | No (flat address space) |
| Process isolation | No (single application) |
| Scheduling | **Only if multiple tasks per core** |
| IPC | **Yes - this is The Reflex** |
| I/O abstraction | Minimal (memory-mapped) |

**Insight:** If each core runs one loop, scheduling disappears. IPC IS the OS.

---

## Node 3: The FreeRTOS Entanglement

ESP-IDF assumes FreeRTOS. WiFi, BLE, and most drivers are written for FreeRTOS primitives.

**Tension:** Want bare metal for determinism, but need WiFi stack.

**Options:**
1. Pure bare metal - lose WiFi
2. FreeRTOS on LP core only - hybrid
3. Polling-based WiFi - may not exist
4. Different chip with simpler WiFi (ESP-NOW only?)

---

## Node 4: Interrupt Segregation

Real-time means no surprise latency. Interrupts are surprise latency.

**Pattern:** All interrupts → LP core. HP core polls channels only.

**Implementation question:** Can ESP32-C6 route all interrupts to LP core? Is this configurable?

---

## Node 5: The Boot Sequence

Clean startup matters. Race conditions at boot are common bugs.

**Proposed sequence:**
1. LP core starts first (hardware default?)
2. LP initializes peripherals, clocks, WiFi
3. LP signals "ready" on channel
4. HP core starts, spins on "ready" channel
5. HP receives signal, begins control loop

**Question:** Which core boots first on C6? Can we control this?

---

## Node 6: Memory Architecture

Both cores must see the same memory for channels to work.

**Unknowns:**
- Is HP-LP memory shared or separate?
- Cache coherency between HP and LP?
- Memory barriers needed?

**Critical:** If memory isn't coherent, The Reflex doesn't work.

---

## Node 7: The 100-Line OS

Minimum viable Reflex OS:

```
reflex_os.h:
  - Channel structure (8 bytes? 32 bytes?)
  - SIGNAL macro (write + barrier)
  - WAIT macro (spin + read)
  - WAIT_TIMEOUT variant

boot.c:
  - Minimal startup (clock, pins)
  - Channel initialization
  - Core launch

hp_main.c:
  - Infinite loop
  - Control law
  - Channel I/O

lp_main.c:
  - Infinite loop
  - Comms handling
  - Channel I/O
```

**Total:** Maybe 200-300 lines for a working system.

---

## Node 8: Debugging Without Printf

HP core can't block on UART. Debug output must be non-blocking.

**Solution:** Debug channel. HP writes to ring buffer. LP drains to UART.

**Implication:** printf-style debugging works, just async. Need tooling support.

---

## Node 9: Peripheral Ownership

SPI, I2C, GPIO - who owns what?

**Clean model:** Static assignment.
- HP owns: Sensor SPI, Actuator PWM
- LP owns: UART, WiFi, Status LED

No sharing. No arbitration. No locks.

---

## Node 10: The Hybrid Option

If pure bare metal is too hard (WiFi stack), consider:

**Hybrid architecture:**
- LP core: FreeRTOS (for WiFi/BLE drivers)
- HP core: Bare metal (pure Reflex loop)
- Interface: Reflex channels between them

This loses some purity but gains practicality. The HP core is still fully deterministic.

---

## Node 11: What Latency to Expect

Thor achieved 926ns P99 at 2.6GHz with cache coherency.

ESP32-C6 at 160MHz:
- 16x slower clock
- But: no OS, no cache hierarchy, direct memory
- Estimate: 100-500ns for channel signal?

**Must measure.** This is the key metric.

---

## Node 12: The First Demo

Minimal proof of concept:

1. HP core: Toggle GPIO at 10kHz, count cycles
2. LP core: Print count to UART every second
3. Coordination: HP signals "count ready", LP reads and prints

Success = 10kHz stable, latency measurable, no glitches.

---

## Node 13: Comparison to Existing Approaches

| Approach | Latency | Complexity |
|----------|---------|------------|
| FreeRTOS tasks | ~1-10μs | High (scheduler) |
| FreeRTOS queues | ~1-10μs | Medium |
| Direct shared memory | ~100ns | Low (but fragile) |
| **Reflex channels** | ~100-500ns | Low (and robust) |

Reflex adds structure to bare shared memory without adding overhead.

---

## Node 14: RISC-V Memory Model

ESP32-C6 is RISC-V. Memory ordering matters.

RISC-V has relaxed memory model. Need `fence` instruction for ordering.

```c
#define REFLEX_BARRIER() __asm__ volatile("fence rw, rw" ::: "memory")
```

**Must verify:** Is this the right fence for C6?

---

## Node 15: Why Not Just Use ESP32-S3?

S3 has symmetric dual cores. Seems cleaner for Reflex demo.

**Counter-argument:** C6's asymmetry is MORE interesting. It proves Reflex works across heterogeneous cores. And the LP core's low-power capability enables duty-cycled applications.

C6 is the harder demo. If it works here, it works anywhere.

---

*End of NODES phase. 15 nodes identified. Key tensions: FreeRTOS entanglement, memory architecture unknowns, peripheral ownership.*
