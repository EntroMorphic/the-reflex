# Phase 1 Results: SCHED_FIFO + mlockall

> Measured: 2026-01-24
> Platform: NVIDIA Jetson AGX Thor (container with --privileged)

---

## Summary

**Phase 1 successfully reduced P99 latency by 98x.**

| Metric | Baseline | Phase 1 | Improvement |
|--------|----------|---------|-------------|
| Median | 556 ns | 666 ns | -20% (expected) |
| P90 | 666 ns | 769 ns | -15% |
| P95 | 37 μs | 778 ns | **47x better** |
| P99 | 236 μs | 2.4 μs | **98x better** |
| P99.9 | 525 μs | 3.5 μs | **150x better** |
| P99.99 | 906 μs | 13 μs | **70x better** |
| Fast (<1μs) | 91.3% | 96.2% | +5% |

---

## What Changed

Added to `control_loop.c`:

```c
// Lock memory to prevent page faults
mlockall(MCL_CURRENT | MCL_FUTURE);

// Set SCHED_FIFO with max priority
struct sched_param param;
param.sched_priority = 99;
sched_setscheduler(0, SCHED_FIFO, &param);
```

---

## Distribution Analysis

### Before SCHED_FIFO

```
P 50.00:        556 ns
P 90.00:        666 ns
P 95.00:     37,123 ns   ← OS scheduler kicks in
P 99.00:    235,818 ns
P 99.90:    524,691 ns
P 99.99:    906,348 ns

>  1000 ns:  4328 samples (8.66%)
>  5000 ns:  3787 samples (7.57%)
> 10000 ns:  3205 samples (6.41%)
```

### After SCHED_FIFO

```
P 50.00:        666 ns
P 90.00:        769 ns
P 95.00:        778 ns   ← No more scheduler spike!
P 99.00:      2,435 ns
P 99.90:      3,510 ns
P 99.99:     13,176 ns

>  1000 ns:  1897 samples (3.79%)
>  5000 ns:    21 samples (0.04%)
> 10000 ns:     6 samples (0.01%)
```

---

## Key Observations

1. **Scheduler preemption eliminated** - P95 dropped from 37μs to 778ns
2. **Bimodal distribution flattened** - No longer 9% in slow mode
3. **Rare extreme outliers remain** - 3 samples > 100μs (kernel interrupts)
4. **Median slightly higher** - 666ns vs 556ns (mlockall/setup overhead)

---

## What's Causing Remaining Slow Samples?

The 3.8% of samples > 1μs are likely:

1. **Timer interrupts** - Kernel still sends some ticks (nohz_full needed)
2. **Kernel housekeeping** - RCU, workqueues (isolcpus needed)
3. **Hardware interrupts** - IRQs on our cores (irqaffinity needed)

The 6 samples > 10μs are likely:
1. **Kernel preemption points** - Even SCHED_FIFO can't prevent all
2. **TLB shootdowns** - Other cores invalidating TLB

The 3 samples > 100μs (max 53ms!) are likely:
1. **Thermal throttling** - Frequency reduction (check `cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq`)
2. **Major kernel event** - Rare but unavoidable without RT kernel

---

## Revised Claims

After Phase 1, we can honestly claim:

> "Sub-microsecond median coordination with 2.4μs P99 latency, suitable for 10kHz+ robotics control on Linux with SCHED_FIFO."

This is a **huge improvement** over the baseline.

---

## Next Steps

**If P99 < 5μs is acceptable:** Done! This is production-ready for many applications.

**If P99 < 1μs needed:** Proceed to Phase 4 (kernel parameters):
- isolcpus=0,1,2
- nohz_full=0,1,2
- rcu_nocbs=0,1,2

**If P99.99 < 1μs needed:** Proceed to Phase 5 (PREEMPT_RT kernel)

---

## Reproduction

```bash
# Build with SCHED_FIFO support
make clean && make all

# Run on Thor with privileges
docker exec --privileged entromorphic-dev bash -c \
  "cd /workspace/trixV/zor/reflex-robotics && taskset -c 0-2 ./build/control_loop"
```

---

## Conclusion

**Phase 1 achieved the primary goal:** Reduce P99 from 236μs to <10μs.

SCHED_FIFO + mlockall is a simple, zero-kernel-modification solution that provides:
- 98x improvement in P99
- 96% of cycles under 1μs
- Suitable for soft real-time robotics

For hard real-time guarantees, proceed to kernel hardening (Phase 4) or PREEMPT_RT (Phase 5).

---

*Phase 1 complete. Gap significantly closed.*
