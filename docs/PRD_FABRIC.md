# PRD: The Fabric Layer - Hardware Event-Task Matrix

**Date:** February 2, 2026
**Status:** Design Phase
**Goal:** Leverage the ESP32-C6's Event Task Matrix (ETM) for zero-CPU-intervention reflexes

---

## The Discovery

The ESP32-C6 has a hardware feature we haven't exploited: the **Event Task Matrix (ETM)**.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    ESP32-C6 EVENT TASK MATRIX                               │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   50 HARDWARE CHANNELS                                                      │
│   ────────────────────                                                      │
│                                                                             │
│   Each channel connects ONE event source to ONE task destination.           │
│   The connection happens in HARDWARE. The CPU never sees it.                │
│                                                                             │
│   EVENT SOURCES              ETM CHANNEL              TASK DESTINATIONS     │
│   ─────────────              ───────────              ─────────────────     │
│   GPIO edge (any pin)   ───►  Channel 0  ───────────► GPIO set/clr/toggle  │
│   Timer alarm           ───►  Channel 1  ───────────► Timer start/stop     │
│   ADC threshold         ───►  Channel 2  ───────────► DMA trigger          │
│   MCPWM event           ───►  Channel 3  ───────────► MCPWM action         │
│   I2S word count        ───►  Channel 4  ───────────► I2S action           │
│   PCNT threshold        ───►  Channel 5  ───────────► GPIO toggle          │
│   ...                   ───►  ...        ───────────► ...                  │
│   (50 channels total)                                                       │
│                                                                             │
│   LATENCY: ~1 CLOCK CYCLE (6.25 ns @ 160MHz)                               │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

**This is the fabric.** Hardware wiring between peripherals, no CPU in the loop.

---

## What ETM Can Do

### Event Sources (things that trigger)

| Peripheral | Events |
|------------|--------|
| GPIO | Rising edge, falling edge, any edge |
| GPTimer | Alarm match, overflow |
| MCPWM | Timer events, comparator events, fault events |
| PCNT | Threshold cross, zero cross |
| Systick | Tick event |
| Temperature | Threshold cross |
| ADC | Conversion done (via DMA) |

### Task Destinations (things that react)

| Peripheral | Tasks |
|------------|-------|
| GPIO | Set, clear, toggle |
| GPTimer | Start, stop, reload, capture |
| MCPWM | Start, stop, sync |
| DMA (GDMA) | Start transfer |
| LP Core | Wake up |

### The Key Insight

**Current Reflex:** CPU polls or interrupts → CPU decides → CPU acts

**ETM Reflex:** Hardware event → Hardware routes → Hardware acts

```
CURRENT (12ns with CPU):
   Event ──► Interrupt ──► ISR ──► Decision ──► Action
                │                      │
                └──── CPU INVOLVED ────┘

ETM FABRIC (~6ns without CPU):
   Event ──► ETM Channel ──► Action
                │
                └── NO CPU ──
```

---

## The Fabric Architecture

### Layer 1: Primitive Weaves

Single event → single task connections. The atoms of the fabric.

```c
// reflex_fabric.h - Direct ETM register access

#define ETM_BASE            0x60013000
#define ETM_CH_ENA_REG      (ETM_BASE + 0x00)    // Channel enable bitmap
#define ETM_CH0_EVT_ID      (ETM_BASE + 0x04)    // Channel 0 event ID
#define ETM_CH0_TASK_ID     (ETM_BASE + 0x08)    // Channel 0 task ID
// ... channels 1-49

// Event IDs (from TRM)
#define ETM_EVT_GPIO0_POS   1    // GPIO 0 positive edge
#define ETM_EVT_GPIO0_NEG   2    // GPIO 0 negative edge
#define ETM_EVT_TIMER0_ALM  48   // Timer 0 alarm
// ...

// Task IDs (from TRM)
#define ETM_TASK_GPIO0_SET  1    // Set GPIO 0
#define ETM_TASK_GPIO0_CLR  2    // Clear GPIO 0
#define ETM_TASK_GPIO0_TOG  3    // Toggle GPIO 0
// ...

// Weave: connect event to task via channel
static inline void fabric_weave(uint8_t channel, uint8_t event_id, uint8_t task_id) {
    volatile uint32_t* evt_reg = (volatile uint32_t*)(ETM_BASE + 0x04 + channel * 8);
    volatile uint32_t* task_reg = (volatile uint32_t*)(ETM_BASE + 0x08 + channel * 8);
    *evt_reg = event_id;
    *task_reg = task_id;
    *(volatile uint32_t*)ETM_CH_ENA_REG |= (1 << channel);
}
```

### Layer 2: Pattern Weaves

Multiple events → multiple tasks via multiple channels. Complex reflexes.

