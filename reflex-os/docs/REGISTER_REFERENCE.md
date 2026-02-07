# ESP32-C6 Register Reference for Autonomous Fabric

Bare-metal register addresses and ETM IDs discovered and verified during development.

## Peripheral Base Addresses

| Peripheral | Base | Notes |
|-----------|------|-------|
| ETM | `0x600B8000` | Event Task Matrix crossbar |
| PCR | `0x60096000` | Peripheral Clock Register |
| GDMA | `0x60080000` | General DMA controller |
| LEDC | `0x60007000` | LED PWM controller |
| PARLIO | `0x60015000` | Parallel IO |
| TIMG0 | `0x60008000` | Timer Group 0 |
| TIMG1 | `0x60009000` | Timer Group 1 |

## ETM Registers

| Register | Address | Purpose |
|----------|---------|---------|
| CLK_EN | `ETM + 0x00` | Clock enable (write 1) |
| CH_ENA_AD0_SET | `ETM + 0x04` | Enable channels 0-31 (write 1 to set) |
| CH_ENA_AD0_CLR | `ETM + 0x08` | Disable channels 0-31 (write 1 to clear) |
| CH_ENA_AD1_SET | `ETM + 0x10` | Enable channels 32-49 |
| CH_ENA_AD1_CLR | `ETM + 0x14` | Disable channels 32-49 |
| CH_EVT_ID(n) | `ETM + 0x18 + n*8` | Event ID for channel n |
| CH_TASK_ID(n) | `ETM + 0x1C + n*8` | Task ID for channel n |

50 channels total. Each channel connects one event to one task.

## ETM Event IDs

| ID | Source | Name |
|----|--------|------|
| 31 | LEDC | OVF_CNT CH0 |
| 32 | LEDC | OVF_CNT CH1 |
| 33 | LEDC | OVF_CNT CH2 |
| 45 | PCNT | Threshold (watch point reached) |
| 46 | PCNT | Limit (high_limit reached) |
| 48 | Timer | TIMER0 group alarm (counter compare) |
| 49 | Timer | TIMER1 group alarm |
| 153 | GDMA | CH0 EOF (DMA transfer complete) |
| 154 | GDMA | CH1 EOF |
| 155 | GDMA | CH2 EOF |

## ETM Task IDs

| ID | Target | Name |
|----|--------|------|
| 47 | LEDC | OVF counter reset CH0 |
| 48 | LEDC | OVF counter reset CH1 |
| 49 | LEDC | OVF counter reset CH2 |
| 57 | LEDC | Timer0 resume |
| 58 | LEDC | Timer1 resume |
| 59 | LEDC | Timer2 resume |
| 61 | LEDC | Timer0 pause |
| 62 | LEDC | Timer1 pause |
| 63 | LEDC | Timer2 pause |
| 69 | Timer | Timer0 capture (latch count) |
| 87 | PCNT | Reset all counters |
| 88 | Timer | Timer0 group start |
| 89 | Timer | Timer1 group start |
| 92 | Timer | Timer0 group stop (see errata) |
| 93 | Timer | Timer1 group stop |
| 94 | Timer | Timer0 group reload |
| 95 | Timer | Timer1 group reload |
| 96 | Timer | Timer0 group capture |
| 97 | Timer | Timer1 group capture |
| 162 | GDMA | CH0 start |
| 163 | GDMA | CH1 start |
| 164 | GDMA | CH2 start |

Source: `soc/esp32c6/include/soc/soc_etm_source.h` in ESP-IDF v5.4.

## GDMA OUT Channel Registers

Base per channel: `0x60080000 + 0xD0 + ch * 0xC0`

| Offset | Register | Key bits |
|--------|----------|----------|
| 0x00 | OUT_CONF0 | bit 0: RST, bit 3: EOF_MODE, bit 6: ETM_EN |
| 0x10 | OUT_LINK | bits [19:0]: descriptor addr, bit 21: START |
| 0x30 | OUT_PERI_SEL | bits [5:0]: peripheral (9 = PARLIO) |

**ETM_EN (bit 6):** Must be set for ETM-triggered DMA starts. Not exposed by IDF GDMA API — requires bare-metal register write.

## LEDC Registers

### Channel CONF0 (per channel, stride 0x14)

Address: `0x60007000 + ch * 0x14`

| Bits | Field | Description |
|------|-------|-------------|
| [1:0] | TIMER_SEL | Which timer (0-3) |
| 2 | SIG_OUT_EN | Output enable |
| 3 | IDLE_LV | Idle level |
| 4 | PARA_UP | Write-trigger: latch all config changes |
| [14:5] | OVF_NUM | Overflow count threshold minus 1 |
| 15 | OVF_CNT_EN | Enable overflow counter |
| 16 | OVF_CNT_RESET | Write-trigger: reset overflow counter |

### INT_RAW (0x60007000 + 0xC0)

| Bits | Field |
|------|-------|
| [3:0] | TIMER0-3 overflow |
| [9:4] | DUTY_CHNG_END CH0-5 |
| 12 | OVF_CNT CH0 |
| 13 | OVF_CNT CH1 |
| 14 | OVF_CNT CH2 |
| 15 | OVF_CNT CH3 |
| 16 | OVF_CNT CH4 |
| 17 | OVF_CNT CH5 |

### INT_CLR (0x60007000 + 0xCC)

Write 1 to clear corresponding INT_RAW bit.

### ETM Enable Registers

| Register | Address | Bits |
|----------|---------|------|
| EVT_TASK_EN0 | `0x600071A0` | bits 8-10: OVF event enable for CH0-2 |
| EVT_TASK_EN1 | `0x600071A4` | bits 16-18: OVF reset task CH0-2, bits 28-30: timer pause/resume tasks |

## PARLIO TX Registers

| Register | Address | Key fields |
|----------|---------|------------|
| TX_CFG0 | `0x60015008` | [17:2] BYTELEN, [18] GATING_EN, [19] TX_START, [25] SMP_EDGE, [26] UNPACK_ORDER, [29:27] BUS_WID (010=4bit), [30] FIFO_SRST |
| TX_CFG1 | `0x6001500C` | [31:16] IDLE_VALUE |
| CLK_REG | `0x60015120` | bit 0: CLK_EN |

## PCR (Peripheral Clock) Registers

| Register | Address | Purpose |
|----------|---------|---------|
| SOC_ETM_CONF | `0x60096090` | bit 0: ETM clock enable, bit 1: ETM reset |
| GDMA_CONF | `0x600960BC` | bit 0: GDMA clock enable, bit 1: GDMA reset |

## Timer Group Registers

### TIMG0 T0CONFIG (0x60008000)

| Bit | Field |
|-----|-------|
| 31 | EN (timer enable — clear to stop) |
| 28 | ETM_EN (default 1) |

## GDMA Channel Detection

After PARLIO driver init, detect which channel it claimed:
```c
for (int ch = 0; ch < 3; ch++) {
    uint32_t peri = *(volatile uint32_t*)(0x60080000 + 0xD0 + ch*0xC0 + 0x30) & 0x3F;
    if (peri == 9) // PARLIO
        parlio_ch = ch;
}
```
