# ESP32-C6 Hardware Errata & Workarounds

Discovered during autonomous computation fabric development on ESP32-C6FH4 (QFN32) revision v0.2.

## ETM (Event Task Matrix)

### ETM GPIO toggle/set/clear tasks do not fire

**Symptom:** Wiring an ETM event to a GPIO toggle/set/clear task produces no output on the pin.

**Affected:** ESP32-C6 rev v0.2. All GPIO ETM task types (toggle, set, clear).

**Root cause:** Suspected hidden enable register or silicon errata. The GPIO ETM task IDs are documented in `soc_etm_source.h` but the tasks do not execute.

**Workaround:** Use PARLIO for GPIO output instead. PARLIO's 4-bit parallel output on GPIO 4-7 works reliably as an ETM→GDMA→PARLIO→GPIO chain.

**Reference:** `ESPRESSIF_ETM_ISSUE.md` in repo root.

### ETM timer stop task does not stop IDF gptimer

**Symptom:** `TIMER0_TASK_CNT_STOP_TIMER0` (ETM task 92) does not stop a timer started via `gptimer_start()`.

**Root cause:** The IDF gptimer HAL likely uses a different start/stop mechanism than the raw timer group EN bit that ETM controls.

**Workaround:** Stop timer0 from an ISR callback by directly clearing the EN bit:
```c
volatile uint32_t *t0cfg = (volatile uint32_t*)0x60008000;
*t0cfg &= ~(1U << 31);  // TIMG0 T0CONFIG, bit 31 = EN
```

### ETM base address is 0x60013000, not 0x600B8000

**Symptom:** All ETM register reads/writes return 0 or have no effect.

**Root cause:** The ETM base address in early documentation and some code examples is wrong. The correct address is `DR_REG_SOC_ETM_BASE = 0x60013000` (from `soc/reg_base.h`). CLK_EN is at `ETM_BASE + 0x1A8`, not `+0x00`.

**Also:** PCR ETM conf register is at `PCR_BASE + 0x98`, not `+0x90` (0x90 is INTMTX).

### IDF startup disables ETM bus clock

**Symptom:** ETM registers are unreadable/unwritable after IDF boot despite correct base address.

**Root cause:** `esp_system/port/soc/esp32c6/clk.c` line 270 calls `etm_ll_enable_bus_clock(0, false)` during startup, disabling the ETM bus clock.

**Workaround:** Re-enable via PCR before any ETM register access:
```c
volatile uint32_t *conf = (volatile uint32_t*)0x60096098;  // PCR_SOC_ETM_CONF
*conf = (*conf & ~(1 << 1)) | (1 << 0);  // clk_en=1, rst=0
REG32(0x600131A8) = 1;  // ETM internal clock gate
```

### LEDC timer cannot be resumed after ETM-triggered pause

**Symptom:** After ETM task `TASK_LEDC_T0_PAUSE` fires, the LEDC timer stays frozen. Neither `ledc_timer_resume()`, clearing bit 23 of `LEDC_TIMER0_CONF_REG`, nor full `ledc_timer_config()` reconfigure restarts it.

**Workaround:** Don't use LEDC timer pause/resume for result latching. Use PCNT ISR callbacks instead.

## GDMA

### GDMA LINK_START may transmit immediately despite ETM_EN

**Symptom:** Setting `GDMA_LINK_START_BIT` with `ETM_EN_BIT` active causes ~10-17 stray PCNT counts before the ETM-triggered GDMA start fires.

**Root cause:** `LINK_START` appears to begin DMA transfer immediately, ignoring `ETM_EN`. The data reaches PARLIO and drives GPIOs, creating PCNT edges before the intended start.

**Workaround:** Defer PARLIO `TX_START` until after GDMA is armed and PCNT is cleared. Set `TX_START` right before starting the ETM trigger timer, so any leaked GDMA data sits in the PARLIO FIFO without driving GPIOs.

### GDMA with ETM_EN does not auto-follow linked list descriptors

