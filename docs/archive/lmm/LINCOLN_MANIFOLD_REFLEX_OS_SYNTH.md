# Synthesis: The Reflex as OS for ESP32-C6

> Phase 4: The clean cut. The wood cuts itself.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      ESP32-C6 REFLEX OS                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│   HP Core (160MHz RISC-V)         LP Core (RISC-V)              │
│   ┌─────────────────────┐         ┌─────────────────────┐       │
│   │                     │         │                     │       │
│   │   hp_main()         │         │   lp_main()         │       │
│   │   ┌───────────────┐ │         │   ┌───────────────┐ │       │
│   │   │ while(1) {    │ │         │   │ while(1) {    │ │       │
│   │   │   wait(cmd)   │ │  ←────  │   │   signal(cmd) │ │       │
│   │   │   sense()     │ │         │   │   ...         │ │       │
│   │   │   compute()   │ │         │   │   wait(telem) │ │       │
│   │   │   actuate()   │ │  ────→  │   │   transmit()  │ │       │
│   │   │   signal(tel) │ │         │   │ }             │ │       │
│   │   │ }             │ │         │   └───────────────┘ │       │
│   │   └───────────────┘ │         │                     │       │
│   │                     │         │   [FreeRTOS OK]     │       │
│   │   [BARE METAL]      │         │                     │       │
│   └─────────────────────┘         └─────────────────────┘       │
│              │                              │                    │
│              └──────── Shared RAM ──────────┘                    │
│                    (Reflex Channels)                             │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Key Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| HP core runtime | Bare metal | Determinism, no scheduler jitter |
| LP core runtime | Minimal FreeRTOS | WiFi/BLE driver compatibility |
| Coordination | Reflex channels | Single primitive, proven pattern |
| Interrupts | LP only | HP never preempted |
| Peripherals | Static assignment | No runtime arbitration |
| Memory model | Explicit fences | RISC-V requires barriers |

---

## Implementation Specification

### File Structure

```
reflex-os/
├── include/
│   └── reflex.h              # Channel primitive (50 lines)
├── hp/
│   ├── hp_main.c             # HP core entry point
│   ├── control_loop.c        # Application control law
│   └── hp_drivers.c          # Sensor/actuator (bare metal)
├── lp/
│   ├── lp_main.c             # LP core entry point
│   ├── channel_service.c     # FreeRTOS task for channels
│   └── comms.c               # WiFi/BLE handlers
├── shared/
│   └── channels.c            # Channel definitions
└── CMakeLists.txt            # ESP-IDF build
```

### reflex.h - The Entire OS Primitive

```c
// reflex.h - The Reflex OS for ESP32-C6
// 50 lines. This is the OS.

#ifndef REFLEX_H
#define REFLEX_H

#include <stdint.h>

// Channel structure - 32-byte aligned for cache efficiency
typedef struct {
    volatile uint32_t sequence;      // Monotonic counter
    volatile uint32_t timestamp;     // Producer's timestamp (optional)
    volatile uint32_t value;         // Payload
    volatile uint32_t flags;         // Application-defined
    uint32_t _pad[4];                // Pad to 32 bytes
} __attribute__((aligned(32))) reflex_channel_t;

// RISC-V memory fence
#define REFLEX_FENCE() __asm__ volatile("fence rw, rw" ::: "memory")

// Get cycle count (RISC-V mcycle CSR)
static inline uint32_t reflex_cycles(void) {
    uint32_t cycles;
    __asm__ volatile("rdcycle %0" : "=r"(cycles));
    return cycles;
}

// Signal: Write value and increment sequence
static inline void reflex_signal(reflex_channel_t* ch, uint32_t val) {
    ch->value = val;
    ch->timestamp = reflex_cycles();
    REFLEX_FENCE();
    ch->sequence++;
    REFLEX_FENCE();
}

// Wait: Spin until sequence changes
static inline uint32_t reflex_wait(reflex_channel_t* ch, uint32_t last_seq) {
    while (ch->sequence == last_seq) {
        __asm__ volatile("nop");  // Reduce power, avoid memory stall
    }
    REFLEX_FENCE();
    return ch->sequence;
}

// Wait with timeout (returns 0 if timeout, else new sequence)
static inline uint32_t reflex_wait_timeout(reflex_channel_t* ch,
                                            uint32_t last_seq,
                                            uint32_t timeout_cycles) {
    uint32_t start = reflex_cycles();
    while (ch->sequence == last_seq) {
        if ((reflex_cycles() - start) > timeout_cycles) {
            return 0;  // Timeout
        }
        __asm__ volatile("nop");
    }
    REFLEX_FENCE();
    return ch->sequence;
}

#endif // REFLEX_H
```

### channels.c - Shared Channel Definitions

```c
// channels.c - All system channels defined here
// Placed in shared memory section

#include "reflex.h"

// Section attribute for shared memory (linker script places this)
#define SHARED_MEM __attribute__((section(".shared_ram")))

// Command channel: LP → HP
SHARED_MEM reflex_channel_t cmd_channel = {0};

// Telemetry channel: HP → LP
SHARED_MEM reflex_channel_t telem_channel = {0};

// Debug channel: HP → LP (ring buffer style)
SHARED_MEM reflex_channel_t debug_channel = {0};

// Error channel: HP → LP (priority)
SHARED_MEM reflex_channel_t error_channel = {0};

// Ack channel: LP → HP (for synchronization)
SHARED_MEM reflex_channel_t ack_channel = {0};
```

