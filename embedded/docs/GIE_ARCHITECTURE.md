# Geometry Intersection Engine: Architecture

**Last Updated:** February 10, 2026 (Hidden Re-encode Skipped, CfC→TriX Migration Phase 4)

## Overview

The Geometry Intersection Engine (GIE) computes ternary dot products using the ESP32-C6's peripheral fabric. CPU pre-multiplies weight×input, encodes into DMA buffers, GDMA streams through PARLIO (2-bit loopback to GPIO), PCNT level-gates edge counting to accumulate agree/disagree counts, dot = agree - disagree.

On top of this, a three-layer hierarchy was built:
- **GIE** (428 Hz): Peripheral hardware IS the neural network
- **LP core** (100 Hz, ~30uA): Hand-written RISC-V for geometric CfC + VDB
- **HP core** (on-demand): Initialization and monitoring

## Three-Layer Architecture

```
Layer 1: GIE          — GDMA + PARLIO + PCNT (peripheral fabric computes)
Layer 2: LP CORE      — 16MHz RISC-V, hand-ASM (geometric CfC + VDB)
Layer 3: HP CORE      — 160MHz CPU (init + monitoring)
```

### Layer 1: Free-Running GIE (Verified, 428 Hz)

Circular DMA chain loops forever: `[dummy×5][neuron×64][separator EOF=1 → back to dummy0]`. ISR fires on each neuron's EOF, reads PCNT (after 200-loop clock domain drain), applies CfC blend, re-encodes the next neuron's products. 64 neurons at 10MHz PARLIO, 428 Hz.

**Resource usage:**
- PARLIO TX: 2-bit mode, 10MHz, GPIO 4-5, io_loop_back
- PCNT Unit 0: agree (X_pos gated by Y_pos + X_neg gated by Y_neg)
- PCNT Unit 1: disagree (X_pos gated by Y_neg + X_neg gated by Y_pos)
- GDMA CH0: owned by PARLIO driver, bare-metal reconfigured
- GPTimer 0: kickoff (ETM-enabled)
- ISR: LEVEL3 priority, source ETS_DMA_OUT_CH0_INTR_SOURCE=69

**CfC blend in ISR (DISABLED — Phase 3):**
```
dot = agree - disagree
f = sign(dot_f)    // gate:      {-1, 0, +1}
g = sign(dot_g)    // candidate: {-1, 0, +1}
h_new = (f == 0) ? h_old : f * g   // ternary blend
// Phase 3: gate_threshold = INT32_MAX → f is ALWAYS T_ZERO → every neuron HOLDs
// Gate firing: 0%. Hidden state freezes after input install.
```

Three blend modes exist: UPDATE (f=+1), HOLD (f=0), INVERT (f=-1). As of Phase 3, only HOLD executes — `gate_threshold = INT32_MAX` blocks all gate activations. The blend code still runs but produces no state changes. Classification is TriX-only.

### Layer 2: LP Core Geometric Processor (Verified, 100 Hz)

16MHz RISC-V LP core, hand-written assembly. Wakes every 10ms via LP timer. Five commands:

| Command | Function | Stack Frame |
|---------|----------|-------------|
| cmd=1 | CfC step (16 neurons, 48-trit input) | 96 bytes |
| cmd=2 | VDB search (NSW graph or brute-force) | 608 bytes |
| cmd=3 | VDB insert (brute-force + top-M + edges) | 224 bytes |
| cmd=4 | CfC + VDB pipeline (one wake cycle) | 96B then 608B sequential |
| cmd=5 | CfC + VDB + feedback (memory → lp_hidden blend) | 96B then 608B sequential |

**Operations:** AND, popcount (256-byte LUT), add, sub, negate, branch, shift. No MUL. No FP.

**VDB:** 64 nodes, 48 trits each, 32 bytes per node (24B vector + 7B neighbors + 1B count). NSW graph with M=7, ef=32, dual entry points. recall@1=95%, recall@4=90%.

**Pipeline (cmd=4):** CfC step → copy 6 words (packed trits) to VDB query BSS → VDB search. The CfC's packed [gie_hidden | lp_hidden] IS the VDB query. No re-packing.

**Feedback (cmd=5):** CfC step → VDB search → load best match's LP-hidden portion (trits 32..47) → blend into lp_hidden using ternary rules:
- Agreement (h == mem): no change
- Gap fill (h == 0, mem != 0): h = mem
- Silence (h != 0, mem == 0): no change
- Conflict (h != 0, mem != 0, h != mem): h = 0 (HOLD)

The HOLD damper prevents feedback-driven oscillation. Verified: 50 unique states in 50 steps, energy bounded [7, 15], no lock-in.

### ISR→HP Coordination: Reflex Channel

The GIE ISR (producer) signals the HP main loop (consumer) via `reflex_channel_t` — the same coordination primitive used on Jetson Thor, adapted for RISC-V SRAM ordering.

- `reflex_signal(&gie_channel, loop_count)`: writes value, captures cycle-accurate timestamp (CSR 0x7e2), `fence rw, rw`, increments sequence
- HP main loop: `reflex_wait_timeout()` spin-waits until sequence changes, then reads with ordering guarantee
- Average latency: 18,300ns (ISR tail work, not channel cost)
- Channel itself: store + fence + increment ≈ ~10 cycles