**Symptom:** With `ETM_EN` set, GDMA processes only the first descriptor in a linked list chain. After EOF, the `TASK_GDMA_START` ETM task does not cause the second descriptor to transmit.

**Root cause:** Each ETM `TASK_GDMA_START` processes one descriptor then stops. The GDMA internal pointer advances but re-triggering doesn't work reliably.

**Workaround:** For descriptor chaining, use normal GDMA mode (without `ETM_EN`). Set `LINK_START` to begin the chain, and GDMA will auto-follow the linked list. Use ETM only for PCNT threshold detection and LEDC latching, not for GDMA flow control.

### PARLIO driver claims a GDMA OUT channel silently

**Symptom:** After `parlio_new_tx_unit()`, one of the 3 GDMA OUT channels (typically CH0) is owned by the PARLIO driver. Writing to its registers (CONF0, PERI_SEL, OUTLINK) corrupts the driver's DMA state, causing `parlio_tx_unit_transmit()` to hang indefinitely.

**Detection:** After PARLIO init, scan all 3 channels:
```c
for (int ch = 0; ch < 3; ch++) {
    uint32_t peri = REG32(GDMA_CH_OUT_BASE(ch) + 0x30) & 0x3F;
    if (peri == 9) {  // 9 = PARLIO peripheral
        // This channel belongs to PARLIO — don't touch it
    }
}
```

**Workaround:** Only configure bare-metal GDMA on the channels PARLIO doesn't own. For the autonomous loop, release the PARLIO driver first or reconfigure its channel in-place after Tests 1-3.

### Bare-metal GDMA setup must be deferred

**Symptom:** Configuring bare-metal GDMA channels (even without OUTLINK_START) during init causes PARLIO transmit to hang.

**Root cause:** Writing GDMA_OUT_CONF0 with reset bit, then EOF_MODE + ETM_EN, on any channel may affect shared GDMA controller state.

**Workaround:** Defer all bare-metal GDMA configuration until after PARLIO driver tests complete. Do not configure bare-metal GDMA channels during peripheral init.

### GDMA M2M cannot write to peripheral address space

**Symptom:** GDMA memory-to-memory transfers targeting peripheral registers (RMT RAM, GPIO registers) silently fail. 800/800 tests returned zero bytes written.

**Scope:** GDMA M2M is SRAM-to-SRAM only on ESP32-C6.

**Workaround:** Use peripheral-mode GDMA (with PERI_SEL set to the target peripheral) instead of M2M mode.

### GDMA M2M requires a full channel pair

**Symptom:** GDMA M2M needs both an IN and OUT channel, consuming an entire pair. Cannot split one channel for PARLIO and the other for M2M.

## LEDC

### OVF_CNT_CH0 interrupt is at bit 12, not bit 10

**Symptom:** After enabling LEDC overflow counter, checking `INT_RAW` bit 10 always reads 0 even when overflow occurred.

**Root cause:** The INT_RAW register layout for ESP32-C6 is:
- Bits 0-3: TIMER0-3 overflow
- Bits 4-9: DUTY_CHNG_END for CH0-5
- Bits 10-11: (unused/reserved)
- **Bit 12: OVF_CNT_CH0**
- Bit 13: OVF_CNT_CH1
- Bit 14: OVF_CNT_CH2
- Bit 15-17: OVF_CNT_CH3-5

### OVF_CNT configuration requires PARA_UP

**Symptom:** Setting OVF_CNT_EN (bit 15) and OVF_NUM (bits 14:5) in CHn_CONF0 has no effect.

**Root cause:** LEDC channel parameter changes require the PARA_UP write-trigger (bit 4 of CONF0) to latch into the hardware.

**Correct sequence:**
```c
volatile uint32_t *conf0 = (volatile uint32_t*)(0x60007000 + ch * 0x14);
uint32_t val = *conf0;
val &= ~(0x3FF << 5);      // clear OVF_NUM
val |= (9 << 5);           // OVF_NUM = 9 (fires after 10 overflows)
val |= (1 << 15);          // OVF_CNT_EN
*conf0 = val;
*conf0 = val | (1 << 4);   // PARA_UP — latch changes
esp_rom_delay_us(10);
*conf0 |= (1 << 16);       // OVF_CNT_RESET
```

