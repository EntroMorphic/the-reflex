# Synthesis: The Reflex Becomes the C6

> Phase 4: What do we build? Actionable specification.

---

## Vision

The ESP32-C6, understood as a channel machine. Every peripheral is a channel. The HP core runs reflexors. The LP core is the sentinel. No RTOS. Just signals.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        ESP32-C6 @ 160MHz                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────────────────┐    ┌─────────────────────────────┐ │
│  │       HP CORE           │    │         LP CORE              │ │
│  │      (160 MHz)          │    │         (20 MHz)             │ │
│  │                         │    │                              │ │
│  │  ┌─────────────────┐   │    │   ┌────────────────────┐    │ │
│  │  │   Fast Loop     │   │    │   │   Sentinel Loop    │    │ │
│  │  │                 │   │    │   │                    │    │ │
│  │  │ for(;;) {       │   │    │   │ for(;;) {          │    │ │
│  │  │   poll_fast();  │   │    │   │   poll_slow();     │    │ │
│  │  │   reflexors();  │   │    │   │   if(need_hp())    │    │ │
│  │  │   actuate();    │   │    │   │     wake_hp();     │    │ │
│  │  │ }               │   │    │   │ }                  │    │ │
│  │  └─────────────────┘   │    │   └────────────────────┘    │ │
│  │           │             │    │            │                 │ │
│  └───────────┼─────────────┘    └────────────┼─────────────────┘ │
│              │                               │                   │
│              └───────────┬───────────────────┘                   │
│                          │                                       │
│  ┌───────────────────────┴──────────────────────────────────────┐│
│  │                    SHARED CHANNELS                            ││
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐            ││
│  │  │HP_WAKE  │ │HP_SLEEP │ │DATA_REQ │ │DATA_RSP │  ...       ││
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘            ││
│  └───────────────────────────────────────────────────────────────┘│
│                          │                                       │
│  ┌───────────────────────┴──────────────────────────────────────┐│
│  │                    HARDWARE CHANNELS                          ││
│  │                                                               ││
│  │  ┌─────────────────────────────────────────────────────────┐ ││
│  │  │ GPIO[22]  │ ADC[7]  │ TIMER[4] │ SPI │ I2C │ UART │ ... │ ││
│  │  └─────────────────────────────────────────────────────────┘ ││
│  │                          │                                    ││
│  │          ┌───────────────┴────────────────┐                   ││
│  │          │        WORLD (pins)             │                   ││
│  │          └─────────────────────────────────┘                   ││
│  └───────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
```

---

## File Structure

```
reflex-os/
├── include/
│   ├── reflex.h              # Core primitives (existing)
│   ├── reflex_c6.h           # C6-specific hardware channels
│   ├── reflex_gpio.h         # GPIO as channels
│   ├── reflex_adc.h          # ADC as channels
│   ├── reflex_timer.h        # Timers as channels
│   ├── reflex_spi.h          # SPI as channels
│   ├── reflex_uart.h         # UART as channels
│   └── reflex_system.h       # HP/LP coordination, boot, sleep
├── src/
│   ├── hp_core.c             # HP core main loop
│   ├── lp_core.c             # LP core sentinel loop (if bare metal)
│   ├── gpio.c                # GPIO channel implementation
│   ├── adc.c                 # ADC channel implementation
│   ├── timer.c               # Timer channel implementation
│   └── boot.c                # Startup sequence
├── examples/
│   ├── blink/                # GPIO output channel demo
│   ├── adc_sensor/           # ADC input channel demo
│   ├── spi_device/           # SPI protocol channel demo
│   └── dual_core/            # HP/LP coordination demo
└── docs/
    ├── CHANNELS.md           # Hardware channel reference
    └── PATTERNS.md           # Common reflexor patterns
```

---

## Core Definitions

### reflex_c6.h - Hardware Channel Map

```c
#ifndef REFLEX_C6_H
#define REFLEX_C6_H

#include "reflex.h"
#include "soc/gpio_reg.h"
#include "soc/sens_reg.h"

// ============================================================
// GPIO CHANNELS (direct register access)
// ============================================================

// GPIO input - read external world
static inline uint32_t gpio_read_all(void) {
    return REG_READ(GPIO_IN_REG);
}

static inline bool gpio_read(uint8_t pin) {
    return (REG_READ(GPIO_IN_REG) >> pin) & 1;
}

// GPIO output - write to external world
static inline void gpio_write(uint8_t pin, bool value) {
    if (value) {
        REG_WRITE(GPIO_OUT_W1TS_REG, 1 << pin);
    } else {
        REG_WRITE(GPIO_OUT_W1TC_REG, 1 << pin);
    }
}

static inline void gpio_toggle(uint8_t pin) {
    REG_WRITE(GPIO_OUT_REG, REG_READ(GPIO_OUT_REG) ^ (1 << pin));
}

// ============================================================
// ADC CHANNELS
// ============================================================

// ADC channel - analog signal from world
// Returns 12-bit value (0-4095)
uint16_t adc_read(uint8_t channel);

// Non-blocking version - returns true if conversion complete
bool adc_try_read(uint8_t channel, uint16_t* value);

// ============================================================
// TIMER CHANNELS
// ============================================================

// Timer channel - periodic signal source
typedef struct {
    uint8_t timer_id;
    volatile uint32_t* count_reg;
    volatile uint32_t* alarm_reg;
} reflex_timer_t;

