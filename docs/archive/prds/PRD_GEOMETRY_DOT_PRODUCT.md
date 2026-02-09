# PRD: 256-Trit Geometry Dot Product Engine

> **Project:** The Reflex — Geometry Intersection Engine (GIE), Milestone 5
> **Version:** 1.0
> **Date:** February 2026
> **Status:** Active
> **Predecessor:** Milestone 4 — Ternary TMUL (9/9 verified on silicon)

---

## Executive Summary

Transform the Milestone 4 ternary multiplier into a 256-trit dot product engine. One dibit = one trit. One 64-byte DMA buffer = one 256-trit vector. Descriptor chains accumulate across multiple vectors. PCNT gives the raw integer dot product, not just the sign. Auto-stop via ETM on DMA completion — no watchdog, no wasted time.

This is the first step of the Geometry Intersection Engine: proving that shapes flowing through DMA channels produce correct inner products when they intersect at PCNT manifolds.

---

## What Changes from Milestone 4

| Aspect | Milestone 4 | Milestone 5 |
|--------|------------|------------|
| Pattern encoding | 208 edges per trit (sledgehammer) | 1 edge per trit (dibit = trit) |
| Vector length | 1 trit per evaluation | 256 trits per buffer |
| Accumulation | Per-test only | Across descriptor chains |
| Completion | Watchdog timer (2ms) | ETM auto-stop on GDMA EOF |
| Output | Sign only {-1, 0, +1} | Raw integer dot product value |
| Y geometry | CPU-driven static | CPU-driven static (unchanged) |
| PCNT units | 2 for TMUL + 2 diagnostic | 2 for TMUL + 2 diagnostic |

---

## Architecture

```
SRAM                    DMA                  GPIO              PCNT
┌──────────┐     ┌──────────────┐     ┌──────────────┐   ┌──────────┐
│ X vector │────→│ GDMA CH0     │────→│ PARLIO 2-bit │──→│ Unit 0   │
│ 64 bytes │     │ desc chain   │     │ GPIO 4 (X+)  │   │ agree    │
│ =256 trits│     │              │     │ GPIO 5 (X-)  │   │ INC+INC  │
└──────────┘     └──────┬───────┘     └──────────────┘   ├──────────┤
                        │                                  │ Unit 1   │
                   EOF event                               │ disagree │
                        │                                  │ INC+INC  │
                   ETM ─┤                                  └────┬─────┘
                        │                                       │
                   Timer stop                              CPU reads:
                        │                                  dot = agree
                   CPU wakes                                    - disagree
                                   ┌──────────────┐
                                   │ GPIO 6 (Y+)  │ CPU-driven
                                   │ GPIO 7 (Y-)  │ static levels
                                   └──────────────┘
```

### Pattern Encoding: 1 Dibit = 1 Trit

A dibit is two bits. A trit is {-1, 0, +1}. They are the same thing:

```
dibit 01 → trit +1 (X_pos=1, X_neg=0) → rising edge on GPIO4
dibit 10 → trit -1 (X_pos=0, X_neg=1) → rising edge on GPIO5
dibit 00 → trit  0 (both LOW)          → no edges (silence)
dibit 11 → INVALID (never used)
```

Each byte packs 4 trits (LSB first):
```
byte = [trit0 in bits 1:0] [trit1 in bits 3:2] [trit2 in bits 5:4] [trit3 in bits 7:6]
```

64 bytes = 256 trits = one vector.

### Edge Counting: 1 Trit = 1 PCNT Count

The key change: in Milestone 4, X_pos toggled ON/OFF/ON/OFF producing many edges per trit. Now each trit occupies exactly one dibit slot. A +1 trit produces one HIGH state on X_pos (dibit 01), followed by the next dibit which may be 00 or different.

Rising edge occurs when X_pos transitions from 0 to 1. This happens when:
- Previous dibit had X_pos=0, current dibit has X_pos=1
- Specifically: trit 0 or -1 followed by trit +1

So not every +1 trit produces a rising edge — only those preceded by 0 or -1.

**This means the count depends on the vector's sequential structure, not just the number of +1 trits.**

CORRECTION: This breaks the dot product. We need every +1 trit to produce exactly one countable event regardless of the preceding trit. 

**Solution: Interleave with zeros.** Each trit gets TWO dibit slots: the trit value, then 00 (return to ground). This guarantees a LOW→HIGH transition for every non-zero trit.

```
trit +1 → dibits: 01, 00  (rise then fall on X_pos)
trit -1 → dibits: 10, 00  (rise then fall on X_neg)
trit  0 → dibits: 00, 00  (silence)
```

Now each trit takes 2 dibits = 4 bits = half a byte. 64 bytes = 128 trits per buffer (not 256).

Wait — we can recover 256 trits by using 128-byte buffers. Or accept 128 trits per 64-byte buffer and chain 2 descriptors for 256 trits.

**Decision: 128 trits per 64-byte buffer. Chain 2 descriptors for 256 trits. Chain N×2 descriptors for N×256-trit vectors.** This keeps the buffer size at 64 bytes (proven DMA size) and the math clean.

### Descriptor Chain Dot Product

For a dot product of two 128-trit vectors W and X:
1. CPU encodes element-wise products: P[i] = W[i] × X[i] (trit × trit = trit, table lookup)
2. CPU packs P into a 64-byte buffer (128 trits with zero-interleave)
3. CPU sets Y = +1 (static)
4. DMA transmits buffer. PCNT counts:
   - agree = number of +1 results (P[i] = +1 → X_pos edge gated by Y_pos)
   - disagree = number of -1 results (P[i] = -1 → X_neg edge gated by Y_pos... wait)