### OVF_NUM is in CONF0, not CONF1

**Symptom:** Writing overflow count to CHn_CONF1 (HPOINT register) corrupts the duty cycle hpoint.

**Correction:** `OVF_NUM` is at bits [14:5] of `CHn_CONF0_REG`, not in CONF1. CONF1 is `CHn_HPOINT_REG`.

### GPIO 10-12 cause LEDC channel hang

**Symptom:** `ledc_channel_config()` hangs when `gpio_num` is set to 10, 11, or 12.

**Root cause:** These GPIOs conflict with SPI flash pins on the ESP32-C6FH4 QFN32 module (which has internal flash).

**Workaround:** Use GPIO 0, 1, 2 for LEDC channels.

## PARLIO

### PARLIO loopback requires io_loop_back flag

For PARLIO output to appear on the same GPIOs that PCNT reads (internal loopback without external wiring), the config must include:
```c
.flags = { .io_loop_back = 1 }
```

### PARLIO bare-metal takeover for autonomous loop

After the IDF PARLIO driver configures the hardware, its GDMA channel (CH0) can be reconfigured bare-metal for ETM-triggered operation. Read the existing TX_CFG0, modify BYTELEN and set TX_START:
```c
uint32_t tx_cfg0 = REG32(0x60015008);  // PARLIO_TX_CFG0
tx_cfg0 &= ~(0xFFFF << 2);             // clear BYTELEN
tx_cfg0 |= (64 << 2);                  // BYTELEN = 64
tx_cfg0 |= (1 << 19);                  // TX_START
tx_cfg0 |= (1 << 18);                  // TX_GATING_EN
REG32(0x60015008) = tx_cfg0;
```

## USB Serial/JTAG

### USB disconnects on reset

**Symptom:** Pressing the hardware RESET button causes the USB Serial/JTAG interface to disconnect and re-enumerate. `idf.py monitor` and simple `serial.Serial` reads lose the connection.

**Workaround:** Use exception-based detection in Python:
```python
try:
    while True:
        data = serial.read(4096)
except serial.SerialException:
    # Reset happened — wait for reconnect
    time.sleep(0.3)
    serial.Serial(new_port, 115200)
```

### DTR/RTS ioctls throw BrokenPipeError

**Symptom:** Software reset via DTR/RTS fails with `BrokenPipeError`.

**Workaround:** Always use hardware reset button. Flash with `--before=no_reset --after=no_reset`.

## PCNT

### PARLIO nibble boundaries produce PCNT glitch counts

**Symptom:** PCNT registers ~6-17 stray counts even for patterns that should produce 0 counts (e.g., AND(1,0) or XOR(1,1)).

**Root cause:** When PARLIO shifts between 4-bit nibbles within a byte, GPIO transitions may produce brief glitch states. For packed byte `0x23` (lower=0x3, upper=0x2), the transition from nibble 0x3 to 0x2 causes GPIO_A to go 1→0 while GPIO_B momentarily appears LOW, creating a spurious XOR edge.

**Workaround:** Set PCNT watch threshold above the noise floor (25 works well) but below the minimum real count (32 for XOR, 63 for AND). The threshold of 8 used initially was too low.

### high_limit causes automatic counter reset

**Symptom:** If pulse count reaches `high_limit`, the counter auto-resets to 0.

**Impact:** If test patterns send exactly `high_limit` pulses, the counter reads 0 (looks like no pulses were received).

**Workaround:** Set `high_limit` well above the maximum expected count for the test scenario.

### Triple PCNT clear required after GDMA arm

**Symptom:** After `GDMA_LINK_START`, some data leaks through PARLIO FIFO to GPIOs before `TX_START` is set, creating stray PCNT counts. A single `pcnt_unit_clear_count()` may not catch all of them due to timing.

**Workaround:** Three consecutive PCNT clears with delays:
```c
clear_all_pcnt();
esp_rom_delay_us(100);
clear_all_pcnt();
esp_rom_delay_us(100);
clear_all_pcnt();
```

