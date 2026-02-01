# PRD: Zero-Dependency Reflex OS

**Date:** February 1, 2026
**Status:** PHASE 1 COMPLETE - Direct CSR/GPIO access verified
**Goal:** Strip ESP-IDF to bare metal. The Reflex IS the operating system.

---

## THE SUMMIT ACHIEVED (Feb 1, 2026)

```
    _____ _   _ _____   ____  _   _ __  __ __  __ ___ _____    
   |_   _| | | | ____| / ___|| | | |  \/  |  \/  |_ _|_   _|   
     | | | |_| |  _|   \___ \| | | | |\/| | |\/| || |  | |     
     | | |  _  | |___   ___) | |_| | |  | | |  | || |  | |     
     |_| |_| |_|_____| |____/ \___/|_|  |_|_|  |_|___| |_|     
                                                               
           ZERO EXTERNAL DEPENDENCIES ACHIEVED                 
================================================================

  What we stripped:
    - esp_cpu.h      -> direct CSR 0x7e2
    - driver/gpio.h  -> direct GPIO registers (0x60091000)
    - stdio.h/printf -> direct USB Serial JTAG registers (0x6000F000)

  What remains:
    - <stdint.h>     (types only, no functions)
    - <stdbool.h>    (types only, no functions)
    - ESP-IDF bootloader (to be replaced in Phase 4)

  FALSIFICATION SUITE
  ------------------------------------------------
  Baseline           34 cy =  212 ns  (81-4768 ns)
  No GPIO            11 cy =   68 ns  (68-1412 ns)
  Pure decision       2 cy =   12 ns  (12-200 ns)    <-- THE MONEY
  With channel       73 cy =  456 ns  (418-3456 ns)

  ADVERSARIAL (interrupts ON, 100K samples)
    Avg: 200 ns
    Min: 68 ns
    Max: 5668 ns (0% catastrophic)

  +-----------------------------------------+
  |  PURE DECISION LATENCY: 12 NANOSECONDS  |
  +-----------------------------------------+

================================================================
```

**This binary uses ZERO libc functions.**
**This binary uses ZERO ESP-IDF HAL functions.**
**Just silicon. Just registers. Just The Reflex.**

---

## Current ESP-IDF Dependencies

### Critical Path (HOT - must eliminate)

| Dependency | Used For | Current | Bare Metal Replacement |
|------------|----------|---------|------------------------|
| `esp_cpu.h` | `esp_cpu_get_cycle_count()` | reflex.h | Direct CSR read (see below) |
| `esp_random.h` | `esp_random()` | spine_main.c | LFSR/xorshift (already have `fast_random()`) |

### Support Path (WARM - can eliminate)

| Dependency | Used For | Current | Bare Metal Replacement |
|------------|----------|---------|------------------------|
| `esp_log.h` | `ESP_LOGW()` | spine_main.c | Direct UART register writes |
| `esp_timer.h` | High-res timer | spine_main.c | Direct timer peripheral |
| FreeRTOS | `vTaskDelay()` | demos | Busy-wait loops |

### Peripheral Drivers (COLD - eliminate later)

| Dependency | Used For | Replacement |
|------------|----------|-------------|
| `driver/spi_master.h` | SPI | Direct SPI registers |
| `esp_adc/*` | ADC | Direct ADC registers |
| `driver/temperature_sensor.h` | Temp | Direct sensor registers |
| `esp_wifi.h` | WiFi | Not needed for spine demo |
| `esp_event.h` | Events | Not needed |
| `esp_netif.h` | Network | Not needed |

---

## Phase 1: Bare Metal Spine (Minimal)

**Goal:** spine_main.c with ZERO external dependencies

### What We Need

```c
// The entire dependency list for bare metal spine:
#include <stdint.h>
#include <stdbool.h>

// That's it. Everything else is direct register access.
```

### Register Map (ESP32-C6)

| Peripheral | Base Address | TRM Section |
|------------|--------------|-------------|
| GPIO | 0x60091000 | Ch. 6 |
| Timer Group 0 | 0x6001F000 | Ch. 11 |
| Timer Group 1 | 0x60020000 | Ch. 11 |
| UART0 | 0x60000000 | Ch. 26 |
| RNG | 0x600260B0 | Ch. 25 |
| System Timer | 0x6000A000 | Ch. 12 |

### Cycle Counter Fix

The `rdcycle` CSR is restricted on ESP32-C6. ESP-IDF uses `esp_cpu_get_cycle_count()` which reads the performance counter. We can do this directly:

