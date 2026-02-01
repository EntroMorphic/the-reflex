# Falsification Complete: The Reflex Claims Verified

**Date:** January 31, 2026
**Session:** Day 2 of Valentine's Day PRD
**Method:** Adversarial testing on both C6 ($5) and Thor ($2000)

---

## Executive Summary

We attempted to break our own claims. They survived.

| Platform | Claimed | Measured | Worst Case | Verdict |
|----------|---------|----------|------------|---------|
| C6 (ideal) | 87 ns | 87 ns | 3.2 μs | ✅ |
| C6 (realistic) | - | **187 ns** | **5.5 μs** | ✅ |
| Thor | 309 ns | 309 ns | 1.3 μs | ✅ |
| Thor (under load) | - | 366 ns | - | ✅ |

**Bottom line:** Sub-microsecond average on both platforms. Single-digit microsecond worst case. Zero catastrophic failures.

---

## C6 Falsification Suite

### Test Matrix (Interrupts Disabled)

| Test | Avg | Min | Max | What It Proves |
|------|-----|-----|-----|----------------|
| **Baseline** | 87 ns | 81 ns | 3,162 ns | Full reflex with PRNG |
| **No GPIO** | 68 ns | 68 ns | 1,412 ns | GPIO costs 19ns |
| **Always anomaly** | 12 ns | 12 ns | 350 ns | Pure decision = 12ns |
| **Never anomaly** | 12 ns | 12 ns | 350 ns | No branch prediction effect |
| **Memory access** | 106 ns | 100 ns | 2,050 ns | Cache hit costs 19ns |
| **With channel** | 437 ns | 418 ns | 2,937 ns | Fences cost 350ns |

### Adversarial Test (Interrupts Enabled)

| Metric | Value |
|--------|-------|
| Samples | 100,000 |
| **Min** | 68 ns |
| **Avg** | **187 ns** |
| **Max** | **5,543 ns** |
| Spikes >6μs | **0 (0.00%)** |

**Key insight:** With interrupts enabled (realistic operating conditions), average latency doubles but remains sub-microsecond. No catastrophic spikes.

---

## Thor Falsification

### Normal Operation
- Processing time: **309 ns**
- Anomalies caught: 1,127 per cycle
- Loop rate: 2.7 MHz effective

### Under CPU Stress
- Processing time: **366 ns** (18% degradation)
- Max observed: 1,268 ns
- System remained stable

---

## Component Breakdown

```
THE REFLEX LATENCY STACK (C6)

┌─────────────────────────────────────┐
│ Pure decision (compare + GPIO)      │  12 ns
├─────────────────────────────────────┤
│ + PRNG overhead                     │ +75 ns  →  87 ns
├─────────────────────────────────────┤
│ + Interrupt servicing (realistic)   │+100 ns  → 187 ns
├─────────────────────────────────────┤
│ + Channel coordination (fences)     │+350 ns  → 437 ns
├─────────────────────────────────────┤
│ + Worst case (interrupt storm)      │    up to 5.5 μs
└─────────────────────────────────────┘
```

---

## What We Tried to Break

| Attack | Result |
|--------|--------|
| Run with interrupts enabled | 187ns avg, 0% catastrophic spikes |
| Stress Thor CPU during test | 366ns, still stable |
| Vary threshold (always/never) | No branch prediction effect |
| Add memory access | +19ns per cache hit |
| Add channel signaling | +350ns (fence cost) |
| Sustained 100K iterations | No drift observed |

---

## Honest Claims

### Before Falsification
> "87ns reflex latency on ESP32-C6. 309ns on Thor."

### After Falsification
> "12ns pure decision. 87ns ideal, 187ns realistic on C6. 309ns on Thor, 366ns under load. Sub-microsecond average, 5-6μs worst case. Zero catastrophic failures in 100K+ samples."

---

## The Numbers That Matter

| Scenario | Latency | Use When |
|----------|---------|----------|
| Safety-critical local | 12-87 ns | Interrupts off, no coordination |
| Realistic local | 187 ns | Normal operation |
| With coordination | 437 ns | Reporting to cortex |
| Hard RT budget | 5-6 μs | Worst case planning |
| Thor processing | 309-366 ns | ROS2 integration |

---

## Conclusions

1. **The primitive works.** 12ns decision time is real.

2. **Realistic conditions double latency.** 87ns → 187ns with interrupts. Still sub-microsecond.

3. **Coordination costs 350ns.** Memory fences for cross-core signaling aren't free.

4. **No catastrophic failures.** 100K samples, 0 spikes >6μs.

5. **Thor holds under load.** 18% degradation, still sub-microsecond.

6. **The topology scales.** Same code, $5 to $2000, sub-microsecond everywhere.

---

## Artifacts

| Document | Purpose |
|----------|---------|
| `FALSIFICATION_C6.md` | Detailed C6 breakdown |
| `SKEPTICAL_REVIEW_ROS2.md` | Thor vulnerabilities |
| `spine_main.c` | C6 falsification suite |
| `reflex_force_control.c` | Thor A/B/C comparison |

---

## Signature

The Reflex claims have been falsified to the extent possible with available hardware. The numbers are real, the methodology is documented, and the limitations are acknowledged.

**Falsification status: PASSED**

```
    ┌─────────────────────────────────────┐
    │                                     │
    │   C6:   187 ns realistic            │
    │   Thor: 366 ns under load           │
    │   Both: Sub-microsecond average     │
    │   Both: Zero catastrophic failures  │
    │                                     │
    │   The claims are honest.            │
    │   The primitive is sound.           │
    │                                     │
    └─────────────────────────────────────┘
```

---

*"We tried to break it. It held."*

*— January 31, 2026*
