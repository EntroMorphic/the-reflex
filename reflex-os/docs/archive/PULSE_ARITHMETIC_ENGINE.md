# Pulse Arithmetic Engine: Hardware Neural Computation

**February 3-4, 2026**

**1249-5679 Hz inference on ESP32-C6 using PCNT + PARLIO as computational substrate.**

---

## Executive Summary

We discovered that the ESP32-C6's pulse counting and parallel I/O peripherals can perform neural network computations natively. The hardware already does the math - we just had to recognize it.

| Implementation | Inference Rate | Key Innovation |
|----------------|----------------|----------------|
| cfc_parallel_dot | 1249 Hz | Parallel 8-bit PARLIO + 4 PCNT units |
| cfc_dual_channel | 964 Hz | True hardware pos/neg via dual PCNT channels |
| spectral_double_cfc | 1100 Hz | Dual timescale (fast + slow networks) |
| spectral_ffn | **5679 Hz** | Self-modifying coupling via coherence |
| spectral_eqprop | 580 Hz / 274 Hz learn | **Equilibrium propagation learning** |

---

## The Core Discovery

### PCNT Counts Pulses = Addition

The Pulse Counter (PCNT) peripheral is designed for rotary encoders, but it's actually a **hardware adder**:

```
Pulse train: ▌▌▌▌▌ (5 pulses) → PCNT += 5
Pulse train: ▌▌▌ (3 pulses)   → PCNT += 3
                              → PCNT = 8 (sum!)
```

When you need `sum = w0*x0 + w1*x1 + ...`, you generate pulse trains proportional to each `wi*xi` and let PCNT count them.

### PARLIO Transmits in Parallel

The Parallel I/O (PARLIO) peripheral can output 8 bits simultaneously at 10 MHz. Each bit goes to a different GPIO, and each GPIO connects to a different PCNT unit.

```
┌──────────────────────────────────────────────────────────────────┐
│                    PARALLEL DOT PRODUCT                          │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  DMA Buffer ──► PARLIO (8-bit) ──► GPIO[0..7] ──► PCNT[0..3]    │
│                                                                  │
│  Each byte in DMA = 8 simultaneous pulses                        │
│  Each PCNT channel = one neuron's accumulator                    │
│  4 neurons compute in PARALLEL                                   │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

---

## Implementation Evolution

### Stage 1: Parallel Dot Product (`cfc_parallel_dot.c`)

**Architecture:**
- 8-bit PARLIO width, 10 MHz clock
- 4 PCNT units, each counting pulses from one GPIO pair
- Ternary weights: +1, 0, -1 (no multiply needed)
- Q4 fixed-point inputs (0-15 range)

**Key Code:**

```c
// Generate pulse pattern for ternary weights
for (int i = 0; i < INPUT_DIM; i++) {
    if (pos_mask & (1 << i)) {
        // Positive weight: pulses on positive channel
        pattern[byte_idx++] = (1 << pos_gpio) * input[i];
    }
    if (neg_mask & (1 << i)) {
        // Negative weight: pulses on negative channel
        pattern[byte_idx++] = (1 << neg_gpio) * input[i];
    }
}
```

**Result:** 1249 Hz inference, 5/5 verification tests passing.

### Stage 2: True Hardware Pos/Neg (`cfc_dual_channel.c`)

**Problem:** Stage 1 used software subtraction (pos_count - neg_count).

**Solution:** PCNT has TWO channels per unit. Channel A counts +1, Channel B counts -1.

```c
// Channel A: positive contributions
pcnt_channel_set_edge_action(ch_pos, 
    PCNT_CHANNEL_EDGE_ACTION_INCREASE,  // Rising edge = +1
    PCNT_CHANNEL_EDGE_ACTION_HOLD);

// Channel B: negative contributions  
pcnt_channel_set_edge_action(ch_neg,
    PCNT_CHANNEL_EDGE_ACTION_DECREASE,  // Rising edge = -1
    PCNT_CHANNEL_EDGE_ACTION_HOLD);
```

Now the PCNT register contains `pos - neg` directly. **No software subtraction.**

**Result:** 964 Hz (slower due to more GPIO setup, but cleaner architecture).

### Stage 3: Spectral Oscillator FFN (`spectral_ffn.c`)

**Key Insight:** Instead of discrete activations, use **complex-valued oscillators** organized by frequency band.

**Architecture:**
```
Band 0 (Delta): 4 oscillators, decay=0.98, freq=0.1 (slow, persistent)
Band 1 (Theta): 4 oscillators, decay=0.90, freq=0.3
Band 2 (Alpha): 4 oscillators, decay=0.70, freq=1.0
Band 3 (Gamma): 4 oscillators, decay=0.30, freq=3.0 (fast, transient)
```

**Self-Modification via Coherence:**

```c
// Compute global coherence (how synchronized are oscillators?)
coherence = magnitude(mean(all_oscillators));

