# PRD: Pulse-Mode Matrix Multiplication Engine

> **Project:** The Reflex - Pulse Matmul
> **Version:** 1.0
> **Date:** February 2026
> **Status:** Proposed

---

## Executive Summary

Build a hardware-accelerated 4×4 ternary matrix multiplication engine on ESP32-C6 using I2S + PCNT peripherals. Values encoded as pulse counts, weights encoded as wiring + polarity. CPU-free computation at 2.1M MACs/sec with ~650μW power consumption.

---

## Problem Statement

### Current State
- Neural network inference on microcontrollers requires CPU cycles
- CPU compute is power-hungry (~20mW active)
- Inference blocks other tasks
- No hardware acceleration on low-cost MCUs

### Desired State
- Hardware-accelerated matrix multiplication
- Ultra-low power (<1mW)
- CPU free during computation
- Works on $2 microcontroller

### Gap
No existing solution provides hardware matmul on ESP32-class devices without dedicated accelerator silicon.

---

## Solution Overview

### Core Concept

**Pulse-mode computation:** Encode values as pulse counts, weights as routing, accumulation via hardware counters.

```
Input: [120, 45, 200, 80]     →  120 pulses, 45 pulses, 200 pulses, 80 pulses
Weights: {-1, 0, +1}         →  Wire routing + PCNT inc/dec mode
Output: PCNT counts          →  Dot product results
```

### Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    PULSE-MODE MATMUL ENGINE                             │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  INPUT VECTOR              WEIGHT MATRIX              OUTPUT VECTOR     │
│  [a, b, c, d]             W[4][4] ∈ {-1,0,+1}        [y0, y1, y2, y3]  │
│       │                         │                          ▲            │
│       ▼                         ▼                          │            │
│  ┌─────────┐              ┌──────────┐              ┌──────────┐       │
│  │ I2S+DMA │──┐           │ Physical │              │  PCNT    │       │
│  │ Pulse   │  │           │ Wiring + │──────────────│ Readout  │       │
│  │ Gen     │  │           │ Config   │              │          │       │
│  └─────────┘  │           └──────────┘              └──────────┘       │
│               │                 │                         ▲            │
│               ▼                 ▼                         │            │
│         ┌─────────────────────────────────────────────────┐            │
│         │  GPIO4 ──┬──────┬──────┬──────┐                 │            │
│         │  GPIO5 ──┼──┬───┼──┬───┼──┬───┼──┐              │            │
│         │  GPIO6 ──┼──┼───┼──┼───┼──┼───┼──┼──┐           │            │
│         │  GPIO7 ──┼──┼───┼──┼───┼──┼───┼──┼──┼──┐        │            │
│         │          ▼  ▼   ▼  ▼   ▼  ▼   ▼  ▼  ▼  ▼        │            │
│         │        ┌─────┐┌─────┐┌─────┐┌─────┐             │            │
│         │        │PCNT0││PCNT1││PCNT2││PCNT3│─────────────┘            │
│         │        └─────┘└─────┘└─────┘└─────┘                          │
│         │          y0     y1     y2     y3                             │
│         └─────────────────────────────────────────────────┘            │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Requirements

### Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| F1 | Perform 4×4 matrix-vector multiplication | P0 |
| F2 | Support ternary weights {-1, 0, +1} | P0 |
| F3 | Support 8-bit unsigned input values (0-255) | P0 |
| F4 | Produce 16-bit signed output values | P0 |
| F5 | Allow runtime weight reconfiguration | P1 |
| F6 | Support continuous streaming operation | P1 |
| F7 | Provide completion notification (interrupt/poll) | P1 |

### Non-Functional Requirements

| ID | Requirement | Target | Priority |
|----|-------------|--------|----------|
| N1 | Throughput | >100K matmuls/sec | P0 |
| N2 | Power consumption | <1 mW | P0 |
| N3 | CPU utilization during compute | <20% | P0 |
| N4 | Output accuracy | 100% vs CPU reference | P0 |
| N5 | Latency per matmul | <10 μs | P1 |
| N6 | Weight change time | <5 μs | P2 |

### Hardware Requirements

| Component | Quantity | Purpose |
|-----------|----------|---------|
| ESP32-C6 | 1 | Main processor |
| GPIO pins | 20 | I2S output (4) + PCNT input (16) |
| Jumper wires | 16 | Physical fan-out |
| Breadboard/PCB | 1 | Wiring substrate |

---

## Technical Design

### GPIO Allocation

