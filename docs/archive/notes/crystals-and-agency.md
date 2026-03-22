# Crystals and Agency in The Reflex

## What The Reflex Is

The Reflex is an ESP32-C6 microcontroller that **doesn't know its own wiring**. GPIO output pins are connected to ADC input pins with physical wires, but the firmware has no idea which output connects to which input. It must discover this through exploration.

## The Exploration Loop

Runs ~100 times/second:
1. **Act**: Toggle a GPIO pin (HIGH or LOW)
2. **Observe**: Read all ADC inputs and measure the change (delta)
3. **Learn**: If toggling GPIO X causes a big delta on ADC Y, strengthen that association

## The Three Layers

Three "brains" with different memory spans vote on which GPIO to explore next:

| Layer | Memory | Role |
|-------|--------|------|
| **Slow** | Long | Remembers distant past, provides stability |
| **Medium** | Medium | Balances exploration and exploitation |
| **Fast** | Short | Reacts quickly to recent discoveries |

When layers agree, confidence rises. When they disagree, the system keeps exploring.

## What Crystals Are

A **crystal** is a piece of knowledge that has become certain enough to be **permanently saved** to flash memory (NVS).

When the system repeatedly observes that "GPIO 0 → ADC 0 with delta ~2650", and confidence crosses a threshold, it **crystallizes** this knowledge:

```
Crystal 0: GPIO 0 → ADC 0, delta=2650, direction=positive
```

**Crystals persist across power loss and resets.** When the C6 reboots, it loads its crystals and already "knows" part of its wiring - it doesn't have to relearn from scratch.

## Applications of Crystals

### 1. Prediction

Once crystallized, the system can *predict* what it will observe before acting:
- "If I toggle GPIO 3 HIGH, ADC 3 should jump by ~2650"
- Compare prediction vs reality to detect anomalies

### 2. Anomaly Detection / Self-Diagnosis

```
Expected: GPIO 2 → ADC 2, delta=2650
Observed: delta=0
Conclusion: Wire disconnected or hardware fault
```

The system can detect its own damage or environmental changes.

### 3. Intentional Action

- Without crystals: "I'll randomly toggle GPIOs and see what happens"
- With crystals: "I want to affect ADC 3, so I'll toggle GPIO 3"

This is the foundation for **agency** - goal-directed behavior rather than random exploration.

### 4. Behavioral Primitives

Crystals become building blocks for higher-level behaviors:
- Crystal: GPIO 0 → ADC 0
- Primitive: `affect_adc0()` → toggle GPIO 0
- Behavior: Chain primitives into sequences

### 5. Confidence as Currency

The crystallization *process* generalizes - any pattern that crosses a confidence threshold gets promoted to permanent knowledge. This could apply to:
- Temporal patterns ("every 100 ticks, X happens")
- Multi-step causality ("A then B causes C")
- Environmental regularities

### 6. Transfer / Inheritance

Crystals could be:
- Exported to another device ("here's what I learned about this wiring harness")
- Used as a "genome" - inherited knowledge for new instances

## The Agency Connection

The system boots with: `AGENCY: No active goal (exploration mode)`

Crystals enable a transition from exploration to agency:

| Phase | Mode | Crystal Role |
|-------|------|--------------|
| 1 | Exploration | Discovering and forming crystals |
| 2 | Agency | Using crystals to achieve goals |

**Once you know your body, you can use your body intentionally.**

## Why It Matters

This is **embodied self-discovery** - the system learns its own physical structure through interaction, not programming. The crystals represent **persistent experiential knowledge** - memories that survive death.

It's a minimal proof of concept: hardware that wakes up ignorant of itself and, through reflexive action, comes to know its own body.
