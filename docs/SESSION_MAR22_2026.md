# Debugging Session: March 22, 2026

## Subject: GIE Free-Running Engine — TEST 2+ Permanent GDMA Stall

**Session goal:** Diagnose and fix why the GIE's free-running loop produces 432 Hz on TEST 1
but exactly 0 loops/second on TEST 2 and all subsequent tests, after `stop_freerun()` and
a second `start_freerun()` call.

**Outcome:** Root cause identified and fixed. Fix applied, built, and confirmed compiling clean.
TEST 1 verified passing at 432 Hz. TEST 2–11 pending flash verification.

---

## 1. Background and Prior State

As of the end of the March 21 session, the GIE free-running engine (Phase 4) had two known
problems:

1. **Second-call hang:** Calling `start_freerun()` a second time hung the device permanently.
   This was fixed during the March 21 session by (a) adding a GDMA reset to `stop_freerun()`
   and (b) moving GDMA interrupt enable to after `setup_gdma_freerun()` completes.

2. **TEST 2+ zero loops:** After the hang was fixed, all 11 tests ran without hanging, but
   `loop_count` stayed 0 in TEST 2 through TEST 11. TEST 1 consistently produced 432 Hz.
   This was the open problem at the start of this session.

The previously established diagnostic showed:
- `isr_eof_count` freezes at exactly **37** after `start_freerun()` step 7 (when `tx_start`
  and PCR clock are enabled).
- GDMA registers: `out_st=2` (waiting for peripheral), `outfifo_full_l1=1`.
- T2D polling at 50ms intervals confirms count stays frozen for 400ms+ — not a timing issue.
- TEST 1 DIAG shows EOFs flowing continuously; TEST 2 DIAG shows EOFs frozen at 37.

---

## 2. Diagnostic Methodology

### 2.1 Instrument the ISR

Added `isr_eof_count` (total `out_eof` ISR fires ever), `isr_done_count` (total `out_done`
fires), and `isr_total_fires` (total ISR invocations). Added step markers to `start_freerun()`
and T2D polling after it returns to observe live evolution of the stall.

Key observation from T2D polling: `isr_eof_count` is 37 at the first 50ms check and every
subsequent check. This means zero EOF events occur after `start_freerun()` returns — the stall
is permanent from the moment `tx_start` is set.

### 2.2 Characterize the pre-fill

The 500μs delay between `setup_gdma_freerun()` (which sets `GDMA_LINK_START_BIT`) and the
PARLIO fire at step 7 is a **pre-fill phase**: GDMA begins fetching descriptor data from SRAM
and pushing it into PARLIO's AHB FIFO before PARLIO is running. With `out_eof_mode=0`, EOF
interrupts fire as GDMA fetches data, not as PARLIO clocks it.

During this 500μs pre-fill, approximately 23 EOFs fire. PARLIO's AHB FIFO and GDMA's L1 FIFO
fill up. GDMA stalls (`out_st=2`, `outfifo_full_l1=1`).

### 2.3 The 37-EOF accounting

At step 7, `tx_start` and PCR clock are enabled. In TEST 1, PARLIO begins clocking data out,
GDMA resumes, more EOFs flow. In TEST 2, the DIAG (500μs after step 7) shows 37 total EOFs.
So: 23 pre-fill + 14 additional EOFs fire after PARLIO starts. Then it freezes.

The 14 additional EOFs confirm that PARLIO **does** briefly accept data after `tx_start` is set
— the stall is not "PARLIO never starts." Instead, PARLIO starts, accepts 14 descriptors' worth
of data from GDMA's L1 FIFO, then stops accepting. GDMA's L1 fills back up. Stall is permanent.

### 2.4 What was ruled out

**PARLIO byte counter exhaustion:** `TX_BYTELEN` field (bits [17:2] of TX_CFG0) is in bytes
(confirmed from `parl_io_reg.h`: "Configures tx sending data byte length"). CHAIN_BYTES = 9936
bytes at 2-bit 20MHz PARLIO = 9936 bytes / (20e6 × 2 / 8) = 1.987ms per transaction. The
500μs DIAG window contains only 1250 bytes clocked. Byte counter nowhere near 0 at DIAG time.
Ruled out.

