# Peripherals-Only Compute: How the GIE Works

**The Geometry Intersection Engine (GIE) runs a 64-neuron ternary neural network at 430 Hz
with both CPU cores asleep. The compute happens entirely inside GDMA, PARLIO, and PCNT —
three hardware peripherals that were designed for data transfer and event counting,
not neural network inference.**

This document explains the architecture from first principles, bottom up.

---

## 1. The Problem: Dot Products Without a CPU

A neuron computes a **dot product**: multiply each weight by the corresponding input, sum
everything up. If the sum is positive the neuron fires; if negative, it doesn't.

Normally this requires a CPU or GPU running multiply-accumulate instructions in a loop.
The GIE does it differently — it turns the dot product into a counting problem, and uses
hardware counters to count.

### Why That Works: Ternary Values

Every weight and every input value in the GIE is constrained to one of three values:
**-1, 0, or +1**. This is called a *ternary* constraint.

When you multiply two ternary values together, the result is always one of three outcomes:

| Weight | Input | Product | Meaning |
|--------|-------|---------|---------|
| +1 | +1 | +1 | **agree** |
| -1 | -1 | +1 | **agree** |
| +1 | -1 | -1 | **disagree** |
| -1 | +1 | -1 | **disagree** |
| 0 | anything | 0 | **abstain** |

So instead of computing a product, you ask a yes/no question: *do these two values agree
or disagree?* The dot product becomes:

```
dot = (number of agrees) - (number of disagrees)
```

That is a subtraction of two counts. Hardware counters can do this without any CPU
involvement — you just need something to generate the pulses and something to count them.

### The Encoding

To represent a ternary pair (weight_i, input_i) in hardware, encode it as two bits on two wires:

- **Wire A (GPIO 4):** pulse once if the pair **agrees** (+1 × +1 or -1 × -1)
- **Wire B (GPIO 5):** pulse once if the pair **disagrees** (+1 × -1 or -1 × +1)
- **Neither wire:** if either value is 0 (abstain)

After all 128 pairs for one neuron have been pulsed:

```
dot = count(Wire A pulses) - count(Wire B pulses)
```

This is exact. No approximation. No floating point. No multiplication anywhere.

---

## 2. The Hardware

Four pieces of silicon do all the work. None of them is a CPU.

```
┌─────────────────────────────────────────────────────────────────────┐
│                     THE GIE SIGNAL PATH                             │
│                                                                     │
│  ┌──────────┐     ┌──────────┐     ┌──────────────┐     ┌───────┐  │
│  │   GDMA   │────▶│  PARLIO  │────▶│  GPIO 4, 5   │────▶│ PCNT  │  │
│  │          │     │          │     │  (loopback)  │     │       │  │
│  │ Conveyor │     │ Flasher  │     │    Wire      │     │Tally  │  │
│  └──────────┘     └──────────┘     └──────────────┘     └───────┘  │
│       │                                                      │      │
│       │                                          ISR reads here     │
│       │                                          (CPU wakes ~2μs)   │
│       ▼                                                             │
│  [Circular descriptor chain in SRAM]                                │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.1 GDMA — The Conveyor Belt

GDMA (General-purpose Direct Memory Access) is a hardware engine that moves data from
memory to peripherals without CPU involvement. It reads from a **descriptor chain** — a
linked list of records in SRAM, each pointing to a block of data and a pointer to the
next descriptor.

For the GIE, GDMA reads pre-encoded weight×input products from SRAM and streams them
into PARLIO's input FIFO. It advances through the chain automatically, one descriptor
at a time, as PARLIO consumes data.

When GDMA reaches the end of the chain, it loops back to the beginning — the chain is
circular. GDMA never stops unless explicitly halted.

**The chain layout:**

```
[5 dummy descriptors] → [neuron 0: 128 entries] → [neuron 1: 128 entries] → ... → [neuron 63: 128 entries]
         ▲                                                                                    │
         └────────────────────────────────────────────────────────────────────────────────────┘
                                        loops back forever