| GPIO | Function | Direction |
|------|----------|-----------|
| 4 | I2S Data 0 (Input 0 pulses) | Output |
| 5 | I2S Data 1 (Input 1 pulses) | Output |
| 6 | I2S Data 2 (Input 2 pulses) | Output |
| 7 | I2S Data 3 (Input 3 pulses) | Output |
| 10-13 | PCNT0 inputs (in0-in3) | Input |
| 14-17 | PCNT1 inputs (in0-in3) | Input |
| 18-21 | PCNT2 inputs (in0-in3) | Input |
| 22-25 | PCNT3 inputs (in0-in3) | Input |

### Physical Wiring Matrix

```
I2S Output    PCNT0    PCNT1    PCNT2    PCNT3
GPIO4    ──────10───────14───────18───────22
GPIO5    ──────11───────15───────19───────23
GPIO6    ──────12───────16───────20───────24
GPIO7    ──────13───────17───────21───────25
```

### Weight Encoding

Each PCNT unit has 2 channels, each with positive and negative edge detection:

| Weight | PCNT Configuration |
|--------|-------------------|
| +1 | Edge action = INCREMENT |
| -1 | Edge action = DECREMENT |
| 0 | Edge action = HOLD (no count) |

### Pulse Encoding

Input value V (0-255) encoded as V consecutive high pulses:

```
Value = 5:   ▄▄▄▄▄_______________  (5 pulses)
Value = 200: ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄...▄  (200 pulses)
```

### Data Flow

```
1. CPU: Prepare DMA buffer with pulse patterns
   └─ generate_pulses([120, 45, 200, 80]) → dma_buffer[256]

2. CPU: Clear PCNT counters
   └─ pcnt_unit_clear_count(unit) × 4

3. CPU: Start I2S DMA transfer
   └─ i2s_channel_write(tx_chan, dma_buffer, 256, ...)

4. HARDWARE: I2S outputs pulses (CPU free)
   └─ 256 clock cycles @ 40MHz = 6.4μs

5. HARDWARE: PCNTs accumulate weighted sums
   └─ Each edge → increment or decrement based on weight

6. CPU: Read PCNT values
   └─ pcnt_unit_get_count(unit) × 4 → outputs[4]
```

---

## API Design

### Core Functions

```c
/**
 * Initialize the pulse matmul engine
 * Configures I2S, DMA, PCNT, and GPIO
 */
esp_err_t pulse_matmul_init(void);

/**
 * Set the 4×4 weight matrix
 * @param weights 4×4 array of {-1, 0, +1}
 */
esp_err_t pulse_matmul_set_weights(const int8_t weights[4][4]);

/**
 * Perform matrix-vector multiplication
 * @param inputs  4-element input vector (0-255)
 * @param outputs 4-element output vector (signed)
 */
esp_err_t pulse_matmul_compute(const uint8_t inputs[4], int16_t outputs[4]);

/**
 * Perform async computation (returns immediately)
 * @param inputs Input vector
 * @param callback Called when complete
 */
esp_err_t pulse_matmul_compute_async(const uint8_t inputs[4], 
                                      pulse_matmul_done_cb_t callback,
                                      void* user_data);

/**
 * Deinitialize and release resources
 */
esp_err_t pulse_matmul_deinit(void);
```

### Usage Example

```c
#include "pulse_matmul.h"

void app_main(void) {
    // Initialize
    pulse_matmul_init();
    
    // Set weights (ternary: -1, 0, +1)
    int8_t weights[4][4] = {
        { 1, -1,  1,  0},
        { 0,  1,  1, -1},
        { 1,  1,  0,  1},
        {-1,  0,  1,  1},
    };
    pulse_matmul_set_weights(weights);
    
    // Compute
    uint8_t inputs[4] = {120, 45, 200, 80};
    int16_t outputs[4];
    
    pulse_matmul_compute(inputs, outputs);
    
    printf("Output: [%d, %d, %d, %d]\n", 
           outputs[0], outputs[1], outputs[2], outputs[3]);
    
    // Expected: W × inputs
    // outputs[0] = 1*120 + (-1)*45 + 1*200 + 0*80 = 275
    // outputs[1] = 0*120 + 1*45 + 1*200 + (-1)*80 = 165
    // etc.
}
```

---

## Performance Targets

### Timing Budget

| Phase | Time | Notes |
|-------|------|-------|
| PCNT clear | 100 ns | 4 register writes |
| DMA buffer prep | 1 μs | CPU, can be precomputed |
| I2S transmission | 6.4 μs | 256 cycles @ 40MHz |
| PCNT readout | 100 ns | 4 register reads |
| **Total** | **~7.6 μs** | |

### Throughput

- **Matmuls/sec:** 131,000
- **MACs/sec:** 2.1 million (16 MACs per matmul)

### Power