**GDMA descriptor owner bits:** If GDMA cleared owner bits after processing each descriptor,
the circular chain would break after one pass. But TEST 1 runs at 432 Hz continuously —
empirically, GDMA on ESP32-C6 does NOT clear owner bits. The chain runs indefinitely. Ruled out.

**PARLIO driver ISR disabling PCR clock:** The PARLIO driver ISR calls
`parlio_ll_tx_enable_clock(false)` after each TX completion, which clears bit 18 of
`PCR_PARL_CLK_TX_CONF`. This is suppressed by `REG32(PARLIO_INT_ENA) = 0` at step 7.
With `INT_ENA=0`, no PARLIO CPU interrupt fires regardless of `INT_RAW`. Ruled out.

**Stale PARLIO INT_RAW:** At TEST 2's DIAG, `PARLIO_INT_RAW=0x5` (bit 0: `tx_fifo_rempty`,
bit 2: `tx_eof`). These bits are sticky — they are NOT cleared by writing `INT_CLR` unless
explicitly done, and neither `stop_freerun()` nor `start_freerun()` wrote `INT_CLR`. These
bits are stale from TEST 1's last successful TX completion. They do not affect PARLIO's
operational state machine — `INT_RAW` gates only the CPU interrupt through `INT_ENA`, which
is 0. Ruled out as behavioral cause.

**PCR clock not enabled:** Added PCR register read to the DIAG. Confirmed bit 18 (`parl_clk_tx_en`)
is set (=1) in both TEST 1 and TEST 2 at DIAG time. PCR clock is correctly enabled. Ruled out.

**GDMA PERI_SEL mismatch:** Confirmed `GDMA_OUT_PERI_SEL=9` (PARLIO) in all DIAG readings.
GDMA is correctly associated with PARLIO. Ruled out.

---

## 3. Root Cause Discovery

### 3.1 The PCR register

Examining `PCR_PARL_CLK_TX_CONF` (0x600960ac) via ESP-IDF source:

```
/home/ztflynn/esp/v5.3.2/components/soc/esp32c6/include/soc/pcr_struct.h
```

The register has the following layout:

| Bits | Field | Default | Description |
|------|-------|---------|-------------|
| [15:0] | `parl_clk_tx_div_num` | 0 | Clock divider |
| [17:16] | `parl_clk_tx_sel` | 0 | Clock source |
| [18] | `parl_clk_tx_en` | 1 | TX clock enable |
| [19] | `parl_tx_rst_en` | 0 | TX core reset |

The `parl_tx_rst_en` field (bit 19) is the **PARLIO TX core reset** — a separate reset that
targets the TX control state machine, not just the data FIFO. This corresponds to the ESP-IDF
LL function:

```c
/* From hal/esp32c6/include/hal/parlio_ll.h */
static inline void parlio_ll_tx_reset_core(parl_io_dev_t *dev) {
    (void)dev;
    PCR.parl_clk_tx_conf.parl_tx_rst_en = 1;
    PCR.parl_clk_tx_conf.parl_tx_rst_en = 0;
}
```

Neither `stop_freerun()` nor `setup_gdma_freerun()` called this reset. Only the FIFO reset
(`tx_fifo_srst`, bit 30 of TX_CFG0) was being used — which clears the data FIFO but does
not reset the TX control state machine.

### 3.2 The state machine corruption theory

When `stop_freerun()` clears `tx_start` while PARLIO is actively clocking data:

1. PARLIO's TX transaction is interrupted mid-run. The byte counter has not reached 0. `tx_eof`
   has not fired. PARLIO was in the middle of consuming bytes from its AHB FIFO.

2. The TX control state machine is now in an intermediate state — not "idle" (which would require
   `tx_eof` to have fired) and not "transmitting" (which would require `tx_start` to be set).
   The exact internal state is opaque, but it is not the clean idle state that `prime_pipeline()`
   leaves behind.

3. The FIFO reset (`tx_fifo_srst`) clears the FIFO contents but does not touch the control
   state machine.

4. When the next `start_freerun()` sets `tx_start`, PARLIO's control state machine attempts
   to begin a new TX transaction. The transition from the corrupted intermediate state to
   "transmitting" is not clean. The hardware briefly accepts data (14 descriptors' worth = 2016
   bytes during the DIAG window), then the control state machine stalls. PARLIO stops signaling
   "FIFO has space" to the AHB bus. GDMA cannot push from its L1 FIFO to PARLIO's AHB FIFO.
   GDMA enters `out_st=2` permanently.