```c
// Example: Button debounce in hardware
// GPIO 9 (button) rising edge → start debounce timer
// Debounce timer alarm → read GPIO 9 → set GPIO 8 (LED) if still high

fabric_weave(0, ETM_EVT_GPIO9_POS, ETM_TASK_TIMER0_START);
fabric_weave(1, ETM_EVT_TIMER0_ALM, ETM_TASK_GPIO8_SET);
// Note: conditional logic requires CPU, but timing is all hardware
```

### Layer 3: The Loom

A higher-level abstraction for common patterns.

```c
// reflex_loom.h - Pattern templates

typedef struct {
    uint8_t trigger_gpio;
    uint8_t trigger_edge;    // POS, NEG, ANY
    uint8_t response_gpio;
    uint8_t response_action; // SET, CLR, TOG
} simple_reflex_t;

// Wire a simple GPIO-to-GPIO reflex
static inline void loom_simple_reflex(simple_reflex_t* r, uint8_t channel) {
    uint8_t evt = gpio_to_etm_event(r->trigger_gpio, r->trigger_edge);
    uint8_t task = gpio_to_etm_task(r->response_gpio, r->response_action);
    fabric_weave(channel, evt, task);
}

// Example usage:
simple_reflex_t button_led = {
    .trigger_gpio = 9,       // Boot button
    .trigger_edge = POS,     // Rising edge
    .response_gpio = 8,      // LED
    .response_action = TOG   // Toggle
};
loom_simple_reflex(&button_led, 0);
// Now button toggles LED in ~6ns, CPU never involved
```

---

## What This Enables

### 1. Sub-Cycle Reflexes

Current best: 12ns (2 cycles) with CPU
ETM fabric: ~6ns (1 cycle) without CPU

### 2. Deterministic Timing

No interrupt latency. No ISR jitter. The hardware path is fixed.

### 3. CPU-Free Operation

The CPU can sleep while reflexes run. Or do other work.

### 4. Composable Patterns

50 channels = 50 simultaneous hardware reflexes. They can chain:

```
GPIO edge → Timer start → Timer alarm → DMA transfer → GPIO toggle
```

All in hardware. CPU configures once, then exits.

---

## The Hierarchy

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           THE REFLEX STACK                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   BRAIN (Thor)           │  Plans, learns              │  milliseconds     │
│   ────────────────────────────────────────────────────────────────────────  │
│                                                                             │
│   SPINE (C6 CPU)         │  Complex reflexes           │  nanoseconds      │
│   ────────────────────────────────────────────────────────────────────────  │
│                                                                             │
│   FABRIC (C6 ETM)        │  Simple reflexes            │  ~1 cycle         │
│   ────────────────────────────────────────────────────────────────────────  │
│                                                                             │
│   SILICON                │  Physics                    │  picoseconds      │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

The Fabric is *below* the Spine. It's the spinal cord's spinal cord - the most primitive reflexes that don't even need the RISC-V core.

---

## Implementation Plan

### Phase 1: Map the ETM Registers

- [ ] Read TRM Chapter 29 (Event Task Matrix)
- [ ] Document all event IDs and task IDs
- [ ] Create `reflex_fabric.h` with direct register access

### Phase 2: Primitive Weaves

- [ ] Implement `fabric_weave(channel, event, task)`
- [ ] Test: GPIO edge → GPIO toggle (measure latency)
- [ ] Test: Timer alarm → GPIO set
- [ ] Test: Chain two channels

### Phase 3: The Loom

- [ ] Design pattern abstractions
- [ ] Implement common patterns (debounce, PWM sync, etc.)
- [ ] Measure latency vs CPU-based reflexes

### Phase 4: Integration

- [ ] Integrate with existing Reflex channel system
- [ ] CPU handles complex decisions, ETM handles fast reactions
- [ ] Document the hybrid model

---

## Questions

1. **ETM vs CPU Reflex:** When to use which?
   - ETM: Fixed patterns, maximum speed, CPU-free
   - CPU: Conditional logic, learning, adaptation

2. **Channel Allocation:** 50 channels is a lot, but how to manage?
   - Static allocation at boot?
   - Dynamic allocation with priority?

3. **Debugging:** ETM operates below visibility. How to observe?
   - Timestamp channels?
   - GPIO probes?

---

## The Vision

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                                                                             │
│   The Fabric is what happens when you go below the CPU.                    │
│                                                                             │
│   We already proved: the hardware coordinates via cache coherency.          │
│   We already proved: the hardware times via cycle counter.                  │
│   We already proved: the hardware signals via GPIO registers.               │
│                                                                             │
│   The ETM is one level deeper:                                             │
│   The hardware ROUTES events to tasks without the CPU.                     │
│                                                                             │
│   It's not just "the hardware is doing the work."                          │
│   It's "the hardware can wire ITSELF."                                     │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## References

- ESP32-C6 Technical Reference Manual, Chapter 29: Event Task Matrix
- ESP-IDF `esp_etm.h`, `gpio_etm.h`
- ETM base address: 0x60013000
- 50 channels available

---

*"The Fabric is the nervous system's nervous system."*