```

Each descriptor for a real neuron has its `eof` flag set at the last entry, which causes
GDMA to fire an interrupt when that descriptor is consumed — the signal that one neuron
is complete.

The 5 dummy descriptors at the start of each loop output zeros (no pulses on either wire).
They serve as a timing buffer: PARLIO needs a brief settling window between loops, and the
dummies provide it without the CPU doing anything.

**Total chain size:** 138 descriptors (5 dummy + 64×2, using pairs for alignment), 9,936
bytes of encoded data. GDMA cycles through all of it in about 2.32 milliseconds per loop
— 430 times per second.

### 2.2 PARLIO — The Flasher

PARLIO (Parallel IO) is normally used to drive parallel data buses — LCD panels, LED
matrices, shift registers. Its job is to clock data out on multiple GPIO pins simultaneously
at a fixed rate.

In the GIE it is configured as a **2-bit parallel shift register** running at 20 MHz. It
reads 2-bit symbols from its FIFO and drives:
- GPIO 4 (PARLIO data bit 0): the "agree" wire
- GPIO 5 (PARLIO data bit 1): the "disagree" wire

Each 2-bit symbol is clocked out in 50 nanoseconds. For 128 weight×input pairs, that's
6.4 microseconds of clocking per neuron. For 64 neurons with separators, one full loop
takes about 1.987 milliseconds of PARLIO output time.

PARLIO has a **byte counter** (`TX_BYTELEN`): it counts how many bytes it has clocked out
and stops when the count is exhausted. The ISR re-arms this counter at each loop boundary
by clearing and resetting `tx_start`. This prevents PARLIO from overrunning into the next
loop's data.

**Key errata:** PARLIO's TX control state machine (distinct from its data FIFO) must be
explicitly reset via `parl_tx_rst_en` (PCR register bit 19) in between runs. Clearing only
`tx_start` and resetting the FIFO leaves the control FSM in a partially-complete state.
On the next `start_freerun()` call, PARLIO accepts data for ~14 more descriptors from the
GDMA pre-fill phase, then stops accepting — causing a permanent GDMA stall. Pulsing
`parl_tx_rst_en` (assert high, wait 5μs, release) is the only complete reset. See
`HARDWARE_ERRATA.md` for the full analysis.

### 2.3 GPIO Loopback — The Wire

GPIO 4 and GPIO 5 are physically connected back to the input side of PCNT through the
ESP32-C6's GPIO matrix. There is no external wiring required — the matrix routes the
PARLIO output signals directly to the PCNT input signals in silicon.

- PARLIO data bit 0 → GPIO 4 → PCNT signal input (counts agree pulses)
- PARLIO data bit 1 → GPIO 5 → PCNT control input (used as level gate)

The level gate is critical: PCNT Unit 0 counts GPIO 4 edges only when GPIO 6 is high
(Y_POS, held high during the run) and GPIO 5 is low. PCNT Unit 1 counts GPIO 5 edges
under the inverse condition. This implements the agree/disagree separation in hardware.

No CPU reads or writes to the GPIO path during a run. Electrons simply flow.

### 2.4 PCNT — The Tallier

PCNT (Pulse Counter) is a hardware counter that increments or decrements in response to
GPIO edge events. It has two counter channels per unit and two independently configurable
units.

In the GIE:
- **PCNT Unit 0** counts agree pulses (GPIO 4 edges when the level condition is met)
- **PCNT Unit 1** counts disagree pulses (GPIO 5 edges under the inverse condition)

After all 128 pairs for one neuron have been clocked through, the counters hold the
accumulated agree and disagree counts. The dot product is:

```
dot = PCNT_U0_CNT - PCNT_U1_CNT
```

This is a 16-bit signed register read. Two reads, one subtraction. That is the entire
"multiply-accumulate" operation for 128 trit pairs.

**Clock domain note:** PARLIO runs at 20 MHz and PCNT runs at the peripheral bus clock.
There is a 1–2 clock pipeline delay between PARLIO outputting the last pulse and PCNT
registering it. The ISR includes a 200-iteration busy loop (~5μs at 160 MHz) after the
EOF interrupt fires and before reading PCNT, to let the pipeline drain. Without this
drain, the last 1–2 pulses are missed and dot products are consistently off by 1.

---

## 3. One Neuron, Step by Step

The following sequence happens 64 times per loop, once per neuron, with no CPU involvement
except the brief ISR at the end of each neuron.

```
Step 1: GDMA advances to the next neuron's first descriptor.
        It begins feeding encoded 2-bit symbols into PARLIO's FIFO.