| Component | Power |
|-----------|-------|
| I2S + DMA | 500 μW |
| PCNT (×4) | 100 μW |
| GPIO | 50 μW |
| **Total** | **~650 μW** |

### Comparison

| Metric | Pulse Mode | CPU Compute |
|--------|------------|-------------|
| Latency | 7.6 μs | 0.5 μs |
| Throughput | 131K/s | 500K/s |
| CPU usage | ~15% | 100% |
| Power | 650 μW | 20 mW |
| Energy/op | 5 nJ | 40 nJ |

---

## Implementation Plan

### Phase 1: Component Verification (2 days)

| Task | Success Criteria |
|------|------------------|
| Verify I2S parallel output | 4 GPIOs toggle simultaneously |
| Verify PCNT edge counting | Count matches pulse count |
| Measure max edge rate | >20 MHz confirmed |

### Phase 2: Single Channel PoC (2 days)

| Task | Success Criteria |
|------|------------------|
| I2S → GPIO → PCNT pipeline | Single input counted correctly |
| Timing measurement | <10 μs for 256 pulses |
| Inc/Dec mode test | Both polarities work |

### Phase 3: Full 4×4 Implementation (3 days)

| Task | Success Criteria |
|------|------------------|
| Physical fan-out wiring | All 16 connections working |
| 4 parallel PCNT accumulation | All 4 outputs correct |
| Weight configuration | Runtime weight changes work |
| End-to-end matmul test | 100% accuracy vs CPU |

### Phase 4: Optimization & Benchmarking (2 days)

| Task | Success Criteria |
|------|------------------|
| Throughput benchmark | >100K matmuls/sec |
| Power measurement | <1 mW |
| Double-buffering | Continuous streaming works |
| API finalization | Clean, documented API |

### Phase 5: Documentation & Release (1 day)

| Task | Deliverable |
|------|-------------|
| Code documentation | Doxygen comments |
| Usage guide | README with examples |
| Performance report | Benchmark results |

---

## Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| I2S doesn't support parallel output on C6 | Medium | High | Fall back to PARLIO + external fan-out |
| PCNT misses edges at high speed | Low | High | Reduce clock speed, add filtering |
| GPIO count insufficient | Low | Medium | Reduce to 3×3 matmul |
| Timing jitter affects accuracy | Medium | Medium | Add synchronization via ETM |

---

## Success Criteria

### Minimum Viable Product (MVP)

- [ ] 4×4 ternary matmul working
- [ ] 100% accuracy vs CPU reference
- [ ] >50K matmuls/sec throughput
- [ ] <2 mW power consumption

### Full Success

- [ ] All MVP criteria met
- [ ] >100K matmuls/sec throughput
- [ ] <1 mW power consumption
- [ ] <20% CPU utilization
- [ ] Async API with callback
- [ ] Double-buffered streaming mode

---

## Future Extensions

### Larger Matrices
- Tile 4×4 blocks for NxN matrices
- Software accumulation of partial results

### Multi-Chip Scaling
- Chain multiple ESP32-C6 via GPIO/SPI
- Linear throughput scaling

### Activation Functions
- Use PCNT thresholds + ETM for ReLU
- Hardware threshold = activation

### Binary Neural Networks
- Train BNN with ternary weights
- Deploy on pulse matmul engine
- Keyword spotting, gesture recognition

---

## Appendix

### A. PCNT Edge Mode Reference

```c
typedef enum {
    PCNT_CHANNEL_EDGE_ACTION_HOLD,      // Weight = 0
    PCNT_CHANNEL_EDGE_ACTION_INCREASE,  // Weight = +1
    PCNT_CHANNEL_EDGE_ACTION_DECREASE,  // Weight = -1
} pcnt_channel_edge_action_t;
```

### B. Pulse Pattern Generation

```c
void generate_pulses(const uint8_t inputs[4], uint8_t* buffer) {
    for (int t = 0; t < 256; t++) {
        uint8_t byte = 0;
        for (int i = 0; i < 4; i++) {
            if (t < inputs[i]) byte |= (1 << i);
        }
        buffer[t] = byte;
    }
}
```

### C. Mathematical Equivalence

```
Standard matmul:    y[i] = Σⱼ W[i][j] × x[j]

Pulse matmul:       y[i] = Σⱼ (pulses from x[j]) × (PCNT mode for W[i][j])
                        = Σⱼ x[j] × W[i][j]
                        = Standard matmul (for W ∈ {-1, 0, +1})
```

---

*"We found a neural accelerator hiding in the interrupt controller."*

**Document Version:** 1.0
**Last Updated:** February 2026
**Author:** The Reflex Project