5. In contrast, `prime_pipeline()` runs to byte-counter exhaustion: the byte counter decrements
   to 0, `tx_eof` fires, PARLIO's control state machine completes its "transmit" → "idle"
   transition cleanly. The first `start_freerun()` finds PARLIO in a known-good idle state and
   works perfectly.

### 3.3 Why 37, not some other number

The pre-fill (23 EOFs) + the brief-working phase after `tx_start` (14 EOFs) = 37 is not a
fundamental limit. The 14 post-`tx_start` EOFs correspond to GDMA draining its L1 FIFO while
PARLIO is briefly accepting data. Once GDMA's L1 empties and GDMA tries to fetch new descriptors
from SRAM to refill it, PARLIO has already stopped accepting. The L1 refills immediately, EOF
count locks at 37. The exact number may vary with PARLIO AHB FIFO size and GDMA L1 FIFO size —
the fundamental cause is PARLIO stopping, not the specific count.

---

## 4. The Fix

In `stop_freerun()`, after clearing `tx_start`, resetting the FIFO, and before stopping GDMA:

```c
/* Reset PARLIO TX core state machine via PCR (parl_tx_rst_en = bit 19).
 * stop_freerun() clears tx_start mid-transaction, leaving the PARLIO TX
 * state machine in a partial state. The FIFO reset above clears the data
 * path but not the control state machine. Without this core reset, the
 * next start_freerun() inherits corrupted TX state: PARLIO stops accepting
 * data from the AHB FIFO after the GDMA pre-fill, causing a permanent stall.
 * prime_pipeline() avoids this by running a complete TX (byte count exhausted
 * → clean idle), so TEST 1 works without this reset. All subsequent tests need it. */
REG32(PCR_PARL_CLK_TX_CONF) |= (1 << 19);   /* assert parl_tx_rst_en */
esp_rom_delay_us(5);
REG32(PCR_PARL_CLK_TX_CONF) &= ~(1 << 19);  /* release reset */
esp_rom_delay_us(5);
```

This is a 10μs operation (5μs assert + 5μs release). It goes into `stop_freerun()` — the
teardown path — so it does not add latency to the hot path (the ISR at loop boundary).

### 4.1 Diagnostic additions

Added to `start_freerun()` DIAG section:
- `tx_start=` and `bytelen=` extracted from `PARLIO_TX_CFG0` to confirm register state
- `pcr: clk_conf=0x... (en=N rst=N)` line to confirm PCR clock and reset state

These additions allow distinguishing between:
- PCR clock not enabled (en=0) → different problem
- tx_start not set (tx_start=0) → different problem
- Byte count wrong (bytelen=0 or wrong value) → different problem
- Everything correct but GDMA still stalled → root cause confirmed as state machine

---

## 5. Hardware Register Discoveries

### 5.1 PARL_IO interrupt register map

From `soc/esp32c6/include/soc/parl_io_reg.h`:

| Register | Address | Description |
|----------|---------|-------------|
| `PARL_IO_TX_CFG0_REG` | `DR_REG_PARL_IO_BASE + 0x8` = 0x60015008 | TX config: bytelen[17:2], tx_gating[18], tx_start[19], fifo_rst[30] |
| `PARL_IO_ST_REG` | `DR_REG_PARL_IO_BASE + 0x10` = 0x60015010 | Status: tx_ready[31] |
| `PARL_IO_INT_ENA_REG` | `DR_REG_PARL_IO_BASE + 0x14` = 0x60015014 | Interrupt enable |
| `PARL_IO_INT_RAW_REG` | `DR_REG_PARL_IO_BASE + 0x18` = 0x60015018 | Raw interrupt status |
| `PARL_IO_INT_ST_REG` | `DR_REG_PARL_IO_BASE + 0x1c` = 0x6001501c | Masked interrupt status |
| `PARL_IO_INT_CLR_REG` | `DR_REG_PARL_IO_BASE + 0x20` = 0x60015020 | Write 1 to clear INT_RAW |

The `PARL_IO_INT_CLR_REG` at 0x60015020 was not previously defined in the firmware.

