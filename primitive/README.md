# primitive — The Reflex Coordination Library

`include/reflex.h` is a single header-only C library for sub-microsecond inter-core coordination via cache coherency signaling.

No dependencies beyond `pthreads`. Works on ARM64 and x86_64.

---

## Core Concept

When one CPU core writes to a cache-aligned memory location, the MESI/MOESI coherency protocol forces all other cores to invalidate their cached copy. A spinning consumer detects this invalidation as a sequence counter change — no syscall, no kernel, no buffering. The write *is* the signal.

This is stigmergy: environment-mediated coordination realized in hardware.

---

## Usage

```c
#include "reflex.h"

// Initialize
reflex_channel_t ch;
reflex_init(&ch);

// Producer (Core 0) — pin with reflex_pin_to_core(0)
uint64_t ts = reflex_rdtsc();
reflex_signal(&ch, ts);

// Consumer (Core 1) — pin with reflex_pin_to_core(1)
uint64_t last_seq = 0;
last_seq = reflex_wait(&ch, last_seq);          // blocks until signal
uint64_t signal_ts = reflex_get_timestamp(&ch); // when producer fired
```

### With payload

```c
reflex_signal_value(&ch, reflex_rdtsc(), sensor_reading);
uint64_t value = reflex_get_value(&ch);
```

### Non-blocking poll

```c
if (reflex_poll(&ch, last_seq)) {
    last_seq = ch.sequence;
    // handle signal
}
```

### Timeout

```c
uint64_t freq = reflex_get_freq();
uint64_t timeout_cycles = freq / 1000;  // 1ms
uint64_t seq = reflex_wait_timeout(&ch, last_seq, timeout_cycles);
if (seq == 0) { /* timed out */ }
```

---

## Build

Copy `include/reflex.h` into your project and add it to your include path.

```makefile
CFLAGS += -I path/to/primitive/include
```

Requires `-pthread` at link time.

---

## Measured Latency (Round-Trip)

| Platform              | Median  | P99     |
|-----------------------|---------|---------|
| Jetson AGX Thor       | 297 ns  | 370 ns  |
| Raspberry Pi 4        | 167 ns  | 167 ns  |

vs. futex on Thor: **30x faster**.

See `../experiments/` for full benchmarks and `../robotics/` for the 10kHz control loop demo.

---

## API Reference

| Function | Description |
|----------|-------------|
| `reflex_init(ch)` | Zero-initialize a channel |
| `reflex_signal(ch, ts)` | Producer: write timestamp, increment sequence, barrier |
| `reflex_signal_value(ch, ts, val)` | Producer: signal with payload |
| `reflex_wait(ch, last_seq)` | Consumer: spin until sequence changes, return new seq |
| `reflex_wait_timeout(ch, last_seq, cycles)` | Consumer: spin with timeout, return 0 on timeout |
| `reflex_poll(ch, last_seq)` | Consumer: non-blocking check, return 1 if new signal |
| `reflex_get_timestamp(ch)` | Read last signal timestamp |
| `reflex_get_value(ch)` | Read last signal payload |
| `reflex_pin_to_core(id)` | Pin calling thread to a specific CPU core |
| `reflex_rdtsc()` | Read hardware cycle counter (ARM64: cntvct_el0, x86: rdtsc) |
| `reflex_get_freq()` | Counter frequency in Hz |
| `reflex_memory_barrier()` | Full system barrier (dsb sy / mfence) |
| `reflex_compiler_barrier()` | Compiler-only barrier |