Actually, with Y=+1:
- agree (Unit 0): Ch0 counts X_pos edges gated by Y_pos=HIGH. Ch1 counts X_neg edges gated by Y_neg=LOW (gated off).
- disagree (Unit 1): Ch0 counts X_pos edges gated by Y_neg=LOW (gated off). Ch1 counts X_neg edges gated by Y_pos=HIGH.

So: agree = count of +1 trits, disagree = count of -1 trits.
Dot product = agree - disagree = (number of +1) - (number of -1) = sum of all trits.

For a descriptor chain of N buffers, PCNT accumulates across all of them:
dot = total_agree - total_disagree = Σ P[i] for all i

With PCNT limits at ±32767 and 1 count per trit, maximum chain = 32767 trits = 255 buffers of 128 trits. That's a 32K-element dot product.

### ETM Auto-Stop

```
ETM Ch 0: Timer0 alarm       → GDMA CH0 start      [kick DMA]
ETM Ch 1: Timer0 alarm       → PCNT reset           [clear accumulators]
ETM Ch 2: GDMA CH0 total EOF → Timer0 stop          [DMA done → stop timer]
ETM Ch 3: GDMA CH0 total EOF → GPIO task CH0 SET    [signal completion on GPIO]
ETM Ch 4: (prime run)
```

No watchdog. DMA finishes → ETM stops the timer → CPU reads PCNT immediately. The completion GPIO can trigger a CPU interrupt in future milestones, but for now CPU polls.

---

## Test Plan

### Test 1: Single Buffer — All Positive
128 trits, all +1. Expect agree=128, disagree=0, dot=+128.

### Test 2: Single Buffer — All Negative
128 trits, all -1. Expect agree=0, disagree=128, dot=-128.

### Test 3: Single Buffer — All Zero
128 trits, all 0. Expect agree=0, disagree=0, dot=0. Perfect silence.

### Test 4: Single Buffer — Mixed
64 trits +1, 64 trits -1, interleaved. Expect agree=64, disagree=64, dot=0.

### Test 5: Single Buffer — Sparse
8 trits +1, 120 trits 0. Expect agree=8, disagree=0, dot=+8.

### Test 6: Descriptor Chain — 2 Buffers
Buffer A: 128 trits all +1. Buffer B: 128 trits all -1. Chain A→B.
Expect total agree=128, disagree=128, dot=0.

### Test 7: Descriptor Chain — Known Dot Product
Vector W = [+1, -1, +1, 0, -1, +1, ...] (128 trits, specific pattern)
Vector X = [+1, +1, -1, +1, 0, -1, ...] (128 trits, specific pattern)
CPU computes P[i] = W[i]×X[i], packs into buffer.
Expected dot product computed by CPU. Verify PCNT matches.

### Test 8: Descriptor Chain — 4 Buffers (512 trits)
Chain 4 buffers with known content. Verify accumulation across full chain.

### Test 9: Sparsity — Zero Vector
128-trit zero vector. Verify ALL PCNT units read zero. No edges, no energy.

### Test 10: Auto-Stop Timing
Measure time from DMA start to completion GPIO. Should be ~256us for 2 buffers at 1MHz (128 bytes × 4 dibits/byte × 1us/dibit = 512 dibit-clocks... actually 128 bytes at 2-bit PARLIO: 128 × 4 = 512 clocks at 1MHz = 512us). Verify no excess time (watchdog eliminated).

---

## SRAM Layout

```
0x0000: pat_buffer[0]   64 bytes  — first 128-trit vector
0x0040: pat_buffer[1]   64 bytes  — second 128-trit vector
0x0080: pat_buffer[2]   64 bytes  — third
0x00C0: pat_buffer[3]   64 bytes  — fourth
0x0100: pat_zero         64 bytes  — shared zero buffer (ground state)
0x0140: descriptors      48 bytes  — up to 4 descriptors × 12 bytes
```

---

## Success Criteria

- [ ] 1 dibit = 1 trit encoding verified (single edges, not amplified)
- [ ] Single-buffer dot product correct for all-pos, all-neg, mixed, sparse
- [ ] Descriptor chain accumulates correctly across 2+ buffers  
- [ ] Auto-stop via ETM (no watchdog timer)
- [ ] Raw PCNT integer value matches CPU-computed expected dot product
- [ ] Zero vectors produce perfect silence (all counts = 0)
- [ ] Total test pass: 10/10

---

## Risk: Edge Counting at 1 Trit per Dibit

Milestone 4 worked because 208 edges gave huge signal well above any noise floor. With 1 edge per trit, the signal is much weaker. A single stray edge (noise, glitch) corrupts the count by ±1. 

Mitigation: The zero-interleave encoding (trit then 00) creates a clean return-to-ground between every trit. PARLIO clocks at 1MHz = 1us per dibit = 2us per trit. Plenty of settling time. GPIO loopback propagation is <10ns. PCNT glitch filter can be enabled if needed (filters pulses shorter than configurable threshold).

This is the main thing to watch in silicon verification. If single-edge counting is unreliable at 1MHz, we can use a 2-edge encoding (trit, trit, 00, 00) at the cost of halving trits per buffer. But 1MHz should be well within tolerance.