Step 2: PARLIO reads symbols from its FIFO and clocks them out on GPIO 4/5 at 20 MHz.
        Each symbol takes 50 ns. 128 symbols take 6.4 μs.

Step 3: GPIO 4 and GPIO 5 route through the matrix to PCNT.
        PCNT Unit 0 clicks for each agree pulse.
        PCNT Unit 1 clicks for each disagree pulse.

Step 4: GDMA reaches the last symbol in this neuron's descriptor block.
        The eof=1 flag is set. GDMA fires an interrupt.

Step 5: The ISR wakes. (CPU was asleep until this moment.)
        It waits 200 iterations for the PCNT pipeline to drain.
        It reads PCNT_U0_CNT and PCNT_U1_CNT.
        dot = agree - disagree.
        It stores dot in isr_agree[idx] and isr_disagree[idx].
        isr_count is incremented.

Step 6: PCNT is not cleared here. The next neuron's counts will be cumulative.
        The ISR computes delta-dots at loop boundary time by differencing cumulative
        snapshots. (agree[i] - agree[i-1]) - (disagree[i] - disagree[i-1]) = dot_i.

Step 7: The ISR returns. CPU goes back to sleep.
        GDMA has already moved to the next descriptor and is feeding the next neuron.
```

**CPU active time per neuron:** approximately 2 microseconds out of every 32.
**CPU active fraction:** ~6%. The other 94% is pure peripheral operation.

---

## 4. The Loop Boundary

After neuron 63 completes (isr_count reaches CAPTURES_PER_LOOP = 69, including dummies),
the ISR performs the loop boundary work. This happens inside the same ISR call, while
GDMA has already looped back and is streaming the dummy descriptors — providing a natural
processing window.

```
Loop boundary sequence (all inside ISR, ~15μs):

1. Call isr_loop_boundary():
   - Compute per-neuron dots from cumulative PCNT snapshots.
   - Run CfC update: for each neuron, compute new hidden state trit.
     (sign of dot → {-1, 0, +1}, combined via liquid time constant formula)
   - If blend enabled: re-encode new W×X products into the descriptor chain.
     (GDMA will read the updated encoding on the NEXT loop)
   - If blend disabled (Phase 3/4): skip re-encode, neurons HOLD their state.

2. Reset isr_count = 0.
   Clear isr_agree[] and isr_disagree[] arrays.

3. Stop PARLIO (clear tx_start), triple-clear PCNT.
   Reprogram PARLIO byte counter (CHAIN_BYTES = 9,936).
   Re-enable tx_start and PCR clock.

4. Clear any stale GDMA interrupt flags.

5. Return from ISR.
```

By the time the dummy descriptors are exhausted and GDMA reaches neuron 0 again, the new
encoding is in place and the loop continues with the updated weights.

**Loop rate:** 430.8 Hz confirmed on hardware (432 loops in 1.003 seconds, TEST 1).
**Per-loop duration:** ~2.32 milliseconds.
**Loop boundary processing budget:** ~0.35 milliseconds (the 5 dummy descriptor window).

---

## 5. The Circular Chain in Detail

The GDMA descriptor chain is allocated in SRAM at initialization and never moved. Each
descriptor is a 12-byte structure:

```
struct gdma_descriptor {
    uint32_t config;    // size, eof flag, owner bit
    uint32_t buf_ptr;   // pointer to data buffer
    uint32_t next_ptr;  // pointer to next descriptor (circular: last → first)
};
```

The 64 neuron descriptors each point to a 144-byte buffer (128 weight×input pairs encoded
as 2-bit symbols packed into bytes, plus 16 bytes of separator zeros). The last descriptor
points back to the first dummy descriptor.

**Owner bit:** GDMA requires the owner bit to be set to 1 (DMA owns the descriptor) before
it can fetch from it. On ESP32-C6, GDMA does NOT clear the owner bit after consuming a
descriptor in circular mode — this was confirmed empirically by the 430 Hz continuous
operation in TEST 1. The ISR does not need to reset owner bits, unlike some other ESP32
variants.

**out_eof_mode=0:** GDMA fires the EOF interrupt when it *fetches* data from SRAM, not when
PARLIO finishes clocking it out. This means EOFs fire slightly ahead of the actual PARLIO
output. The PCNT drain loop compensates. It also means ~23 "phantom" EOF interrupts fire
during the GDMA pre-fill phase (before `tx_start` is set) — these are counted and ignored;
they do not accumulate PCNT data because PARLIO is not running yet.

---

## 6. The Full Signal Path: Wireless to Classification

Here is the complete end-to-end flow with all layers visible:

```
┌─────────────────────────────────────────────────────────────────────────┐
│  BOARD B (ESP32-C6, espnow_sender.c)                                    │
│                                                                         │
│  Cycles through 4 patterns at different rates and payloads:             │
│    P0: 10 Hz, pattern_id=0, fixed payload                               │
│    P1: burst (~20 Hz), pattern_id=1, counter payload                    │
│    P2: 2 Hz, pattern_id=2, fixed payload                                │
│    P3: 10 Hz, pattern_id=3, different payload (same rate as P0)         │
│                                                                         │
│  Sends ESP-NOW unicast to Board A's Wi-Fi STA MAC every cycle.          │
└─────────────────────────────────────────────────────────────────────────┘
        │ 2.4 GHz RF, ~-48 dBm, channel 1
        ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  ESP-NOW RECEIVER (HP core, interrupt-driven)                           │