```c
// ESP32-C6: Read MCYCLE (machine cycle counter)
// Note: Requires M-mode or appropriate PMP config
static inline uint32_t reflex_cycles(void) {
    uint32_t cycles;
    __asm__ volatile("csrr %0, mcycle" : "=r"(cycles));
    return cycles;
}
```

If `mcycle` is restricted, use the system timer:

```c
// System Timer: 16MHz (not 160MHz!) - need conversion
#define SYSTIMER_BASE       0x6000A000
#define SYSTIMER_UNIT0_VALUE_LO (SYSTIMER_BASE + 0x004)

static inline uint64_t systimer_read(void) {
    return *(volatile uint64_t*)SYSTIMER_UNIT0_VALUE_LO;
}

// Convert to ~cycles (multiply by 10 for 160MHz equivalent)
static inline uint32_t reflex_cycles(void) {
    return (uint32_t)(systimer_read() * 10);
}
```

### UART Output (Replaces ESP_LOG)

```c
#define UART0_BASE          0x60000000
#define UART_FIFO_REG       (UART0_BASE + 0x00)
#define UART_STATUS_REG     (UART0_BASE + 0x1C)
#define UART_TX_DONE_BIT    (1 << 14)

static inline void uart_putc(char c) {
    while (!(*(volatile uint32_t*)UART_STATUS_REG & UART_TX_DONE_BIT));
    *(volatile uint32_t*)UART_FIFO_REG = c;
}

static inline void uart_puts(const char* s) {
    while (*s) uart_putc(*s++);
}

static inline void uart_puthex(uint32_t val) {
    const char hex[] = "0123456789abcdef";
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0xF]);
    }
}
```

### Hardware RNG (Replaces esp_random)

```c
#define RNG_BASE            0x600260B0
#define RNG_DATA_REG        (RNG_BASE + 0x00)

static inline uint32_t hw_random(void) {
    return *(volatile uint32_t*)RNG_DATA_REG;
}
```

But for deterministic benchmarks, keep using xorshift:

```c
static uint32_t prng_state = 0x12345678;
static inline uint32_t fast_random(void) {
    prng_state ^= prng_state << 13;
    prng_state ^= prng_state >> 17;
    prng_state ^= prng_state << 5;
    return prng_state;
}
```

### Delay (Replaces vTaskDelay)

```c
static inline void delay_cycles(uint32_t cycles) {
    uint32_t start = reflex_cycles();
    while ((reflex_cycles() - start) < cycles) {
        __asm__ volatile("nop");
    }
}

static inline void delay_us(uint32_t us) {
    delay_cycles(us * 160);  // 160 cycles per μs at 160MHz
}

static inline void delay_ms(uint32_t ms) {
    delay_cycles(ms * 160000);
}
```

---

## Phase 2: Bare Metal Boot

**Goal:** Boot without ESP-IDF second-stage bootloader

### What ESP-IDF Bootloader Does

1. Initializes clocks (PLL, CPU freq)
2. Configures flash cache
3. Sets up memory regions
4. Loads app from flash
5. Jumps to `app_main()`

### Minimal Startup

```c
// startup.c - Bare metal entry point

// Linker-provided symbols
extern uint32_t _sbss, _ebss;
extern uint32_t _sdata, _edata, _sidata;
extern void app_main(void);

// Reset handler - first code to run
void __attribute__((naked, section(".text.reset"))) reset_handler(void) {
    // Set stack pointer
    __asm__ volatile("la sp, _stack_top");
    
    // Zero BSS
    for (uint32_t* p = &_sbss; p < &_ebss; p++) *p = 0;
    
    // Copy data from flash to RAM
    uint32_t* src = &_sidata;
    for (uint32_t* dst = &_sdata; dst < &_edata; ) *dst++ = *src++;
    
    // Initialize clocks (minimal)
    init_clocks();
    
    // Jump to main
    app_main();
    
    // Hang if main returns
    while(1);
}

void init_clocks(void) {
    // TODO: Configure PLL for 160MHz
    // ESP32-C6 boots at 40MHz XTAL, need to enable PLL
}
```

### Linker Script (Minimal)