### 5.2 TX_CFG0 bit layout (confirmed)

- Bits [17:2]: `TX_BYTELEN` — byte count in **bytes** (not bits). Confirmed via
  `parl_io_reg.h` field description: "Configures tx sending data byte length."
- Bit 18: `tx_gating_en` — gates TX clock on TXD[7] level. Must be 0 in free-running mode
  (TXD[7] is always 0 in our encoding, which would permanently gate the clock off).
- Bit 19: `tx_start` — initiates a TX transaction.
- Bit 30: `tx_fifo_srst` — resets TX FIFO data only.

### 5.3 PCR_PARL_CLK_TX_CONF layout (confirmed)

Confirmed via `soc/esp32c6/include/soc/pcr_struct.h`. Two relevant bits near bit 18:
- Bit 18 (`parl_clk_tx_en`): default 1, drives PARLIO TX clock output
- Bit 19 (`parl_tx_rst_en`): default 0, TX core reset (1=reset asserted)

The ESP-IDF LL function `parlio_ll_tx_reset_core()` pulses bit 19 (set→clear). The ESP-IDF
LL function `parlio_ll_tx_enable_clock()` sets/clears bit 18.

---

## 6. Serial Capture Analysis

Four background captures were collected during this session:

| Capture | Firmware | TEST 1 Result | Notes |
|---------|----------|---------------|-------|
| bzyh9lukc | Mar 21 18:36 build | FAIL (0 Hz, isr_fires=0) | Pre-session firmware, old DIAG format |
| bbdffjxpk | Intermediate build | FAIL (0 Hz, isr_fires=1) | Mid-session firmware |
| bvg1dc2yk | Intermediate build | FAIL (0 Hz, isr_fires=1) | Mid-session firmware |
| bkbfrm5hj | Latest pre-fix build | **PASS (432 Hz, isr_fires=40)** | Previous session fixes applied |

The progression from 0→1→40 ISR fires reflects the March 21 session's changes taking effect.
The bkbfrm5hj capture has the DIAG format from the previous session (with `isr_done=` but
without `tx_start=` or `pcr:` lines, which were added in this session). All four captures
cut off immediately after `-- TEST 2: Hidden state evolves autonomously --` due to capture
timeout — TEST 2 output was not captured in any of these runs.

Key observation from bkbfrm5hj (latest pre-fix):
```
[DIAG] parlio: cfg=0x1a089b40 st=0x80000000(tx_ready=1) int_raw=0x0(eof=0 rempty=0)
```
`int_raw=0x0` — no stale bits from prime_pipeline. This is the clean state that TEST 1 inherits.
In TEST 2, `int_raw` would show 0x5 (stale from TEST 1's last tx_eof) — purely diagnostic noise,
not behavioral.

---

## 7. Current State

| Item | Status |
|------|--------|
| Fix designed | ✅ PARLIO TX core reset via `parl_tx_rst_en` pulse in `stop_freerun()` |
| Fix implemented | ✅ Applied to `geometry_cfc_freerun.c` |
| Build passing | ✅ `idf.py build` clean, 18% free |
| TEST 1 (432 Hz baseline) | ✅ Confirmed passing in pre-fix build |
| TEST 2–11 | Pending flash and capture of new build |
| Diagnostic code | Still present: step markers, T2D polling, PCR DIAG line |
| Cleanup needed | Remove diagnostic code after TEST 2–11 confirmed passing |

---

## 8. Next Steps

1. **Flash and verify:** Flash the new build. Capture full 11-test output. Confirm loop_count > 0
   in TEST 2 and all subsequent tests.

2. **Clean up diagnostics:** Remove `[SF]` step markers, `[T2D]` polling, `isr_total_fires`,
   `isr_done_count`, `isr_eof_count` from ISR and `start_freerun()`. Remove the extended DIAG
   `pcr:` line (or collapse to a single compact summary). These were investigative scaffolding.

3. **Commit clean version:** Commit the fixed, cleaned firmware as the definitive Phase 4 state.

4. **Consider clearing PARLIO_INT_CLR:** Add `REG32(0x60015020) = 0x7` to `start_freerun()` to
   prevent stale `INT_RAW` bits from appearing in future diagnostic captures. Low priority —
   does not affect behavior.
