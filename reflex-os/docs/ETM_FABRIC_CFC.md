# ETM Fabric CfC: Hardware Neural Computation

**551 Hz inference. 64 neurons. Zero CPU in the loop.**

---

## Overview

The ETM (Event Task Matrix) Fabric CfC is a hardware neural network implementation that runs on the ESP32-C6 with **near-zero CPU involvement**. It achieves this by:

1. **PCNT as hardware adder** - Pulse counting IS addition
2. **RMT pulse generation** - Values become pulse trains
3. **LUT-based activation** - Sigmoid/tanh via memory reads
4. **Mixer LUT** - 256 KB table eliminates multiply operations

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         ETM FABRIC CfC                                  │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   INPUT ──► SPARSE DOT ──► ACTIVATION ──► MIXER ──► OUTPUT             │
│             (PCNT pulses)   (LUT read)    (LUT read)                   │
│                                                                         │
│   No multiply. No floating point. Just memory reads + pulse counting.  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Performance Progression

| Method | Latency | Rate | Speedup | Description |
|--------|---------|------|---------|-------------|
| Individual RMT | 22 ms | 44 Hz | 1.0x | One rmt_transmit per weight |
| Batched | 5.3 ms | 188 Hz | 4.3x | One rmt_transmit per sparse dot |
| **Pulse Train** | **1.8 ms** | **551 Hz** | **12.5x** | Two rmt_transmit per inference |

---

## Architecture

### The CfC Equation

Closed-form Continuous-time (CfC) networks use this update rule:

```
gate = sigmoid(W_gate · [input, hidden] + b_gate)
candidate = tanh(W_cand · [input, hidden] + b_cand)
h_new = (1 - gate) * h_prev * decay + gate * candidate
```

### Hardware Mapping

| Operation | Hardware | Method |
|-----------|----------|--------|
| Sparse dot product | PCNT | Count pulses = sum values |
| Sigmoid activation | Memory | 256-byte LUT |
| Tanh activation | Memory | 256-byte LUT |
| Mixer computation | Memory | 256 KB 3D LUT [gate][h_prev][cand] |

### The Key Insight: PCNT Counts Pulses

PCNT (Pulse Counter) is designed for encoders, but we use it as an **adder**:

```
Value 5 → Generate 5 pulses → PCNT counts 5
Value 3 → Generate 3 pulses → PCNT counts 8 (cumulative!)
Value 7 → Generate 7 pulses → PCNT counts 15

PCNT = Σ(values) = sparse dot product!
```

### The Mixer LUT

The mixer equation `h = (1-g)*h*decay + g*c` requires multiply. We eliminate it with a 3D lookup table:

```c
// 4-bit quantization: 16 × 16 × 16 = 4096 entries per neuron
// 64 neurons × 4096 bytes = 256 KB total
mixer_lut[gate][h_prev][candidate] = precomputed_result
```

---

## The Pulse Train Architecture

### The Problem

Each `rmt_transmit()` call has 23 μs of setup overhead. With 128 sparse dots:
- 128 × 23 μs = 2,944 μs wasted on setup alone

### The Solution

Batch ALL gates into ONE transmission, then ALL candidates into ONE transmission:

```
Phase 1: Gate Pulse Train
┌─────────────────────────────────────────────────────────────────────────┐
│ [N0 pulses] [marker] [N1 pulses] [marker] ... [N63 pulses] [end]       │
└─────────────────────────────────────────────────────────────────────────┘
                              ↓ ONE rmt_transmit()
              
Phase 2: Candidate Pulse Train  
┌─────────────────────────────────────────────────────────────────────────┐
│ [N0 pulses] [marker] [N1 pulses] [marker] ... [N63 pulses] [end]       │
└─────────────────────────────────────────────────────────────────────────┘
                              ↓ ONE rmt_transmit()
```

### Reconstructing Per-Neuron Sums

We track cumulative pulse counts at each marker:

```c
// pulse_counts[n] = cumulative pulses through neuron n
// Individual sum = pulse_counts[n] - pulse_counts[n-1] - 1  (marker = 1 pulse)
```

### Why 2 Calls, Not 1?

We need gate values BEFORE computing candidates (CfC equation dependency).
The mixer LUT requires both gate AND candidate values.

---

## Memory Layout

| Component | Size | Purpose |
|-----------|------|---------|
| Mixer LUTs | 262,144 bytes | 64 neurons × 4 KB each |
| Activation LUTs | 512 bytes | Sigmoid + tanh |
| Sparse weights | 8,448 bytes | Index arrays for ternary weights |
| Train buffer | 32,768 bytes | Pulse train for 64 neurons |
| Pulse counts | 256 bytes | Cumulative counts array |
| Hidden state | 64 bytes | 4-bit quantized |
| **Total** | **~304 KB** | Fits in C6's 512 KB SRAM |

---

## Sparse Ternary Weights

CfC uses sparse ternary weights: {-1, 0, +1}

With ~81% sparsity, each neuron has only ~12 nonzero weights.