```ld
/* reflex.ld - Bare metal linker script */
MEMORY {
    iram (rwx) : ORIGIN = 0x40800000, LENGTH = 0x60000  /* 384KB IRAM */
    dram (rw)  : ORIGIN = 0x3FC80000, LENGTH = 0x70000  /* 448KB DRAM */
    flash (rx) : ORIGIN = 0x42000000, LENGTH = 0x400000 /* 4MB Flash */
}

SECTIONS {
    .text : { *(.text.reset) *(.text*) } > flash
    .rodata : { *(.rodata*) } > flash
    .data : { _sdata = .; *(.data*) _edata = .; } > dram AT > flash
    _sidata = LOADADDR(.data);
    .bss : { _sbss = .; *(.bss*) *(COMMON) _ebss = .; } > dram
    _stack_top = ORIGIN(dram) + LENGTH(dram);
}
```

---

## Phase 3: What We Keep vs. Strip

### KEEP (The Reflex Core)

| File | Purpose | Dependencies |
|------|---------|--------------|
| reflex.h | Core primitive | `<stdint.h>` only |
| reflex_gpio.h | GPIO channels | `<stdint.h>` only |
| reflex_c6.h | C6 integration | reflex.h, reflex_gpio.h |
| reflex_spline.h | Interpolation | `<stdint.h>` only |
| reflex_void.h | Entropy field | `<stdint.h>`, `<string.h>` |

### STRIP (ESP-IDF Bloat)

| Component | Size | Why Strip |
|-----------|------|-----------|
| FreeRTOS | ~50KB | Not needed for bare metal |
| WiFi stack | ~200KB | Not needed for spine demo |
| Bluetooth | ~150KB | Not needed |
| ESP-IDF HAL | ~100KB | Replace with direct registers |
| Newlib | ~100KB | Replace with minimal libc |
| Bootloader | ~30KB | Replace with minimal startup |

### RESULT

| Before (ESP-IDF) | After (Bare Metal) |
|------------------|-------------------|
| ~800KB binary | ~10KB binary |
| 2+ second boot | <10ms boot |
| Unknown latency sources | Fully deterministic |

---

## Implementation Plan

### Day 1: Fix Cycle Counter
- [ ] Test direct `mcycle` CSR read
- [ ] If restricted, use system timer with conversion
- [ ] Update reflex.h to remove `esp_cpu.h` dependency

### Day 2: Bare Metal UART
- [ ] Implement `uart_putc`, `uart_puts`, `uart_puthex`
- [ ] Create `reflex_uart.h` with direct register access
- [ ] Replace all `ESP_LOGW` calls

### Day 3: Bare Metal Spine Demo
- [ ] Create `spine_bare.c` with zero dependencies
- [ ] Build with minimal startup code
- [ ] Verify benchmarks match ESP-IDF version

### Day 4: Documentation
- [ ] Document all register addresses
- [ ] Create bare metal quick start guide
- [ ] Update ARCHITECTURE.md

---

## The Vision

```
┌─────────────────────────────────────────────────────────────────┐
│                    THE REFLEX: BARE METAL                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   #include <stdint.h>                                          │
│   #include "reflex.h"         // 150 lines                     │
│   #include "reflex_gpio.h"    // 100 lines                     │
│   #include "reflex_uart.h"    // 50 lines                      │
│                                                                 │
│   void app_main(void) {                                        │
│       gpio_set_output(8);     // LED                           │
│       while (1) {                                               │
│           uint32_t t0 = reflex_cycles();                       │
│           bool anomaly = (hw_random() > 128);                  │
│           gpio_write(8, !anomaly);                             │
│           uint32_t latency = reflex_cycles() - t0;             │
│           uart_puthex(latency);                                │
│       }                                                         │
│   }                                                             │
│                                                                 │
│   Binary size: ~5KB                                            │
│   Boot time: <10ms                                             │
│   Latency: 12ns (same as ESP-IDF version)                      │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Questions

1. **Priority:** Do we need WiFi/BLE for Valentine's Day demo?
   - If NO → Strip now
   - If YES → Strip after demo

2. **Bootloader:** Keep ESP-IDF bootloader for flash encryption?
   - Production may need secure boot
   - Dev can use minimal bootloader

3. **Debug:** Keep JTAG/USB-serial debug?
   - Useful for development
   - Can be stripped for production

---

## References

- [ESP32-C6 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-c6_technical_reference_manual_en.pdf)
- [RISC-V Privileged Spec](https://riscv.org/specifications/privileged-isa/)
- [ESP-IDF Source (for register addresses)](https://github.com/espressif/esp-idf)

---

*"The Reflex doesn't need an operating system. The Reflex IS the operating system."*
