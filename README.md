# Stigmergy: Cache Coherency as a Coordination Primitive

**Cache coherency traffic—the mechanism by which multi-core processors maintain memory consistency—can be repurposed as a coordination primitive without syscalls, locks, or explicit messages.**

---

<p align="center">
  <strong>30x faster than futex. Works on a $35 Raspberry Pi.</strong>
</p>

---

## Quick Results

| Platform | Stigmergy | Futex | Speedup |
|----------|-----------|-------|---------|
| Jetson Thor (14-core ARM) | 297 ns | 9,083 ns | **30x** |
| Raspberry Pi 4 (4-core ARM) | 167 ns | 778 ns | **5x** |

## What is Stigmergy?

Stigmergy is indirect coordination through environmental traces—like ants leaving pheromone trails. In computing, we use **cache coherency traffic** as the trace:

1. **Producer** writes to a cache line
2. **Cache coherency protocol** (CHI, MESI) invalidates remote copies
3. **Consumer** detects the change via access latency or sequence number
4. **No syscalls, no locks, no explicit messages**

The hardware is already doing this to maintain memory consistency. We're just... noticing.

---

## Run It Yourself

### Option 1: Raspberry Pi (Recommended for verification)

```bash
# On any Raspberry Pi 3/4/5
git clone https://github.com/EntroMorphic/stigmergy-demo.git
cd stigmergy-demo/src
gcc -O3 -Wall -pthread e3_latency_comparison.c -o e3_latency -lm
./e3_latency
```

### Option 2: Any Linux (ARM or x86_64)

```bash
git clone https://github.com/EntroMorphic/stigmergy-demo.git
cd stigmergy-demo/src
gcc -O3 -Wall -pthread e3_latency_comparison.c -o e3_latency -lm
./e3_latency
```

### Option 3: Google Colab (One-Click)

[![Open In Colab](https://colab.research.google.com/assets/colab-badge.svg)](https://colab.research.google.com/github/EntroMorphic/stigmergy-demo/blob/main/notebooks/stigmergy_demo.ipynb)

---

## The Experiments

| Experiment | What it proves | Build & Run |
|------------|---------------|-------------|
| **E3: Latency** | Stigmergy beats futex by 5-30x | `gcc -O3 -pthread e3_latency_comparison.c -o e3 -lm && ./e3` |
| **E1: Causality** | 100% detection, signal→detect→respond | `gcc -O3 -pthread e1_coordination_v3.c -o e1 -lm && ./e1` |
| **E2b: False Sharing** | 0% false positives, 100% precision | `gcc -O3 -pthread e2b_false_sharing.c -o e2b -lm && ./e2b` |

### E3: Latency Comparison

Compares coordination mechanisms:

```
+------------+----------+----------+
| Mechanism  |  Median  | Speedup  |
+------------+----------+----------+
| Stigmergy  |   167 ns |   1.0x   |
| Atomic     |   148 ns |   0.9x   |
| Futex      |   778 ns |   4.7x   |  <- syscall overhead
| Pipe       | 16000 ns |  96.0x   |  <- kernel buffers
+------------+----------+----------+
```

**Key insight:** Cache-based coordination (stigmergy, atomic) is 5-100x faster than syscall-based (futex, pipe).

### E1: Causality Proof

Proves the coordination loop:

```
Signal -> Propagation -> Detection -> Behavior Change
  |           |             |              |
  ts=0      ~50ns        ts=50          vigilance++
```

- **100% detection rate** (100/100 signals)
- **100% causality preserved** (detect_ts > signal_ts always)
- **Behavior changes** (vigilance 50 → 100)

---

## Key Findings

### 1. It's Fast
Stigmergy: **167-297 ns** vs Futex: **778-9083 ns**

### 2. It's Reliable
**100% detection rate** under realistic load (E2 experiment)

### 3. It's Free
**Zero measurable power overhead** (E5 experiment)

### 4. Load Helps (Counterintuitive!)
| Load | Detection | Latency |
|------|-----------|---------|
| 0% (idle) | 78% | 200 μs |
| 50% | **100%** | **1.5 μs** |
| 80% | **100%** | 18 μs |

Busy systems have more predictable timing than idle systems.

---

## Repository Structure

```
stigmergy-demo/
├── src/
│   ├── e3_latency_comparison.c  # Latency comparison (E3)
│   ├── e1_coordination_v3.c     # Causality proof (E1)
│   └── e2b_false_sharing.c      # False sharing control (E2b)
├── notebooks/
│   └── stigmergy_demo.ipynb     # Colab notebook
├── results/
│   ├── thor_results.md          # Jetson Thor (14-core ARM)
│   └── pi4_results.md           # Raspberry Pi 4 (4-core ARM)
└── README.md
```

---

## How It Works

### The Mechanism

```c
// Producer (Core 0)
stigmergy.value = timestamp;
stigmergy.sequence++;
memory_barrier();

// Consumer (Core 1) - polling
while (stigmergy.sequence == last_seq) {
    // Cache coherency delivers the invalidation
}
// Detected! sequence changed.
```

### Why It's Fast

| Mechanism | What happens |
|-----------|--------------|
| **Stigmergy** | Cache write → snoop → invalidate → reload |
| **Futex** | User → kernel → scheduler → wake → user |
| **Pipe** | User → kernel → buffer → kernel → user |

Stigmergy stays in hardware. Syscalls go through the kernel.

### The Cache Coherency Protocol

```
Core 0 writes → DSU/LLC invalidates Core 1's copy
             → Core 1's next read fetches new value
             → ~50-300 ns total
```

This is what cache coherency protocols (CHI, MESI, MOESI) do anyway. We're using the existing infrastructure.

---

## Platforms Tested

| Platform | Cores | Architecture | E3 Latency | E1 Causality | E2b FP |
|----------|-------|--------------|------------|--------------|--------|
| NVIDIA Jetson AGX Thor | 14 | ARM Cortex-A78AE | ✅ | ✅ | ✅ |
| Raspberry Pi 4 Model B | 4 | ARM Cortex-A72 | ✅ | ✅ | ✅ |
| x86_64 Linux | varies | Intel/AMD | ✅ | ⚠️ | ⚠️ |
| Google Colab | 2 | Intel Xeon | ✅ | ⚠️ | ⚠️ |

**Note:** E1 and E2b use ARM-specific barriers (`dsb sy`). E3 works on all platforms.

---

## FAQ

**Q: Isn't this just false sharing?**
A: False sharing is *accidental* coordination that hurts performance. Stigmergy is *intentional* coordination that provides value. E2b proves we can distinguish them with 100% precision.

**Q: What about memory ordering?**
A: We use explicit memory barriers (`dsb sy` on ARM, `mfence` on x86) after writes. The consumer polls, so it always sees the latest value.

**Q: Is this a covert channel?**
A: Yes, cache timing can be a covert channel. This is the *constructive* use of that mechanism. Security implications are out of scope.

**Q: Why not just use atomics?**
A: You can! Atomics are similar speed. Stigmergy provides richer semantics (timestamps, sequences, patterns) without the constraints of atomic operations.

---

## Citation

```bibtex
@misc{stigmergy2026,
  title={Cache Coherency as a Coordination Primitive},
  author={EntroMorphic Research},
  year={2026},
  url={https://github.com/EntroMorphic/stigmergy-demo}
}
```

---

## Related Work

- **Delta Observer**: [github.com/EntroMorphic/delta-observer](https://github.com/EntroMorphic/delta-observer) - Neural network interpretability using similar observation principles

---

## License

MIT License

---

**The hardware is already doing the work. We're just listening.**