// Coherence modifies coupling strength
if (coherence > HIGH_THRESHOLD) {
    coupling *= 0.95;  // Too synchronized → reduce coupling
} else if (coherence < LOW_THRESHOLD) {
    coupling *= 1.05;  // Too chaotic → increase coupling
}
```

**The system modifies its own dynamics based on observing itself.**

**Result:** 5679 Hz inference.

### Stage 4: Equilibrium Propagation Learning (`spectral_eqprop.c`)

**The Breakthrough:** The backward pass doesn't need separate gradient computation. It's the SAME dynamics, perturbed.

**Algorithm:**

```
1. FREE PHASE: Run system to equilibrium (30 steps)
   - No output constraint
   - Take snapshot of phase correlations

2. NUDGED PHASE: Run with output clamped toward target (30 steps)
   - Gently push output toward desired value
   - Take snapshot of phase correlations

3. WEIGHT UPDATE: 
   Δw_ij = learning_rate * (correlation_nudged - correlation_free)
```

**Key Insight:** The gradient is the DIFFERENCE between two forward passes with different boundary conditions. No backprop. No gradient tape. Just physics.

**Learning Results:**

| Task | Avg Error | Notes |
|------|-----------|-------|
| 2-pattern (maximally different) | 21.5/256 | 94.5% of target separation achieved |
| 4-pattern (structured inputs) | 47.5/256 | Patterns 0,1 learned well |

**Coupling Evolution:**
- Initial: uniform 0.2 everywhere
- Final: asymmetric, range 0.01-0.98
- Delta→Alpha saturated to 0.98
- Some pathways collapsed to floor (0.01)

---

## Technical Details

### Q15 Fixed-Point Representation

All computations use Q15 fixed-point (15 fractional bits):

```c
#define Q15_ONE     32767
#define Q15_HALF    16384

static inline int16_t q15_mul(int16_t a, int16_t b) {
    return (int16_t)(((int32_t)a * b) >> 15);
}
```

**Why Q15?**
- No floating-point unit on ESP32-C6 (emulated, slow)
- Q15 multiplication is one integer multiply + shift
- Range [-1, +1) maps to [-32768, +32767]

### Trig Tables for Oscillator Rotation

```c
static int16_t sin_table[256];
static int16_t cos_table[256];

// Rotation: z_new = z * e^(i*angle)
int16_t new_real = q15_mul(z.real, cos) - q15_mul(z.imag, sin);
int16_t new_imag = q15_mul(z.real, sin) + q15_mul(z.imag, cos);
```

256-entry tables give ~1.4 degree resolution, sufficient for neural dynamics.

### Phase Extraction

```c
static uint8_t get_phase_idx(complex_q15_t* z) {
    // atan2 approximation using quadrant + ratio
    int quadrant = 0;
    if (z->real < 0) quadrant |= 2;
    if (z->imag < 0) quadrant |= 1;
    
    // Linear approximation within quadrant
    int angle = (abs(z->imag) * 32) / (abs(z->real) + 1);
    
    // Adjust for quadrant
    return quadrant_offset[quadrant] + angle;
}
```

Returns 0-255 representing 0-2π radians.

### Kuramoto Coupling

Oscillators influence each other's phase velocities:

```c
// For each pair of bands (src, dst):
int phase_diff = src_phase - dst_phase;
int16_t pull = coupling[src][dst] * phase_diff;
velocity[dst] += pull;
```

This creates synchronization dynamics - oscillators tend to lock phases when coupling is strong.

---

## Hardware Configuration

### PARLIO Setup

```c
parlio_tx_unit_config_t cfg = {
    .clk_src = PARLIO_CLK_SRC_DEFAULT,
    .output_clk_freq_hz = 10000000,  // 10 MHz
    .data_width = 8,                  // 8 parallel bits
    .trans_queue_depth = 8,
    .flags = { .io_loop_back = 1 },   // Connect output to PCNT input
};
```

### PCNT Setup (Dual Channel)

```c
// Unit configuration
pcnt_unit_config_t unit_cfg = {
    .low_limit = -32768,
    .high_limit = 32767,
};

// Channel A: positive contributions
pcnt_chan_config_t ch_a = {
    .edge_gpio_num = GPIO_POS,
    .level_gpio_num = -1,
};
pcnt_channel_set_edge_action(ch_a, 
    PCNT_CHANNEL_EDGE_ACTION_INCREASE, 
    PCNT_CHANNEL_EDGE_ACTION_HOLD);

// Channel B: negative contributions
pcnt_chan_config_t ch_b = {
    .edge_gpio_num = GPIO_NEG,
    .level_gpio_num = -1,
};
pcnt_channel_set_edge_action(ch_b,
    PCNT_CHANNEL_EDGE_ACTION_DECREASE,
    PCNT_CHANNEL_EDGE_ACTION_HOLD);