### hp_main.c - HP Core Entry Point

```c
// hp_main.c - HP Core: The real-time reflex arc

#include "reflex.h"

extern reflex_channel_t cmd_channel;
extern reflex_channel_t telem_channel;
extern reflex_channel_t ack_channel;

// Application-specific
extern void control_init(void);
extern void control_loop_iteration(uint32_t cmd);
extern uint32_t get_telemetry(void);

void hp_main(void) {
    // Minimal init (clocks, GPIO for sensors/actuators)
    control_init();

    // Wait for LP core to signal ready
    uint32_t seq = 0;
    seq = reflex_wait(&cmd_channel, seq);

    // Main control loop - runs forever
    uint32_t telem_seq = 0;
    while (1) {
        // Wait for command (or just new tick from LP)
        seq = reflex_wait(&cmd_channel, seq);
        uint32_t cmd = cmd_channel.value;

        // Execute control law
        control_loop_iteration(cmd);

        // Send telemetry
        uint32_t telem = get_telemetry();
        reflex_signal(&telem_channel, telem);
        telem_seq++;
    }
}
```

### lp_main.c - LP Core Entry Point

```c
// lp_main.c - LP Core: Comms and coordination

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "reflex.h"

extern reflex_channel_t cmd_channel;
extern reflex_channel_t telem_channel;

// High-priority task: service channels
void channel_task(void* arg) {
    uint32_t telem_seq = 0;
    uint32_t tick = 0;

    // Signal HP core to start
    reflex_signal(&cmd_channel, 0);

    while (1) {
        // Send tick/command to HP at 10kHz
        tick++;
        reflex_signal(&cmd_channel, tick);

        // Check for telemetry (non-blocking with timeout)
        uint32_t new_seq = reflex_wait_timeout(&telem_channel, telem_seq, 16000);
        if (new_seq != 0) {
            telem_seq = new_seq;
            uint32_t telem = telem_channel.value;
            // Queue for WiFi transmission
            send_telemetry_async(telem);
        }

        // 100μs delay for 10kHz
        vTaskDelay(1);  // Minimum FreeRTOS tick
    }
}

// Lower-priority task: WiFi
void wifi_task(void* arg) {
    wifi_init();
    while (1) {
        wifi_poll();
        vTaskDelay(10);
    }
}

void app_main(void) {
    // Start channel service at highest priority
    xTaskCreate(channel_task, "channels", 4096, NULL, 10, NULL);

    // Start WiFi at lower priority
    xTaskCreate(wifi_task, "wifi", 8192, NULL, 5, NULL);
}
```

---

## Build Configuration

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# HP core: minimal, no FreeRTOS
# LP core: FreeRTOS for WiFi

project(reflex_os)
```

### Linker Script Addition

```
/* Shared RAM section for channels */
.shared_ram (NOLOAD) :
{
    . = ALIGN(32);
    _shared_ram_start = .;
    *(.shared_ram)
    . = ALIGN(32);
    _shared_ram_end = .;
} > sram
```

---

## Success Criteria

### Phase 1: Proof of Concept

- [ ] HP core toggles GPIO at 10kHz
- [ ] LP core prints counter via UART
- [ ] Coordination via reflex channel works
- [ ] Measure HP↔LP latency: target <500ns

### Phase 2: Control Loop Demo

- [ ] HP core reads analog sensor
- [ ] HP core runs PID control
- [ ] HP core drives PWM actuator
- [ ] LP core logs telemetry
- [ ] Stable 10kHz operation

### Phase 3: Networked Demo

- [ ] LP core runs WiFi
- [ ] Telemetry streams over WiFi
- [ ] Commands received over WiFi
- [ ] HP core unaffected by network latency

---

## Expected Results

| Metric | FreeRTOS Baseline | Reflex OS | Improvement |
|--------|-------------------|-----------|-------------|
| HP→LP latency | ~1-5μs | ~100-300ns | 5-50x |
| Control jitter | ~10μs P99 | ~1μs P99 | 10x |
| Code size | ~100KB | ~10KB | 10x smaller |
| Complexity | Tasks, queues, semaphores | Channels only | Much simpler |

---

## Implementation Order

1. **Week 1: Channel Primitive**
   - Port reflex.h to ESP32-C6
   - Test memory barrier semantics
   - Verify shared memory access HP↔LP
   - Measure raw channel latency

2. **Week 2: Dual-Core Skeleton**
   - HP core bare metal startup
   - LP core minimal FreeRTOS
   - Basic channel communication
   - GPIO toggle demo

3. **Week 3: Control Loop**
   - Sensor integration (SPI/I2C)
   - PID controller
   - Actuator integration (PWM)
   - Latency characterization

4. **Week 4: Network Integration**
   - WiFi on LP core
   - Telemetry streaming
   - Remote command
   - Full system demo

---

## The Reflex OS in One Sentence

*Each core runs one loop, they talk through channels, and the hardware does the rest.*

---

## What Comes Next

This synthesis is the implementation spec. The next step is code.

Start with `reflex.h` on the Pi4, flash to the C6, measure the first channel latency. If that number is under 500ns, the architecture is validated.

The wood is ready. The axe is sharp.

**Cut.**

---

*"Give me six hours to chop down a tree, and I will spend the first four sharpening the axe."*

*We spent the four hours. Now we cut.*
