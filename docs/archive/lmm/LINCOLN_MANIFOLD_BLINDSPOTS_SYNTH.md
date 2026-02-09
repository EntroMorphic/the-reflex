# Lincoln Manifold: The Reflex Blindspots

> Phase 4: SYNTH — The clean cut
>
> One imperative. One demo. Everything else follows.

---

## The Imperative

**Control something real.**

Until The Reflex closes a control loop with physics, it's a toy. After it does, every other concern (tests, docs, scaling, power) can be addressed from a position of proof.

---

## The Demo: Motor Position Control

### Why This Demo

| Option | Complexity | Proof Value | Hardware Cost |
|--------|-----------|-------------|---------------|
| Balancing robot | High | Very High | ~$50-100 |
| Drone attitude | Very High | Very High | ~$200+ |
| Motor position | Low | High | ~$20 |
| Servo sweep | Very Low | Low | ~$5 |

Motor position control is the sweet spot:
- Simple enough to debug
- Complex enough to be meaningful
- Requires real-time response (encoder pulses don't wait)
- PID control is well-understood baseline
- Failure is visible (motor doesn't reach target)

### Hardware

```
ESP32-C6
    ├── GPIO_OUT → Motor driver IN1
    ├── GPIO_OUT → Motor driver IN2
    ├── GPIO_PWM → Motor driver ENA (speed control)
    ├── GPIO_IN  → Encoder A (interrupt or polling)
    └── GPIO_IN  → Encoder B (interrupt or polling)

Motor: DC motor with quadrature encoder (~$10-15)
Driver: L298N or TB6612 (~$5)
Power: 12V supply for motor, USB for C6
```

### Software Architecture

```c
// Channels
reflex_channel_t encoder_ch;      // Position from encoder
reflex_channel_t setpoint_ch;     // Desired position
reflex_channel_t output_ch;       // PWM duty cycle
reflex_channel_t error_ch;        // For monitoring

// The control loop (10kHz target)
void control_loop(void) {
    reflex_timer_channel_t timer;
    timer_channel_init(&timer, 0, 0, 100);  // 100us = 10kHz

    int32_t position = 0;
    int32_t setpoint = 0;
    int32_t integral = 0;
    int32_t last_error = 0;

    for (;;) {
        timer_wait(&timer);

        // Read
        position = reflex_read(&encoder_ch);
        setpoint = reflex_read(&setpoint_ch);

        // PID
        int32_t error = setpoint - position;
        integral += error;
        int32_t derivative = error - last_error;
        last_error = error;

        int32_t output = (KP * error + KI * integral + KD * derivative) >> 10;
        output = clamp(output, -1000, 1000);

        // Write
        reflex_signal(&output_ch, output);
        reflex_signal(&error_ch, error);

        // Actuate
        motor_set_pwm(output);
    }
}
```

### Success Criteria

| Metric | Target | How to Measure |
|--------|--------|----------------|
| Loop frequency | 10kHz ± 1% | Scope on debug GPIO |
| Position tracking | ±10 encoder counts | Scope or serial log |
| Step response | <100ms to settle | Scope |
| Steady-state error | <5 counts | Serial log |
| CPU headroom | >50% idle | Cycle counter |

### What This Proves

1. **The Reflex can close a real control loop** — not synthetic, not simulated
2. **Timing is stable under load** — encoder interrupts + PID + PWM
3. **The channel abstraction works for real I/O** — encoder → channel → controller → channel → actuator
4. **Performance is competitive** — if it can't hit 10kHz PID, the architecture is flawed

---

## Secondary Deliverables

After the motor demo works, address in order:

### 1. Test Suite (P1)
```
tests/
├── test_channel.c      — unit tests for reflex_signal/wait/read
├── test_spline.c       — unit tests for interpolation
├── test_entropy.c      — unit tests for field operations
├── test_echip.c        — unit tests for shape/route logic
└── Makefile            — `make test` runs all
```

### 2. Quickstart Guide (P1)
```markdown
# QUICKSTART.md

## Prerequisites
- ESP32-C6 dev board
- ESP-IDF v5.x installed

## Build & Flash
git clone ...
cd reflex-os
idf.py build flash monitor

## Expected Output
[benchmark results]
[LED blinking at 0.5Hz]

## Next Steps
- See examples/motor_control/ for a real control loop
- See ARCHITECTURE.md for how it works
```

### 3. Multi-Core Test (P1)
```c
// Test on Raspberry Pi 4 or similar
// Multiple threads writing to different channels
// One thread writing, multiple reading same channel
// Verify no torn reads, no sequence gaps
```

### 4. Failure Mode Documentation (P2)
```markdown
# FAILURE_MODES.md

## Memory Allocation Failure
- `entropy_field_init()` returns false
- Caller must check and handle

## Channel Corruption
- Detected by sequence number gaps
- Mitigation: CRC in flags field (optional)

## Numerical Overflow
- Spline values clamp to INT32_MAX/MIN
- Weights saturate at ±WEIGHT_MAX

...
```

### 5. Power-Aware Wait (P2)
```c
// Hybrid wait: spin briefly, then interrupt-sleep
uint32_t reflex_wait_power(reflex_channel_t* ch,
                            uint32_t last_seq,
                            uint32_t spin_cycles,
                            uint32_t timeout_ms);
```

### 6. MQTT Bridge (P3)
```c
// Publish channel values to MQTT broker
// Subscribe to topics and signal channels
reflex_mqtt_publish(&mqtt, "motor/position", &position_ch);
reflex_mqtt_subscribe(&mqtt, "motor/setpoint", &setpoint_ch);
```

---

## Timeline

| Week | Focus | Deliverable |
|------|-------|-------------|
| 1 | Motor demo hardware | Wiring, encoder test |
| 1 | Motor demo software | Basic PID running |
| 2 | Motor demo tuning | Hit success criteria |
| 2 | Test suite | `make test` works |
| 3 | Quickstart + docs | Onboarding path exists |
| 3 | Multi-core test | Verified on RPi4 |
| 4 | Skeptic PRD items | Jitter fix, benchmarks |
| 4+ | P2/P3 items | Power, bridges |

---

## Hardware Shopping List

| Item | Example | Price |
|------|---------|-------|
| DC motor with encoder | JGA25-370 | ~$12 |
| Motor driver | TB6612FNG | ~$5 |
| 12V power supply | Any 12V 2A | ~$8 |
| Jumper wires | M-F kit | ~$5 |
| **Total** | | **~$30** |

Note: You may already have some of this.

---

## The Thesis

The Reflex is currently a claim. The motor demo makes it a proof.

Every synthetic benchmark, every architecture diagram, every philosophical framework is just words until physics validates them.

One motor. One encoder. One control loop.

The rest follows.

---

*"The wood cuts itself when you understand the grain."*

The grain says: **control something real.**

---

## Appendix: Encoder Reading Options

### Option A: Interrupt-Based (Higher Accuracy)
```c
// ISR increments/decrements counter
// Channel signals new position on each edge
// Pro: Never miss a pulse
// Con: Interrupts on hot path (jitter)
```

### Option B: Polling-Based (Deterministic)
```c
// Poll encoder pins in control loop
// Decode quadrature in software
// Pro: Fully deterministic timing
// Con: Miss pulses if loop is too slow
```

### Option C: Hardware Counter (Best)
```c
// Use PCNT (pulse counter) peripheral
// Hardware counts pulses, software reads register
// Pro: Never miss, no jitter
// Con: C6 PCNT setup is complex
```

Recommendation: Start with Option B (polling). It's simplest and proves the concept. Move to Option C if pulse rate exceeds loop rate.

---

*End of SYNTH phase. Build plan ready.*
