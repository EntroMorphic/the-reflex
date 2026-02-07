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

## GDMA

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

### high_limit causes automatic counter reset

**Symptom:** If pulse count reaches `high_limit`, the counter auto-resets to 0.

**Impact:** If test patterns send exactly `high_limit` pulses, the counter reads 0 (looks like no pulses were received).

**Workaround:** Set `high_limit` well above the maximum expected count for the test scenario.