```c
typedef struct {
    uint8_t pos_indices[32];  // Indices where weight = +1
    uint8_t neg_indices[32];  // Indices where weight = -1
    uint8_t pos_count;        // Number of +1 weights
    uint8_t neg_count;        // Number of -1 weights
} fabric_sparse_row_t;
```

### Hardware Execution

1. **Positive weights**: PCNT counts pulses for each +1 weight
2. **Negative weights**: Software sum (fast - just additions)
3. **Result**: pos_sum - neg_sum = sparse dot product

---

## 4-Bit Quantization

All values quantized to 4 bits (0-15) for LUT indexing:

```c
// Q15 to 4-bit
uint8_t q4 = ((q15 + 32768) >> 12) & 0x0F;

// 4-bit to float (for understanding)
float f = (q4 / 7.5f) - 1.0f;  // Maps to [-1, +1]
```

| 4-bit value | Float equivalent |
|-------------|------------------|
| 0 | -1.0 |
| 8 | 0.0 |
| 15 | +1.0 |

---

## API Reference

### Initialization

```c
#include "reflex_fabric_cfc.h"

fabric_engine_t base_fabric;
fabric_cfc_engine_t cfc_engine;

// Initialize base fabric (PCNT + RMT)
fabric_init(&base_fabric);

// Initialize CfC engine (allocates 256 KB for mixer LUTs)
fabric_cfc_init(&cfc_engine, &base_fabric);
```

### Inference

```c
uint8_t input_q4[4] = {8, 10, 6, 12};  // 4-bit quantized inputs

// Software reference (for verification)
fabric_cfc_step_sw(&cfc_engine, input_q4);

// Hardware - batched (188 Hz)
fabric_cfc_step_hw_batched(&cfc_engine, input_q4);

// Hardware - pulse train (551 Hz)
fabric_cfc_step_hw_train(&cfc_engine, input_q4);

// Read output
uint8_t* hidden = cfc_engine.hidden_q4;  // 64 values, 4-bit each
```

### Cleanup

```c
fabric_cfc_deinit(&cfc_engine);
```

---

## Verification

Software and hardware produce **100% identical results**:

```
VERIFICATION: SOFTWARE vs HARDWARE

  Results:
    Exact matches: 64 / 64 (100.0%)
    Total difference: 0
    Avg difference: 0.00

  First 8 neurons:
    SW:  2  7  5  7 12  7  6 13 
    HW:  2  7  5  7 12  7  6 13 
```

---

## Timing Breakdown

From actual hardware measurements:

| Operation | Time | % of Total |
|-----------|------|------------|
| RMT setup+wait | 46 μs | 3% |
| Pulse generation | 1,600 μs | 88% |
| PCNT read/clear | 50 μs | 3% |
| LUT lookups | 5 μs | <1% |
| Software overhead | 100 μs | 6% |
| **Total** | **1,800 μs** | **551 Hz** |

---

## Hardware Requirements

| Resource | Usage |
|----------|-------|
| PCNT units | 1 of 4 |
| RMT TX channels | 1 of 4 |
| GPIO pins | 1 (internal loopback) |
| SRAM | ~304 KB |
| CPU | Near-zero during pulse transmission |

---

## Future Optimizations

### Parallel PCNT (4x speedup potential)
Use all 4 PCNT units + 4 RMT channels to process 4 neurons simultaneously.
- Theoretical: 1.8 ms / 4 = 450 μs = 2,200 Hz

### Direct Register Access
Bypass ESP-IDF RMT driver overhead (23 μs → ~5 μs).
- Potential: additional 2-3x improvement

### LP Core Integration
Run the fabric from the 20 MHz low-power core.
- Power: ~1 mA continuous inference
- Battery: weeks to months on single charge

---

## Files

| File | Purpose |
|------|---------|
| `reflex_fabric_cfc.h` | Main implementation |
| `reflex_turing_fabric.h` | Base PCNT/RMT fabric |
| `fabric_cfc_demo.c` | Benchmarks and verification |
| `activation_q15.h` | Q15 activation functions |
| `cfc_cell_chip.h` | CfC cell reference |

---

## The Stack

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    PURE ETM FABRIC CfC                                  │
│                    551 Hz, 64 neurons, 100% accurate                    │
├─────────────────────────────────────────────────────────────────────────┤
│                    DMA Pulse Train                                      │
│                    2 rmt_transmit() calls per inference                 │
├─────────────────────────────────────────────────────────────────────────┤
│                    Turing Fabric                                        │
│                    PCNT counts pulses = hardware addition               │
├─────────────────────────────────────────────────────────────────────────┤
│                    Yinsen Q15                                           │
│                    Sparse ternary, LUT activations                      │
├─────────────────────────────────────────────────────────────────────────┤
│                    ESP32-C6 RV32IMAC                                    │
│                    $5, no FPU, 160 MHz                                  │
└─────────────────────────────────────────────────────────────────────────┘
```

---

**551 thoughts per second. Zero CPU. Pure hardware logic.**

*The chip doesn't run a neural network. The chip IS the neural network.*