On the C6, this is SRAM ordering (no cache to snoop). On Thor, it's cache line invalidation. The primitive is the same; the physics underneath changes.

### TriX Classification Path (ISR Step 3b, Verified Feb 10 2026)

The ISR now performs TriX classification alongside the CfC blend. After decoding per-neuron dots (step 3), step 3b validates and publishes classification data:

**ISR Step 3b flow:**
1. Check all 4 groups of 8 neurons for uniformity (all 8 dots identical within each group)
2. If all 4 groups are uniform → clean loop. If any group has mixed values → stale/shifted data from GDMA offset, skip.
3. On clean loop: pack 4 group dot values as signed bytes into `trix_channel.value`, signal via `reflex_signal()`.

**Why groups of 8?** TriX assigns the same signature to all 8 neurons of a pattern. If the GDMA offset hasn't shifted neurons across group boundaries, all 8 produce identical dots. Non-uniform dots indicate the GDMA read pointer crossed a pattern boundary, mixing neurons from different patterns.

**GDMA chain offset:** The free-running GDMA circular chain never stops. When PARLIO restarts after each loop, the GDMA's internal read pointer is at an arbitrary descriptor. This means capture[0] may correspond to neuron K, not neuron 0. The offset permutes which group of 8 maps to which capture indices. The ISR publishes raw group values; the main loop resolves the permutation by matching against CPU-computed reference dots from `sig[]`.

**trix_channel:** `reflex_channel_t` with packed 4×int8 dot values in `.value`. Consumer uses `reflex_wait_timeout()` with 100ms timeout (16M cycles at 160 MHz). Sequence number tracks exactly with `trix_count` (verified on silicon). Effective clean rate: ~62 Hz (2108 clean out of 33807 loops).

**CfC blend (Step 4):** DISABLED as of Phase 3 (commit `c6fd284`). `gate_threshold = INT32_MAX` — 0% gate firing. Every neuron HOLDs. The blend code still executes but produces no state changes. Hidden state freezes after input install.

**Hidden re-encode (Step 5):** SKIPPED as of Phase 4 (commit `8a33369`). Gated by `if (thresh < 0x7FFFFFFF)` — only executes when blend is active (Tests 1-10). When blend is disabled, hidden never changes, so the DMA buffers encoding W×hidden products remain valid. Saves ~20us per ISR loop.

### Layer 3: HP Core (On-Demand)

Full 160MHz RISC-V CPU. Initializes peripherals, loads weights, starts GIE and LP core, then monitors. Provides the VDB API (`vdb_insert`, `vdb_search`, `vdb_cfc_pipeline_step`, `vdb_cfc_feedback_step`).

### Historical Note: REGDMA Path Not Taken

The original plan (M8-M9) was to use REGDMA to advance descriptor pointers between neurons, with ETM auto-restart loops. Instead, the free-running circular DMA chain with ISR-driven re-encoding proved simpler and more effective. The LP core provides the inter-layer intelligence that REGDMA would have managed, but with much more capability (CfC + VDB).

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

## Performance (Verified on Silicon)

**GIE (10MHz PARLIO):**
- 428 Hz loop rate (64 neurons per loop)
- ~2.3ms per full loop
- ISR re-encode: ~20us per neuron (within 86us inter-neuron window)

**LP Core (16MHz RISC-V):**
- CfC step: ~200us (16 neurons × 2 pathways = 32 INTERSECT calls)
- VDB search (NSW, 64 nodes): ~100-200us
- Pipeline (CfC + VDB): ~300-400us per wake
- Duty cycle: ~4% (active 400us out of 10,000us wake period)

**Combined throughput:**
- GIE: 428 × 64 = 27,392 dot products/s (peripheral hardware)
- LP: 100 × 32 = 3,200 INTERSECT calls/s (geometric processor)
- VDB: 100 searches/s with NSW graph (if pipeline mode)

## LP Core Memory Budget (16KB LP SRAM)

| Section | Size | Notes |
|---------|------|-------|
| Vector table | 128 B | Fixed |
| Code (.text) | ~7,600 B | CfC + VDB search + insert + pipeline + feedback |
| Popcount LUT (.rodata) | 288 B | 256-byte LUT + alignment |
| CfC state (.bss) | ~968 B | Weights, hidden, dots, sync vars |
| VDB nodes (.bss) | 2,048 B | 64 × 32 bytes (M=7 neighbors) |
| VDB metadata (.bss) | ~80 B | Query, results, counters |
| Feedback state (.bss) | ~24 B | fb_applied, fb_source_id, fb_score, fb_blend_count, fb_total_blends, fb_threshold |
| shared_mem | 16 B | Top of SRAM |
| **Free for stack** | **~4,400 B** | Peak: 608B (VDB search frame), measured 4,356 free |

## HP Core Memory Budget

| Content | Size | Notes |
|---------|------|-------|
| CfC struct (static) | ~16 KB | 2 × 64 × 160 weights + biases + hidden + DMA buffers |
| DMA descriptors | ~5 KB | Circular chain: 5 dummy + 64 neurons + separators |
| Product buffers | ~512 B | Reused across neurons in ISR |