**Discovered:** Milestone 5.

## RMT

### ESP32-C6 RMT has no DMA support

**Symptom:** Setting `.flags.with_dma = 1` in `rmt_tx_channel_config_t` returns `ESP_ERR_NOT_SUPPORTED`.

**Root cause:** `soc_caps.h` for ESP32-C6 does not define `SOC_RMT_SUPPORT_DMA`. The driver guards all DMA code behind `#if SOC_RMT_SUPPORT_DMA`. Unlike ESP32-S3, the C6's RMT uses ping-pong mode with 48-word internal memory, refilled by CPU ISR.

**Impact:** RMT cannot be used for DMA-driven waveform generation. This prevents the originally planned dual-DMA architecture (PARLIO for X, RMT for Y) in the Geometry Intersection Engine.

**Workaround:** Use PARLIO for all DMA-driven waveform generation. For Y geometry, use CPU-driven static GPIO levels and pre-multiply W×X on CPU before encoding into PARLIO buffers.

**Reference:** `soc/esp32c6/include/soc/soc_caps.h` — no `SOC_RMT_SUPPORT_DMA` defined.

**Discovered:** Milestone 6 design phase.

## PARLIO (Free-Running Engine, March 2026)

### PARLIO TX core state machine corrupts when tx_start is cleared mid-transaction

**Symptom:** After `stop_freerun()` clears `tx_start` while PARLIO is actively clocking data, the
next call to `start_freerun()` causes GDMA to stall permanently. GDMA advances through exactly N
descriptors (in our case 37 of 69), accumulating the same number of EOF interrupts, then freezes.
`out_st=2` (waiting for peripheral), `outfifo_full_l1=1`. T1D diagnostic polling confirms the
`isr_eof_count` never advances past N even 400ms later. `loop_count` stays 0 indefinitely.

**Root cause:** Stopping a PARLIO TX transaction by clearing `tx_start` mid-run leaves the PARLIO
TX **control state machine** in an unknown partial state. The FIFO data reset (`tx_cfg0 bit 30 =
tx_fifo_srst`) clears the FIFO data path, but it does not reset the TX control state machine.
The control state machine still holds internal state from the incomplete transaction. When
`tx_start` is set again on the next run, the PARLIO hardware does not properly initiate a new
TX transaction — it accepts data from GDMA during the GDMA pre-fill phase (before `tx_start` is
set), but stops accepting data immediately after `tx_start` is asserted, because the control
state machine's "start" transition expects a specific precondition that is no longer met.

**Why the first run works:** `prime_pipeline()` (called once at startup) runs a complete TX
transaction to natural completion — the byte counter exhausts, `tx_eof` fires, and PARLIO's TX
control state machine returns to a clean idle state. The first `start_freerun()` inherits this
clean state and operates correctly. All subsequent calls inherit a corrupted state.

**The exact stall mechanism:** With `out_eof_mode=0` (EOF fires on SRAM fetch, not PARLIO
consumption), GDMA begins filling PARLIO's AHB FIFO as soon as `GDMA_LINK_START_BIT` is set.
PARLIO accepts these bytes into its FIFO regardless of `tx_start` state. The FIFO fills, GDMA's
L1 FIFO backs up, GDMA stalls. ~23 EOFs fire during this pre-fill phase. When `tx_start` and the
PCR clock are enabled (step 7), PARLIO briefly begins accepting more data — GDMA resumes,
~14 more EOFs fire (total 37). But then PARLIO's corrupted state machine stops accepting data.
GDMA's L1 FIFO refills completely (`outfifo_full_l1=1`), `out_st=2` locks permanently.

**Fix:** Pulse `parl_tx_rst_en` (bit 19 of `PCR_PARL_CLK_TX_CONF`, address `0x600960ac`) in
`stop_freerun()` after clearing `tx_start` and resetting the FIFO. This is the hardware-level
TX core reset — equivalent to the ESP-IDF LL function `parlio_ll_tx_reset_core()`. Set bit 19
to assert reset, then clear bit 19 to release. The core reset properly resets the TX control
state machine to its idle state, allowing the next `tx_start` to initiate cleanly.

