# Geometry Intersection Engine: Architecture

## Overview

The Geometry Intersection Engine (GIE) computes ternary dot products using the ESP32-C6's peripheral fabric. Shapes (trit vectors) flow through DMA channels, intersect at PCNT manifolds, and produce scalar results. The ETM topology defines the computation. SRAM holds the geometry. The ground state is silence.

## Three-Level Architecture

```
Level 3: TOPOLOGY    — ETM channels, REGDMA (defines what connects to what)
Level 2: CHANNELS    — DMA, PARLIO, GPIO (move/transform shapes)
Level 1: MANIFOLDS   — PCNT units (measure intersections)
```

### Level 1: Single-Unit Pipeline (Milestones 4-7, verified)

PARLIO(X) + static Y → PCNT → result. Single dot product per evaluation. CPU pre-multiplies W×X and encodes the product into DMA buffers.

**Resource usage:**
- PARLIO TX: 2-bit mode, 1MHz, GPIO 4-5
- PCNT Unit 0: agree (X_pos×Y_pos + X_neg×Y_neg)
- PCNT Unit 1: disagree (X_pos×Y_neg + X_neg×Y_pos)
- GDMA CH0: PARLIO data
- GPTimer 0: kickoff (ETM-enabled)

### Level 1.5: Ternary CfC Layer (Milestone 7, verified)

GIE dot products + CfC temporal dynamics. The GIE computes 2N dot products per step (N for gate pathway, N for candidate pathway). The CPU applies the ternary CfC blend: `h_new = (f==0) ? h_old : f*g`. Three blend modes: UPDATE (f=+1), HOLD (f=0), INVERT (f=-1).

**Zero-copy concatenation:** Weight layout encodes concatenation statically. `W[0..input_dim-1]` pairs with input, `W[input_dim..concat_dim-1]` pairs with hidden state. No runtime buffer copy.

**Novel dynamics:** The inversion mode (f=-1) creates natural oscillation and convergence resistance — the network maintains a high-energy uncommitted state under constant stimulus (analogous to biological pluripotency).

### Level 2: Dual-Channel Engine (planned, Milestone 8)

REGDMA advances descriptor pointers between neuron evaluations. ETM auto-restart loop: GDMA EOF → Timer reload → GDMA start (next neuron). Multiple neurons sequence without CPU.

### Level 3: Self-Sequencing Fabric (planned, Milestone 8-9)

LP core manages inter-layer logic. Main CPU sleeps. REGDMA advances descriptor tables. ETM topology reconfigures between phases.

## Key Architectural Decisions

### 1. Two-unit TMUL (not single-unit)

A single PCNT unit has 2 channels. Ternary multiply with static Y requires 4 gating combinations: X_pos×Y_pos, X_neg×Y_neg (agree), X_pos×Y_neg, X_neg×Y_pos (disagree). Two PCNT units handle this naturally. This leaves 2 PCNT units free for diagnostics or parallel evaluation.

### 2. PARLIO for X, static GPIO for Y (not RMT for Y)

The original plan was PARLIO(X) + RMT(Y), both DMA-driven. ESP32-C6 RMT has no DMA support — it uses 48-word ping-pong buffers refilled by ISR. This puts the CPU in the loop, defeating autonomy. Solution: keep Y as static CPU-driven levels. Pre-multiply P[i] = W[i] × X[i] on CPU (~200 cycles for 256 trits), encode P into PARLIO buffers. The hardware sums P.

### 3. Normal GDMA mode (not ETM-triggered)

GDMA with ETM_EN processes one descriptor per trigger and won't follow linked lists. Normal GDMA mode auto-follows descriptor chains. Gate output with PARLIO TX_START instead of ETM-triggered GDMA.

### 4. Zero-interleave encoding (not packed)

Each trit occupies 2 dibit slots: (value, then 00). This guarantees one clean rising edge per non-zero trit, regardless of neighbors. 64 bytes = 128 trits per buffer. Chains of buffers scale linearly.

### 5. 1 MHz clock (conservative, upgradeable)

PARLIO runs at 1 MHz. PCNT is rated for 40 MHz. Increasing to 10-20 MHz gives 10-20x throughput with no architectural change. This is deferred to Milestone 9.

### 6. Zero-copy concatenation (Milestone 7)

CfC networks concatenate input and hidden state as `[input | hidden]` before computing dot products. Rather than copying into a temporary buffer (~160 bytes memcpy), the weight matrix layout encodes the concatenation: `W[0..127]` multiplies with `input[0..127]`, `W[128..159]` multiplies with `hidden[0..31]`. The pre-multiply loop walks both arrays directly from their permanent SRAM locations. The "concatenation" is a compile-time convention, not a runtime operation.

## Encoding Detail

```
Trit +1: dibit 01 (GPIO4=1, GPIO5=0), then dibit 00 (silence)
Trit -1: dibit 10 (GPIO4=0, GPIO5=1), then dibit 00 (silence)
Trit  0: dibit 00, dibit 00 (silence, silence)

Byte layout (2 trits per byte):
  bits [1:0] = trit 0 value dibit
  bits [3:2] = trit 0 zero dibit (always 00)
  bits [5:4] = trit 1 value dibit
  bits [7:6] = trit 1 zero dibit (always 00)

  +1,+1 → 0x11    -1,+1 → 0x12    0,+1 → 0x10
  +1,-1 → 0x21    -1,-1 → 0x22    0,-1 → 0x20
  +1, 0 → 0x01    -1, 0 → 0x02    0, 0 → 0x00
```

## ETM Channel Budget

```
Level 1 (current):   5 channels  (kick, clear, watchdog, auto-stop, prime)
Level 2 (planned):  20 channels  (dual-neuron eval + result routing)
Level 3 (planned):  15 channels  (REGDMA loop + LP core signaling)
Total:              40 channels
Remaining:          10 channels  (error handling, diagnostics, expansion)
```

## Performance Model

At 1 MHz PARLIO clock:
- 128 trits = 256 dibit clocks = 256 us on wire + DMA overhead ≈ 1 ms total
- 256 trits = 512 dibit clocks = 512 us on wire + DMA overhead ≈ 1.5 ms total

Layer throughput (N neurons, D trits each):
- T_total ≈ N × (D/128 × 1ms + 0.8ms overhead)
- 32 neurons × 256 trits: 32 × 2.35ms ≈ 75ms → 425 neurons/s

At 10 MHz: wire time drops 10x, overhead dominates → ~1.4ms/neuron → ~700 neurons/s
At 20 MHz: wire time drops 20x → ~1.3ms/neuron → ~770 neurons/s

The real win at higher clock rates is for large vectors (1024+ trits).

## Memory Budget

| Content | Size | Notes |
|---------|------|-------|
| Weight vector (256 trits) | 64 bytes | 2 bits per trit, bit-packed before encoding |
| DMA buffer (128 trits) | 64 bytes | Zero-interleaved, ready for PARLIO |
| DMA descriptor | 12 bytes | lldesc_t |
| 256-neuron layer weights | 16 KB | 256 × 64 bytes |
| 8 DMA buffers | 512 bytes | Reused across neurons |
| SRAM available | 512 KB | Fits ~30 layers of 256×256 |
| CfC layer (32 hidden, 160 concat) | ~16 KB | 2 × 32 × 256 weights + biases + hidden |
| CfC step working memory | 0 bytes | Zero-copy concat; product buffer on stack (~256B) |