│                                                                         │
│  Receives packet: {rssi, pattern_id, sequence, payload[8]}             │
│  Encodes into 128-trit input vector X:                                  │
│    trits [0..15]:   RSSI mapped to bipolar range                        │
│    trits [16..23]:  pattern_id one-hot in ternary                       │
│    trits [24..31]:  payload bytes mapped to {-1,0,+1}                   │
│    trits [32..127]: inter-packet timing, sequence deltas, etc.          │
│                                                                         │
│  Writes X into the CfC input buffer (shared memory).                    │
└─────────────────────────────────────────────────────────────────────────┘
        │ writes to cfc.input[] in SRAM
        ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  GIE: GDMA + PARLIO + PCNT (no CPU during operation)                   │
│                                                                         │
│  Circular DMA chain streams pre-encoded W×X products.                  │
│  PARLIO clocks 2-bit symbols at 20 MHz onto GPIO 4/5.                  │
│  GPIO loopback feeds PCNT, which tallies agree/disagree.               │
│  ISR fires on each neuron EOF: reads PCNT, stores dot.                  │
│                                                                         │
│  At loop boundary (every 2.32 ms):                                      │
│    dot[n] = agree[n] - disagree[n]   (for n in 0..63)                  │
│    hidden[n] = cfc_update(dot[n], hidden[n])                            │
│    (new encoding written back to SRAM for next loop)                   │
│                                                                         │
│  Rate: 430.8 Hz  |  CPU active: ~6% of time  |  Cores: asleep          │
└─────────────────────────────────────────────────────────────────────────┘
        │ cfc.hidden[32] updated 430× per second
        ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  TriX CLASSIFICATION (inside ISR, at 711 Hz)                           │
│                                                                         │
│  On every GIE EOF (not just loop boundary), the ISR computes a          │
│  dot product between the current cumulative PCNT snapshot and each      │
│  of 4 stored pattern signatures.                                        │
│                                                                         │
│  The highest-dot signature wins → classification vote.                  │
│  Votes are packed into a reflex_signal and written to the channel.      │
│                                                                         │
│  ISR classification rate: 711 Hz (24,357 in ~34 seconds, TEST 11)      │
└─────────────────────────────────────────────────────────────────────────┘
        │ reflex_channel_t (fence-ordered, ISR→main, 14.5μs avg latency)
        ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  LP CORE (RISC-V, 16 MHz, ~30μA, wakes 100× per second)               │
│                                                                         │
│  Reads cfc.hidden[32] from shared memory.                               │
│  Runs its own CfC step (16 neurons, 48-trit input).                     │
│  Searches VDB (NSW graph, 64 nodes) for nearest stored memory.          │
│  Optionally blends nearest memory back into lp_hidden (CMD 5).         │
│                                                                         │
│  Sleeps between wakes. Total active time: <1% of elapsed time.         │
└─────────────────────────────────────────────────────────────────────────┘
        │ lp_hidden[16], classification result
        ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  HP CORE (RISC-V, 160 MHz, ~15 mA, on-demand only)                    │