```c
/* In stop_freerun(), after clearing tx_start and resetting FIFO: */
REG32(PCR_PARL_CLK_TX_CONF) |= (1 << 19);   /* assert parl_tx_rst_en */
esp_rom_delay_us(5);
REG32(PCR_PARL_CLK_TX_CONF) &= ~(1 << 19);  /* release reset */
esp_rom_delay_us(5);
```

**Critical distinction from FIFO reset:**

| Reset type | Register | What it resets | Fixes stale state? |
|------------|----------|----------------|--------------------|
| FIFO reset | `TX_CFG0` bit 30 (`tx_fifo_srst`) | Data FIFO contents | No — data path only |
| Core reset | `PCR_PARL_CLK_TX_CONF` bit 19 (`parl_tx_rst_en`) | TX control state machine | Yes |

**PCR_PARL_CLK_TX_CONF register layout (0x600960ac):**

| Bits | Field | Default | Description |
|------|-------|---------|-------------|
| [15:0] | `parl_clk_tx_div_num` | 0 | Clock divider |
| [17:16] | `parl_clk_tx_sel` | 0 | Clock source select |
| [18] | `parl_clk_tx_en` | 1 | TX clock enable |
| [19] | `parl_tx_rst_en` | 0 | TX core reset (1=reset asserted, 0=normal) |

**Discovered:** March 22, 2026. GIE Phase 4 free-running engine (Milestone 36+).

---

### GDMA out_eof_mode=0 fires EOFs during pre-fill even when PARLIO is not clocked