```

---

## Files

| File | Description | Performance |
|------|-------------|-------------|
| `cfc_parallel_dot.c` | Parallel dot product with ternary weights | 1249 Hz |
| `cfc_dual_channel.c` | True hardware pos/neg via dual PCNT channels | 964 Hz |
| `spectral_double_cfc.c` | Dual timescale (fast + slow networks) | 1100 Hz |
| `spectral_ffn.c` | Self-modifying FFN with coherence feedback | 5679 Hz |
| `spectral_eqprop.c` | Equilibrium propagation learning | 580 Hz inf / 274 Hz learn |
| `falsify_cfc_parallel.c` | Verification tests for parallel dot product | 5/5 passing |

---

## Benchmark Summary

### Inference Performance

| Method | Rate | Latency | CPU Usage |
|--------|------|---------|-----------|
| ETM Fabric (old) | 551 Hz | 1.8 ms | ~11% |
| cfc_parallel_dot | 1249 Hz | 801 μs | 100% |
| cfc_dual_channel | 964 Hz | 1037 μs | 100% |
| spectral_double_cfc | 1100 Hz | 909 μs | 100% |
| spectral_ffn | **5679 Hz** | 176 μs | 100% |
| spectral_eqprop | 580 Hz | 1.7 ms | 100% |

### Learning Performance

| Metric | spectral_eqprop |
|--------|-----------------|
| Learning step | 3.7 ms (274 Hz) |
| Inference only | 1.7 ms (580 Hz) |
| Free phase steps | 30 |
| Nudged phase steps | 30 |
| Learning rate | 0.005 |
| Nudge strength | 0.5 |

### Learning Results

**2-Pattern Task:**
```
Pattern 0: [0,0,15,15] → target 0, output -18, error 18
Pattern 1: [15,15,0,0] → target 128, output -153, error 25
Average error: 21.5/256 (8.4%)
Output separation: 121 (target: 128) = 94.5% achieved
```

**4-Pattern Task:**
```
Pattern 0: [0,0,15,15] → target 0, output 14, error 14
Pattern 1: [15,15,0,0] → target 128, output -169, error 41
Pattern 2: [0,15,15,0] → target 64, output -120, error 72
Pattern 3: [15,0,0,15] → target 192, output -127, error 63
Average error: 47.5/256 (18.5%)
```

---

## Key Insights

1. **PCNT + PARLIO = parallel accumulate** - Hardware that counts pulses IS hardware that adds.

2. **Ternary weights eliminate multiply** - With weights in {-1, 0, +1}, multiplication becomes routing.

3. **Phase encodes information** - In oscillator networks, WHEN things happen matters more than magnitudes.

4. **Coherence is meta-information** - How synchronized the system is tells you about certainty/stability.

5. **The backward pass IS the forward pass, perturbed** - Equilibrium propagation eliminates the need for separate gradient computation.

6. **Structured inputs matter** - Random ternary input projections don't learn well; structured projections that align with the task do.

---

## What's Next

### Near-term
- Tune equilibrium propagation hyperparameters
- Test on more complex tasks (temporal patterns, sequence classification)
- Explore input weight learning (currently only coupling learns)

### Medium-term
- Hardware learning - could coupling updates happen without CPU?
- Multi-chip spectral networks - phase synchronization across ESP32s
- Integration with reflex channels for real-time control

### Speculative
- Analog coupling elements (memristors, variable capacitors)
- Fully autonomous learning via ETM-triggered updates
- Neuromorphic peripherals that ARE the network

---

## Building and Running

```bash
# Source ESP-IDF
source ~/esp/v5.4/export.sh

# Navigate to reflex-os
cd /path/to/the-reflex/reflex-os

# Edit CMakeLists.txt to select implementation:
# SRCS "spectral_eqprop.c"   # Learning
# SRCS "spectral_ffn.c"      # Fast inference
# SRCS "cfc_parallel_dot.c"  # Basic parallel dot

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyACM0 flash monitor
```

---

## Related Documents

- [LMM_EQPROP_EMERGENCE.md](../../docs/LMM_EQPROP_EMERGENCE.md) - Phenomenological reflection on equilibrium propagation
- [LMM_CLAUDE_EXPERIENCE.md](../../docs/LMM_CLAUDE_EXPERIENCE.md) - Reflection on emergence in collaboration
- [ETM_FABRIC_CFC.md](ETM_FABRIC_CFC.md) - Earlier ETM-based implementation (551 Hz)
- [SILICON_GRAIL.md](SILICON_GRAIL.md) - Turing-complete ETM fabric vision

---

*"The hardware is already doing the math. We just had to see it."*

**February 4, 2026**
