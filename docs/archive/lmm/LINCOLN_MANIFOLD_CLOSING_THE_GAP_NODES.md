# Lincoln Manifold: Closing the Gap - NODES

> Key concepts and their relationships
> Extracted from RAW exploration

---

## Core Problem Node

```
THE GAP
├── Current: 556ns median, 236μs P99
├── Target: 556ns median, <50μs P99
├── Ratio: 424x between median and P99
├── Cause: 9% of cycles hit OS slow path
└── Root: Kernel doesn't know we're real-time
```

---

## Interference Sources (Ranked by Impact)

```
SLOW PATH CAUSES
│
├── [HIGH IMPACT]
│   ├── Scheduler preemption (CFS timeslice expiry)
│   ├── Timer interrupts (HZ=250 = 4ms ticks)
│   └── Kernel thread execution on our cores
│
├── [MEDIUM IMPACT]
│   ├── RCU callbacks (deferred work)
│   ├── Softirqs (network, block)
│   ├── Workqueue threads (kworker)
│   └── cgroup accounting (container overhead)
│
└── [LOW IMPACT]
    ├── Page faults (solvable with mlockall)
    ├── TLB shootdowns (rare for dedicated cores)
    ├── Cache pollution (kernel code)
    └── Thermal throttling (stable workload)
```

---

## Solution Hierarchy

```
SOLUTIONS (by difficulty)
│
├── TIER 1: Userspace (no privileges needed)
│   ├── SCHED_FIFO priority 99
│   ├── mlockall(MCL_CURRENT | MCL_FUTURE)
│   ├── CPU affinity (already implemented)
│   ├── Memory prefaulting
│   └── Compiler optimizations
│
├── TIER 2: Kernel Parameters (root needed)
│   ├── isolcpus=0,1,2
│   ├── nohz_full=0,1,2
│   ├── rcu_nocbs=0,1,2
│   ├── irqaffinity (move IRQs away)
│   └── CPU governor: performance
│
├── TIER 3: Kernel Rebuild (significant effort)
│   ├── PREEMPT_RT patch
│   ├── Disable unnecessary features
│   └── Custom kernel config
│
└── TIER 4: Bypass Kernel (major effort)
    ├── Xenomai dual-kernel
    ├── Jailhouse hypervisor
    ├── Zephyr on dedicated cores
    └── Bare metal execution
```

---

## Expected Improvements

```
IMPROVEMENT PREDICTIONS
│
├── Baseline (current)
│   ├── Median: 556ns
│   ├── P90: 666ns
│   ├── P99: 236μs
│   └── Slow path: 9%
│
├── After SCHED_FIFO + mlockall
│   ├── Median: ~550ns (unchanged)
│   ├── P90: ~600ns
│   ├── P99: ~50μs (predicted)
│   └── Slow path: <1%
│
├── After isolcpus + nohz_full
│   ├── Median: ~500ns
│   ├── P90: ~550ns
│   ├── P99: ~10μs (predicted)
│   └── Slow path: <0.1%
│
└── After PREEMPT_RT
    ├── Median: ~500ns
    ├── P90: ~550ns
    ├── P99: ~5μs (predicted)
    └── Slow path: <0.01%
```

---

## Implementation Nodes

### Node: SCHED_FIFO Implementation

```c
// Required headers
#include <sched.h>
#include <sys/mman.h>

// Set real-time priority
struct sched_param param;
param.sched_priority = 99;  // Max priority
sched_setscheduler(0, SCHED_FIFO, &param);

// Lock memory
mlockall(MCL_CURRENT | MCL_FUTURE);
```

**Dependencies:** CAP_SYS_NICE capability or root
**Risk:** Can starve other processes
**Mitigation:** Only on dedicated cores

---

### Node: Kernel Parameters

```bash
# Boot parameters (add to GRUB)
isolcpus=0,1,2 nohz_full=0,1,2 rcu_nocbs=0,1,2

# Runtime IRQ affinity
echo 3-13 > /proc/irq/default_smp_affinity
for irq in /proc/irq/*/smp_affinity; do
    echo fff8 > $irq 2>/dev/null  # Cores 3-13 only
done

# CPU governor
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance > $cpu
done
```

**Dependencies:** Root access, kernel support
**Risk:** Reduces flexibility
**Mitigation:** Only isolate needed cores

---

### Node: Measurement Strategy

```
WHAT TO MEASURE
│
├── Distribution shape (histogram)
├── Percentiles (P50, P90, P95, P99, P99.9, P99.99)
├── Slow path frequency (% > threshold)
├── Slow path magnitude (median of slow samples)
└── Run-to-run variance (5+ runs)

HOW TO COMPARE
│
├── Same conditions (thermal, load)
├── Same duration (50,000 samples)
├── Same core assignment
└── Multiple runs for statistics
```

---

## Relationship Map

```
SCHED_FIFO ─────────┬─────────► Eliminates CFS preemption
                    │
mlockall ───────────┼─────────► Eliminates page faults
                    │
isolcpus ───────────┼─────────► Removes kernel threads
                    │
nohz_full ──────────┼─────────► Removes timer ticks
                    │
rcu_nocbs ──────────┼─────────► Moves RCU elsewhere
                    │
PREEMPT_RT ─────────┴─────────► Makes kernel preemptible
                    │
                    ▼
            BOUNDED LATENCY
```

---

## Decision Tree

```
START
│
├── Is P99 > 50μs?
│   ├── YES → Implement SCHED_FIFO
│   └── NO → Done (acceptable)
│
├── After SCHED_FIFO, is P99 > 10μs?
│   ├── YES → Add kernel parameters
│   └── NO → Done (good)
│
├── After kernel params, is P99 > 5μs?
│   ├── YES → Build PREEMPT_RT kernel
│   └── NO → Done (excellent)
│
└── After PREEMPT_RT, is P99 > 2μs?
    ├── YES → Consider bare metal
    └── NO → Done (optimal)
```

---

## Risk Assessment

| Solution | Effort | Risk | Reversible |
|----------|--------|------|------------|
| SCHED_FIFO | Low | Low | Yes |
| mlockall | Low | Low | Yes |
| isolcpus | Medium | Low | Reboot |
| nohz_full | Medium | Low | Reboot |
| PREEMPT_RT | High | Medium | Reflash |
| Bare metal | Very High | High | Complex |

---

## Success Criteria

```
MINIMUM VIABLE
├── P99 < 100μs (fits in 10kHz period)
└── Slow path < 1%

TARGET
├── P99 < 50μs (50% margin)
└── Slow path < 0.1%

STRETCH
├── P99 < 10μs (10x margin)
└── Slow path < 0.01%
```

---

## Next Actions

1. **Implement SCHED_FIFO** in control_loop.c
2. **Add mlockall** at startup
3. **Measure improvement** with same methodology
4. **Document results** for next iteration
5. **Decide** if kernel parameters needed