**Context:** With `out_eof_mode=0` (the only mode that works for bare-metal PARLIO on ESP32-C6;
mode 1 requires PARLIO's DMA pop handshake which is not asserted in bare-metal operation), GDMA
fires `out_eof_int_raw` when descriptor data is **fetched from SRAM**, not when PARLIO actually
clocks it out. This has a counterintuitive consequence during startup:

After `GDMA_LINK_START_BIT` is set but before `tx_start` is asserted, GDMA begins fetching
descriptor data from SRAM and pushing it into PARLIO's AHB FIFO. EOF interrupts fire for each
descriptor fetched. In the GIE's 500μs pre-arm window (between GDMA start and PARLIO start),
approximately 23 EOF events fire — corresponding to ~3,312 bytes fetched from SRAM. These
pre-fill EOFs count toward `isr_count` and must be accounted for in the loop boundary logic.

The GIE's circular chain uses `NUM_DUMMIES=5` dummy neurons at the start of each loop precisely
to absorb this pre-fill offset. The dummy separators' PCNT values are discarded during decode.

**Impact on diagnostics:** Any `isr_eof_count` diagnostic will show ~23 "phantom" EOFs before
the engine actually begins producing valid dot products. Do not mistake these for loop boundaries.

**Discovered:** March 2026. GIE Phase 4 debugging.

---

### PARLIO INT_RAW bits are sticky across tx_start cycles

**Symptom:** After TEST 1 completes (many successful loop iterations), `PARL_IO_INT_RAW_REG`
(0x60015018) retains bits set from the last completed TX transaction: `int_raw=0x5` (bit 0:
`tx_fifo_rempty`, bit 2: `tx_eof`). These bits persist into subsequent `start_freerun()` calls
because neither `stop_freerun()` nor `start_freerun()` write to `PARL_IO_INT_CLR_REG`
(0x60015020).

**Impact:** Stale `int_raw` bits can mislead register-dump diagnostics into thinking a TX
completed prematurely. In practice, `int_raw` does not feed back into PARLIO's operational state
machine — it only gates the CPU interrupt via `INT_ENA`. With `PARLIO_INT_ENA = 0` (cleared in
`start_freerun()` step 7), the stale bits cause no behavioral problem but DO confuse diagnostics.

**Clarification:** In our system, `PARLIO_INT_ENA` is set to 0 at `start_freerun()` step 7 to
prevent the PARLIO driver ISR from calling `parlio_ll_tx_enable_clock(false)` after each TX
completion (which would kill the free-running PCR clock). Because `INT_ENA=0`, `INT_ST` is always
0 regardless of `INT_RAW`. No ISR fires. The stale `INT_RAW` is purely diagnostic noise.

**Workaround:** To get clean diagnostics, write `REG32(0x60015020) = 0x7` (clear bits 0-2) at
the start of `start_freerun()` before reading any PARLIO status registers.

**Discovered:** March 22, 2026.

---

## PARLIO (Milestones 4-6)

### PARLIO 2-bit mode eliminates nibble-boundary glitches

**Context:** In 4-bit PARLIO mode (Milestones 1-3), transitions between nibbles within a byte create brief glitch states on GPIOs that PCNT registers as spurious edges (~6-17 counts). This required threshold workarounds.

**Resolution:** PARLIO 2-bit mode (Milestones 4-6) drives only GPIO 4 and GPIO 5. Each dibit maps directly to one (X_pos, X_neg) state. No cross-nibble transitions occur. The nibble-boundary glitch problem is completely eliminated.

**Configuration:**
```c
parlio_tx_unit_config_t cfg = {
    .data_width = 2,
    .output_clk_freq_hz = 1000000,
    .flags = { .io_loop_back = 1 },
};
cfg.data_gpio_nums[0] = 4;  // X_pos
cfg.data_gpio_nums[1] = 5;  // X_neg
```

### PARLIO FIFO reset sequence

**Requirement:** Before each dot product evaluation, the PARLIO FIFO must be reset to prevent stale data from a previous transfer from reaching the GPIOs.

**Correct sequence:**
```c
uint32_t tx_cfg0 = REG32(0x60015008);  // PARLIO_TX_CFG0
tx_cfg0 |= (1 << 30);                   // FIFO_RST = 1
REG32(0x60015008) = tx_cfg0;
esp_rom_delay_us(10);
tx_cfg0 &= ~(1 << 30);                  // FIFO_RST = 0
REG32(0x60015008) = tx_cfg0;
esp_rom_delay_us(10);
```

**Discovered:** Milestone 5.

## GDMA (Milestones 5-6)

### GDMA descriptor chains work correctly without ETM_EN

**Context:** With `ETM_EN` set, GDMA processes only one descriptor per `TASK_GDMA_START` trigger (Milestone 3 errata). Without `ETM_EN`, GDMA auto-follows the linked list `next` pointer, processing all chained descriptors in sequence.

**Architecture implication:** For multi-buffer dot products (Milestones 5-6), use normal GDMA mode. Gate the output with PARLIO `TX_START` instead of ETM-triggered GDMA start:

1. Set GDMA `LINK_START` (DMA begins immediately, fills PARLIO FIFO)
2. Clear PCNT (remove any leaked counts)
3. Set PARLIO `TX_START` (data flows from FIFO to GPIOs)
4. Wait for DMA EOF
5. Read PCNT

This gives the same result as ETM-triggered DMA but with full descriptor chain support.

**Discovered:** Milestone 5.

## GDMA (Free-Running Engine, March 2026)

### GDMA circular chain must use out_eof_mode=0; mode=1 is unusable with bare-metal PARLIO

**Symptom:** With `out_eof_mode=1` (`GDMA_OUT_CONF0` bit 3), GDMA's `out_eof_int_raw` never
fires. The GIE's ISR never triggers. Loop count stays 0.

**Root cause:** `out_eof_mode=1` requires the peripheral to assert its "DMA FIFO popped" handshake
signal before GDMA fires the EOF interrupt. In ESP-IDF's bare-metal PARLIO configuration (no
driver managing the transaction handshake), PARLIO never asserts this signal. The interrupt never
fires.

**Workaround:** Use `out_eof_mode=0` (the default, `GDMA_OUT_CONF0` bit 3 = 0). With mode 0,
GDMA fires `out_eof_int_raw` as soon as descriptor data is fetched from SRAM into GDMA's internal
FIFO. No PARLIO handshake required. The trade-off: EOFs fire during the pre-fill phase before
PARLIO clocks the data, generating ~23 phantom EOFs per `setup_gdma_freerun()` call (see PARLIO
section above).

