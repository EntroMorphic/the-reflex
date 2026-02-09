# C6 Actual Topology

> What actually runs on the ESP32-C6, not what we claim.
>
> Created: 2026-01-24

---

## The Honest Assessment

The current implementation is a **single sense-act-update loop** with lookup tables. It is not a network of reflexors, not an entropy field, not emergent behavior.

---

## Hardware Layer

```
ESP32-C6 @ 160MHz (single core RISC-V)

OUTPUTS (8):
  GPIO 0, 1, 2, 3, 4, 5, 6, 7

INPUTS:
  GPIO: 10, 11, 14, 15, 18, 19, 20, 21 (8 pins)
  ADC:  channels 0, 1, 2, 3 (4 channels)
  TEMP: internal sensor (1)

SPECIAL:
  GPIO 8:  LED (output)
  GPIO 9:  BOOT button (input, external agent)
```

---

## Memory Layout

```
embodied_state_t (~2KB)
├── forward_model_t
│   ├── gpio_pred[8][2][8]    = 256 bytes (int16)
│   ├── adc_pred[8][2][4]     = 128 bytes (int16)
│   ├── temp_pred[8][2]       = 32 bytes (int16)
│   └── confidence[8][2]      = 16 bytes (uint8)
│
├── backward_model_t
│   ├── gpio_credit[8][8]     = 128 bytes (int16)
│   ├── adc_credit[8][4]      = 64 bytes (int16)
│   └── temp_credit[8]        = 16 bytes (int16)
│
├── memory_buffer_t
│   └── entries[16]           = ~800 bytes (ring buffer)
│
├── explore_entropy_t
│   ├── gpio_entropy[8][8]    = 128 bytes (uint16)
│   ├── adc_entropy[8][4]     = 64 bytes (uint16)
│   ├── output_entropy[8]     = 16 bytes (uint16)
│   └── total_entropy         = 4 bytes (uint32)
│
└── relationships[32]         = ~512 bytes
```

---

## The Loop

```c
while (1) {
    // 1. Check button (external agent)
    if (button_changed) record_external_agent();

    // 2. Pick output with highest entropy
    output_idx = max(entropy.output_entropy[0..7]);

    // 3. Read all inputs (before)
    read_gpio(), read_adc(), read_temp();

    // 4. Toggle output
    gpio_write(output_pin, !current_state);

    // 5. Wait 1ms for physical effects
    delay_us(1000);

    // 6. Read all inputs (after)
    read_gpio(), read_adc(), read_temp();

    // 7. Compute deltas
    delta = after - before;

    // 8. Update memory (ring buffer push)
    memory_push(action, observation);

    // 9. Update forward model (EMA)
    prediction[output][state] = 0.9 * old + 0.1 * delta;

    // 10. Update backward model (credit)
    for recent_actions: credit[action] += weighted_delta;

    // 11. Decrease entropy for this output
    if (entropy[output] > 100) entropy[output] -= 100;

    // 12. Check crystallization
    if (abs(prediction) > threshold && confidence > min)
        add_relationship();

    // 13. Sleep 100ms
    vTaskDelay(100ms);
}
```

**Loop rate:** 10 Hz (100ms per tick)

---

## Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│   entropy[8]                                                    │
│       │                                                         │
│       │ argmax                                                  │
│       ▼                                                         │
│   ┌───────┐     ┌───────────┐     ┌───────────┐                │
│   │SELECT │────►│GPIO WRITE │────►│ PHYSICAL  │                │
│   │OUTPUT │     │(toggle)   │     │ WORLD     │                │
│   └───────┘     └───────────┘     └─────┬─────┘                │
│                                         │                       │
│                                         ▼                       │
│                                   ┌───────────┐                 │
│                                   │READ INPUTS│                 │
│                                   │gpio,adc,  │                 │
│                                   │temp       │                 │
│                                   └─────┬─────┘                 │
│                                         │                       │
│                                         ▼                       │
│                                   ┌───────────┐                 │
│                                   │  DELTA    │                 │
│                                   │after-before│                │
│                                   └─────┬─────┘                 │
│                                         │                       │
│         ┌───────────────┬───────────────┼───────────────┐       │
│         ▼               ▼               ▼               ▼       │
│   ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐    │
│   │ MEMORY   │   │ FORWARD  │   │ BACKWARD │   │ ENTROPY  │    │
│   │ push()   │   │ update() │   │ credit() │   │ decay()  │    │
│   └──────────┘   └────┬─────┘   └──────────┘   └──────────┘    │
│                       │                                         │
│                       ▼                                         │
│                 ┌──────────┐                                    │
│                 │CRYSTALLIZE│                                   │
│                 │threshold? │                                   │
│                 └─────┬─────┘                                   │
│                       │                                         │
│                       ▼                                         │
│                 ┌──────────┐                                    │
│                 │RELATIONS │                                    │
│                 │ [32]     │                                    │
│                 └──────────┘                                    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## What This IS

1. **A sense-act-update loop** - classic control theory
2. **Table-driven predictions** - lookup, not inference
3. **Exponential moving average** - simple smoothing
4. **Threshold-based detection** - fixed rules
5. **Deterministic algorithm** - same inputs → same outputs

---

## What This is NOT

| Claimed Architecture | Actual Implementation |
|---------------------|----------------------|
| Network of reflexors | Single monolithic loop |
| Entropy field (geometric manifold) | Array of 8 integers |
| Stillness/disturbance dynamics | Not implemented |
| Online observer (Delta Observer) | Not implemented |
| Latent space encoding | Not implemented |
| Emergent behavior | Fixed algorithm |
| Self-directed exploration | Programmed selection rule |
| Navigation through frozen landscape | Table lookup |

---

## Known Bugs (as of 2026-01-24)

1. **Entropy collapses to flat**: All outputs reach 100, exploration stops diversifying
2. **Tie-breaking is deterministic**: When entropies equal, always picks GPIO 0
3. **Toggle stuck on HIGH**: Pin read returns 0, so !0 = HIGH always
4. **Temperature sensor reads low**: 12°C seems wrong for running chip
5. **No actual discovery**: The "discovered" relationship was noise

---

## Falsification Status

**Claim:** "The C6 discovers its own body through self-directed exploration."

**Status:** FALSIFIED (implementation failure)

**Evidence:**
- Exploration degenerates to fixed behavior (GPIO 0, HIGH, repeat)
- No diversity in output selection after entropy equalizes
- No relationships discovered that survive scrutiny
- The system cannot notice it's stuck

---

## Lines of Code

```
embody_main.c:     504 lines
reflex_embody.h:   381 lines
--------------------------
Total:             885 lines
```

No reflexors. No channels. No entropy fields.
Just a loop with arrays.

---

## Next Steps

This document exists to be honest about what we built vs what we claimed.

Before proceeding, we must decide:
1. Is the loop architecture fundamentally limited?
2. Should we implement actual reflexors?
3. Or is the loop sufficient if fixed properly?

The trace output revealed the failure. Now we understand the topology.

---

*"The first step to fixing a system is knowing what the system actually is."*