│                                                                         │
│  Reads classification result.                                           │
│  Outputs: "Pattern 0 / 1 / 2 / 3"                                      │
│                                                                         │
│  Accuracy: 100% (32/32, TEST 11, March 22 2026)                        │
│  Rate baseline (packet timing only): 84% — cannot distinguish P0/P3   │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 7. Why the GIE Beats a Rate-Only Classifier

The test includes two patterns — P0 and P3 — that both send at ~10 Hz. A classifier that
looks only at inter-packet timing cannot distinguish them. It will score at most 84% because
25% of the test samples (the P0/P3 overlap) are ambiguous.

The GIE encodes packet **content** (payload bytes, pattern_id field) into the input vector.
P0 and P3 have different payloads, so their encoded input vectors differ. The GIE's hidden
state evolves differently under P0 input than under P3 input. The signatures for P0 and P3
are therefore distinguishable — the cross-dot between them is low.

From the TEST 11 XOR mask decomposition (March 22, 2026):

| Signal field | Share of discriminating weight |
|-------------|-------------------------------|
| Payload bytes (trits 24–31) | **47%** |
| Timing (inter-packet gaps) | **37%** |
| RSSI | 9% |
| Pattern ID trits | 5% |

The system is primarily classifying by content (47%) and rhythm (37%), not by signal strength.
This is what the hardware dot-product accumulator makes possible: every bit of the input
vector contributes to the hidden state, including payload bytes that a simple rate counter
would ignore entirely.

---

## 8. What Makes This "Peripherals-Only"

The phrase "peripherals-only compute" means the neural network inference loop — the
multiply-accumulate operations at the core of every neuron — runs entirely inside hardware
that was not designed to be a processor:

| What normally needs a CPU | What does it in the GIE |
|---------------------------|------------------------|
| Loop over weight×input pairs | GDMA descriptor chain advance |
| Multiply weight × input | 2-bit encoding (precomputed at weight-load time) |
| Accumulate products | PCNT edge counting |
| Detect end of neuron | GDMA eof flag → hardware interrupt |
| Move to next neuron | GDMA next_ptr (automatic) |
| Loop back to start | Circular chain (last descriptor → first) |

The CPU (both HP and LP cores) is absent during the inner loop. It appears only at the
loop boundary for ~15 microseconds to run the CfC update formula and re-arm the counters.

The CfC update itself uses no multiplication: it is a sequence of sign comparisons, additions,
and right-shifts, implemented in integer arithmetic. The ternary encoding of new W×X products
is a table lookup per trit pair.

**Nothing in the compute path exercises the RISC-V multiply extension (M). Nothing uses
floating point. Everything is exact.**

---

## 9. Resource Summary

| Resource | Configuration | Why |
|----------|--------------|-----|
| GDMA channel 0 | Circular chain, out_eof_mode=0 | Owned by PARLIO TX; eof fires on fetch |
| PARLIO TX | 2-bit parallel, 20 MHz, GPIO 4/5 | Fastest reliable rate on ESP32-C6 C6 |
| PCNT Unit 0 | Counts GPIO 4 edges (agree) | Rising-edge count, level-gated by GPIO 6 |
| PCNT Unit 1 | Counts GPIO 5 edges (disagree) | Rising-edge count, inverse level gate |
| GPIO 4 | PARLIO data bit 0 → PCNT signal | PARLIO_TX_DATA0_IDX = 47 |
| GPIO 5 | PARLIO data bit 1 → PCNT signal | PARLIO_TX_DATA1_IDX = 48 |
| GPIO 6 | Y_POS (static high during run) | PCNT level gate |
| GPIO 7 | Y_NEG (static low) | Reserved |
| SRAM | 9,936 bytes for descriptor chain | Fits in LP SRAM with room |
| ISR | LEVEL3, IRAM-resident | Cannot be preempted; no cache miss |
| PCR | parl_tx_rst_en (bit 19) | Core FSM reset between runs |

---

## 10. The One-Sentence Version

**The Reflex turns neural network inference into a counting problem, then uses hardware
counters to count — so the CPU never has to.**

---

*Verified on ESP32-C6FH4 (QFN32) rev v0.2, ESP-IDF v5.3.2, March 22, 2026.*
*11/11 tests passing. 430.8 Hz. 100% classification accuracy. No floating point.*
*No multiplication. No GPU. No neural network framework.*