**Discovered:** Phase 4 free-running engine.

---

### GDMA circular chain owner bits are NOT cleared by GDMA on ESP32-C6

**Symptom:** If GDMA owner bit behavior matched the TRM description (GDMA sets `owner=0` after
processing a descriptor, making it CPU-owned), a circular chain would break after one full pass —
GDMA would encounter `owner=0` on the second wrap and halt.

**Observed:** The GIE runs at 432 Hz continuously across multiple seconds without the ISR resetting
owner bits. The circular chain survives indefinitely. Owner bits remain `1` (GDMA-owned) after
processing.

**Conclusion (empirical):** On ESP32-C6 rev v0.2, GDMA TX does NOT clear owner bits after
processing a descriptor. The circular chain works without any owner-bit maintenance in the ISR.
This is consistent with observed behavior and with the free-running design. Do not add owner-bit
reset logic to the ISR — it is not needed and could break the chain.

**Discovered:** Phase 4 free-running engine validation.

---

### GDMA reset must be issued in stop_freerun() to prevent ISR race on second start_freerun()

**Symptom:** Without an explicit GDMA reset in `stop_freerun()`, calling `start_freerun()` a
second time hangs the ESP32-C6 permanently. The firmware must be power-cycled. The hang is caused
by a race between the ISR and `setup_gdma_freerun()`'s GDMA reset.

**Root cause sequence:**
1. `stop_freerun()` resets PARLIO FIFO (bit 30 of TX_CFG0). This unblocks a stalled GDMA.
2. The circular GDMA chain resumes running (FIFO is now empty, GDMA can push data again).
3. `start_freerun()` enables GDMA interrupts before or during `setup_gdma_freerun()`.
4. The running GDMA chain fires EOF interrupts. The ISR calls `pcnt_unit_clear_count()`.
5. `setup_gdma_freerun()` simultaneously resets GDMA (`GDMA_OUT_CONF0 = GDMA_RST_BIT`).
6. `pcnt_unit_clear_count()` internally acquires a mutex. The GDMA reset may corrupt DMA state.
7. Deadlock or memory corruption. Hang.

**Fix:**
1. In `stop_freerun()`: after resetting PARLIO FIFO, wait 20μs (let in-flight AHB complete), then
   assert `GDMA_RST_BIT` on `GDMA_OUT_CONF0`, wait 10μs, clear it. GDMA is now stopped.
2. In `start_freerun()`: enable GDMA interrupts AFTER `setup_gdma_freerun()` completes, not before.

This ensures GDMA is always idle when `setup_gdma_freerun()` runs and when interrupts are enabled.

**Discovered:** Phase 4 free-running engine, second-call hang investigation.

---

## FreeRTOS / Memory (Milestone 7)

### Large structs on FreeRTOS task stack cause Load access fault

**Symptom:** `Guru Meditation Error: Core 0 panic'ed (Load access fault)` immediately on entering `app_main`. Stack dump shows all zeros.

**Root cause:** The `ternary_cfc_t` struct (~16,450 bytes: `2 × 32 × 256` weight arrays + biases + hidden state) was allocated on the `app_main` task stack. The default ESP-IDF main task stack is 3,584 bytes. Stack overflow corrupts memory and triggers a load access fault.

**Workaround:** Declare large structs as `static` so they go in BSS (SRAM) instead of the task stack. This costs nothing — BSS is zero-initialized at boot and persists for the lifetime of the program.

```c
// BAD: 16KB on the 3.5KB task stack → crash
ternary_cfc_t cfc;

// GOOD: 16KB in BSS (SRAM) → zero stack cost
static ternary_cfc_t cfc;
```

**Alternative:** Increase main task stack via `CONFIG_ESP_MAIN_TASK_STACK_SIZE` in sdkconfig. But `static` is simpler and doesn't waste stack space.

**Discovered:** Milestone 7.