// Read current timer value
static inline uint32_t timer_read(reflex_timer_t* t) {
    return *t->count_reg;
}

// Wait for timer alarm (blocking)
void timer_wait(reflex_timer_t* t);

// Check if alarm fired (non-blocking)
bool timer_check(reflex_timer_t* t);

// ============================================================
// HP/LP COORDINATION CHANNELS
// ============================================================

// Shared memory channels for core coordination
extern reflex_channel_t hp_wake_channel;    // LP signals HP to wake
extern reflex_channel_t hp_sleep_channel;   // HP signals LP it's sleeping
extern reflex_channel_t hp_request_channel; // HP requests data from LP
extern reflex_channel_t hp_response_channel;// LP responds to HP

// Wake HP core from LP core
static inline void wake_hp(void) {
    reflex_signal(&hp_wake_channel, 1);
    // Hardware wake signal
    // (implementation depends on LP core setup)
}

// HP signals it's going to sleep
static inline void hp_signal_sleep(void) {
    reflex_signal(&hp_sleep_channel, 1);
}

// HP waits for wake signal
static inline void hp_wait_wake(void) {
    static uint32_t last_seq = 0;
    last_seq = reflex_wait(&hp_wake_channel, last_seq);
}

#endif // REFLEX_C6_H
```

---

## Implementation Phases

### Phase 1: GPIO Channels (Bare Metal)
**Goal**: Blink LED at 100kHz using GPIO as channel
**What we prove**: Direct register access as channel semantics

```c
// HP core loop
void hp_main(void) {
    gpio_set_output(LED_PIN);

    for (;;) {
        gpio_write(LED_PIN, 1);
        delay_cycles(800);  // 5us at 160MHz
        gpio_write(LED_PIN, 0);
        delay_cycles(800);
    }
}
```

### Phase 2: Timer Channels
**Goal**: Precise periodic signaling from hardware
**What we prove**: Hardware can be a channel producer

```c
// Timer signals at 10kHz
reflex_timer_t loop_timer;
timer_init(&loop_timer, TIMER_0, 100);  // 100us period

void hp_main(void) {
    for (;;) {
        timer_wait(&loop_timer);
        // Exactly 10kHz, hardware-timed
        do_control_loop();
    }
}
```

### Phase 3: ADC Channels
**Goal**: Read analog sensor as channel
**What we prove**: External signals become channel signals

```c
// ADC samples at 1kHz, processed in control loop
void hp_main(void) {
    for (;;) {
        timer_wait(&loop_timer);

        uint16_t sensor = adc_read(ADC_SENSOR_PIN);
        reflex_signal(&sensor_channel, sensor);

        // Reflexor processes sensor_channel
        uint16_t output = process_sensor(sensor);
        gpio_write(ACTUATOR_PIN, output > THRESHOLD);
    }
}
```

### Phase 4: HP/LP Coordination
**Goal**: LP handles UART, HP stays deterministic
**What we prove**: Asymmetric cores as control/data plane

```c
// LP core
void lp_main(void) {
    uart_init();

    for (;;) {
        // Check UART
        if (uart_available()) {
            uint8_t cmd = uart_read();
            reflex_signal(&command_channel, cmd);
            wake_hp();
        }

        // Check HP response
        if (reflex_check(&response_channel)) {
            uint32_t response = reflex_read(&response_channel);
            uart_write(response);
        }
    }
}

// HP core
void hp_main(void) {
    for (;;) {
        // Fast loop - uninterrupted
        for (int i = 0; i < 1000; i++) {
            timer_wait(&loop_timer);
            do_control_loop();
        }

        // Check for LP commands (non-blocking)
        if (reflex_check(&command_channel)) {
            uint8_t cmd = reflex_read(&command_channel);
            uint32_t result = process_command(cmd);
            reflex_signal(&response_channel, result);
        }
    }
}
```

### Phase 5: SPI Channels
**Goal**: SPI peripheral as bidirectional channel pair
**What we prove**: Protocol channels work

### Phase 6: WiFi Channels
**Goal**: Network packets as channel signals
**What we prove**: Complex stacks can be channelized
**Approach**: May need FreeRTOS on LP core just for WiFi stack

---

## Deliverables

| Phase | Output | Metric |
|-------|--------|--------|
| 1 | reflex_gpio.h | 100kHz toggle verified on scope |
| 2 | reflex_timer.h | 10kHz loop, <1% jitter |
| 3 | reflex_adc.h | 1kHz ADC reads in control loop |
| 4 | reflex_system.h | HP/LP coordination demo |
| 5 | reflex_spi.h | SPI device communication |
| 6 | reflex_wifi.h | MQTT publish/subscribe as channels |

---

## Success Criteria

1. **HP core never takes interrupts** - all interrupt handling on LP core
2. **Control loop jitter < 1%** - measured with scope
3. **Channel latency < 200ns** - already proven at 206ns
4. **LP sentinel wake time < 10us** - from LP detect to HP running
5. **Complete channel map** - every C6 peripheral documented as channels
6. **Zero RTOS on HP core** - bare metal only

---

## The Ultimate Demo

A robot running on ESP32-C6:
- Motor control at 10kHz on HP core
- Sensor fusion in reflexors
- WiFi telemetry via LP core
- OTA update without stopping motors
- All peripherals visible as channels
- Total firmware: < 1000 lines

The Reflex doesn't run on the robot. The Reflex IS how the robot thinks.

---

*End of SYNTH phase. Build plan ready.*
