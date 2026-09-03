# Fabric-Side Ternary Multiply — VERIFIED ON SILICON

**September 2, 2026.** `data/sep02_2026/fabric_multiply.log`

**Result: 10/10 vectors match the CPU reference exactly. The ternary multiply
runs in the peripheral fabric. The CPU performs zero multiplies.**

## What was wrong

The production engine streams **CPU-premultiplied** products on a 2-bit PARLIO
lane (GPIO 4/5) and holds GPIO 6/7 at a static level. So the fabric only
population-counts; the CPU performs every ternary multiply — 2048 `tmul()` calls
per GIE loop, ~880k multiplies/second, ~285 µs per loop (12% of the period).

But the PCNT channels were **already wired as a multiply table**:

```
agree    = (X_POS edge & Y_POS level) | (X_NEG edge & Y_NEG level)   -> +1
disagree = (X_POS edge & Y_NEG level) | (X_NEG edge & Y_POS level)   -> -1
dot      = agree - disagree
```

Y was simply never streamed.

## What it took

Set PARLIO `data_width = 4`, map GPIO 4,5,6,7 (PARL_TX_DATA0..3 = signals
47..50), and stream both operands. PCNT config unchanged.

**The subtlety that made the first attempt read exactly zero:** PARLIO drives
all four lanes off the same clock edge, so Y transitions simultaneously with X.
PCNT samples the *level* input through GPIO-matrix synchroniser flops, so at X's
edge Y still reads its previous value — always 0 from the return-to-zero nibble.
Every channel gates off. Zero counts, not wrong counts.

**Fix — present Y one nibble early:**

```c
static uint8_t fm_encode(int8_t a, int8_t b) {
    uint8_t xb = 0, yb = 0;
    if (a > 0) xb |= 0x1;  if (a < 0) xb |= 0x2;   /* GPIO4/5 */
    if (b > 0) yb |= 0x4;  if (b < 0) yb |= 0x8;   /* GPIO6/7 */
    return (uint8_t)(yb | ((yb | xb) << 4));
}
```

LSB pack order emits the low nibble first: Y alone (settles, no X edge), then
Y+X (X's edge fires with Y stable). X returns to 0 at the next pair's low
nibble, so return-to-zero is preserved. Still one byte per trit product.

**Return-to-zero is load-bearing, not padding.** PCNT counts edges; without RZ,
two consecutive same-sign products produce one edge. This is also why the
production 2-bit encoding uses 80 bytes for 160 trits rather than 40 — the
"wasted" half is the RZ.

## The diagnostic ladder

The first attempt read `agree=0, disagree=0` on every vector. Zero counts is a
dead path, not a wrong multiply, so it said nothing about the hypothesis. The
ladder isolated it:

| Rung | Tests | Result |
|---|---|---|
| L1 | CPU-driven edge → PCNT | agree=20, expect 20 — **OK** |
| L2 | PARLIO driver TX, 2-bit, production geometry | agree=16, expect 16 — **OK** |
| L2.5 | 4-bit width, Y held static | agree=16 — **OK, width is fine** |
| L3 | 4-bit, both operands streamed | agree=0 — **dead** |

L2.5 was decisive: 4-bit width works, so the fault was exclusively in streaming
Y — which pointed at the level-sampling race and gave the fix.

## Verification

| Vector | fabric | cpu | |
|---|---|---|---|
| all +1×+1 | +64 (a=64 d=0) | +64 | MATCH |
| all −1×−1 | +64 (a=64 d=0) | +64 | MATCH |
| all +1×−1 | −64 (a=0 d=64) | −64 | MATCH |
| all zeros | 0 | 0 | MATCH |
| alternating | 0 (a=32 d=32) | 0 | MATCH |
| random ×5 | +13, −3, −8, −7, −5 | same | MATCH |

64 trit pairs per vector, 30% sparsity on the random ones.

## What this buys

- Step 5's 2048 `tmul()` per loop → **zero** (~285 µs, 12% of the loop period)
- `premultiply_all()` → zero
- Per-packet input re-encode (8192 `tmul`) → zero
- Makes "the CPU never computes a dot product" literally true *including the
  products*, which it is not today

## Not yet done

This used the PARLIO driver's blocking transmit. Production drives PARLIO
bare-metal (`tx_bytelen` + `tx_start`) with a circular GDMA chain. Porting
requires:

1. Buffer format change: weights and state interleaved per the v2 encoding
   rather than pre-multiplied products.
2. The hidden-state feedback path: hidden changes per loop, so its nibbles must
   be rewritten each loop — far cheaper than 2048 multiplies, but not free.
3. Re-verify at the project's own standard: **64/64 exact vs CPU reference**
   under the free-running engine (the M5/M8 bar), not just blocking transmits.
4. Confirm the Silicon Interlock (USB-JTAG on GPIO 4–7) tolerates PARLIO
   driving 6/7.

## Credit

Tripp called this. My initial analysis said the multiply could move to the
fabric but predicted a 2× throughput win that does not exist (RZ is required),
and my first implementation failed on a timing detail I had flagged as a risk
and then encoded wrongly anyway. The idea was right and the hardware was already
wired for it.
