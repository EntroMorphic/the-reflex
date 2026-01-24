# Lincoln Manifold: Closing the Gap - SYNTH

> Actionable synthesis and implementation plan
> From 236μs P99 to <10μs P99

---

## Executive Summary

**Current State:** 556ns median, 236μs P99 (424x ratio)
**Target State:** 556ns median, <10μs P99 (18x ratio)
**Required Improvement:** 24x reduction in P99

**Strategy:** Layered hardening from userspace to kernel

---

## The Attack Plan

### Phase 1: Userspace Hardening (Immediate)

**Effort:** 1 hour
**Expected P99:** 236μs → ~80μs (3x improvement)

#### Implementation

Add to `control_loop.c`:

```c
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>

static void setup_realtime(void) {
    // Lock all memory
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("mlockall failed");
    }

    // Set SCHED_FIFO with max priority
    struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
        perror("sched_setscheduler failed (need root/CAP_SYS_NICE)");
    }
}

// Call in main() before starting threads
int main() {
    setup_realtime();
    // ... rest of main
}
```

For each thread:

```c
void* sensor_thread(void* arg) {
    // Set thread priority
    struct sched_param param;
    param.sched_priority = 99;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    // ... rest of thread
}
```

#### Verification

```bash
# Run with capabilities
sudo setcap cap_sys_nice+ep ./build/control_loop
./build/control_loop

# Or run as root
sudo ./build/control_loop
```

---

### Phase 2: Container vs Native Test (Immediate)

**Effort:** 15 minutes
**Purpose:** Quantify container overhead

#### Implementation

```bash
# On Thor, run OUTSIDE container
ssh -p 11965 ztflynn@10.42.0.2

# Copy binary and run natively
cp /home/ztflynn/entromorphic-workspace/001/trixV/zor/reflex-robotics/build/control_loop /tmp/
sudo taskset -c 0-2 /tmp/control_loop
```

Compare P99 between container and native runs.

---

### Phase 3: Kernel Configuration Check (Immediate)

**Effort:** 10 minutes
**Purpose:** Understand what's available

#### Implementation

```bash
# Check current boot parameters
cat /proc/cmdline

# Check kernel config for RT/isolation support
zcat /proc/config.gz | grep -E "(PREEMPT|NOHZ|RCU_NOCB|ISOLCPUS)"

# Check CPU isolation capability
ls /sys/devices/system/cpu/isolated
cat /sys/devices/system/cpu/isolated

# Check available governors
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors
```

---

### Phase 4: Kernel Parameters (If Phase 1-3 insufficient)

**Effort:** 1 hour + reboot
**Expected P99:** ~80μs → ~15μs (5x improvement)

#### Implementation

Edit `/boot/extlinux/extlinux.conf` (JetPack uses extlinux, not GRUB):

```
APPEND ... isolcpus=0,1,2 nohz_full=0,1,2 rcu_nocbs=0,1,2
```

Configure IRQ affinity after boot:

```bash
#!/bin/bash
# Move all IRQs to cores 3-13
for irq in /proc/irq/*/smp_affinity; do
    echo fff8 > "$irq" 2>/dev/null
done

# Set performance governor
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance > "$cpu"
done
```

---

### Phase 5: PREEMPT_RT Kernel (If Phase 4 insufficient)

**Effort:** 4-8 hours
**Expected P99:** ~15μs → ~3μs (5x improvement)

#### Research First

```bash
# Check if NVIDIA provides RT kernel
# JetPack documentation: developer.nvidia.com
# Look for "real-time" or "Drive" variants

# Check if RT patch exists for this kernel version
uname -r  # Get kernel version
# Compare with https://wiki.linuxfoundation.org/realtime/start
```

#### Build Process (if needed)

1. Download JetPack kernel source
2. Download matching PREEMPT_RT patch
3. Apply patch
4. Configure kernel with RT options
5. Build with NVIDIA driver compatibility
6. Flash to Thor
7. Test NVIDIA drivers still work

**Warning:** This is complex. Verify NVIDIA driver compatibility first.

---

## Measurement Protocol

### For Each Phase

Run 5 times, report:
- Median (should stay ~550ns)
- P90 (target: <1μs)
- P95 (target: <5μs)
- P99 (target: <10μs)
- P99.9 (target: <50μs)
- Slow path % (target: <0.1%)

### Comparison Script

```python
#!/usr/bin/env python3
import numpy as np
import sys

def analyze(csv_file, label):
    data = np.genfromtxt(csv_file, delimiter=',', names=True)
    total = data['total_loop_ns']

    print(f"\n=== {label} ===")
    for p in [50, 90, 95, 99, 99.9]:
        print(f"P{p:5.1f}: {np.percentile(total, p):10.1f} ns")

    slow = np.sum(total > 1000) / len(total) * 100
    print(f"Slow path: {slow:.2f}%")

# Usage: python3 compare.py baseline.csv phase1.csv phase2.csv
for i, f in enumerate(sys.argv[1:]):
    analyze(f, f"Phase {i}")
```

---

## Success Criteria

| Phase | P99 Target | Slow Path Target | Decision |
|-------|------------|------------------|----------|
| Baseline | 236μs | 9% | Continue |
| Phase 1 | <100μs | <5% | If met, Phase 2 optional |
| Phase 4 | <20μs | <0.5% | If met, Phase 5 optional |
| Phase 5 | <5μs | <0.1% | Success |

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| SCHED_FIFO starves system | Only on dedicated cores, verify with htop |
| Kernel params break boot | Keep fallback config, test on reboot |
| PREEMPT_RT breaks NVIDIA | Test in VM first if possible |
| Container is main issue | Phase 2 identifies this early |

---

## Deliverables

1. **control_loop.c v2** - With SCHED_FIFO + mlockall
2. **PHASE_1_RESULTS.md** - Measurements after userspace hardening
3. **CONTAINER_VS_NATIVE.md** - Comparison data
4. **KERNEL_CONFIG.md** - JetPack kernel capabilities
5. **Final benchmark** - Best achievable on current kernel

---

## Timeline

| Task | Duration | Dependency |
|------|----------|------------|
| Implement SCHED_FIFO | 30 min | None |
| Test Phase 1 | 15 min | SCHED_FIFO |
| Native vs Container test | 15 min | None |
| Kernel config check | 10 min | None |
| Document Phase 1 results | 15 min | Tests complete |
| **Total Phase 1** | **~1.5 hours** | |
| Kernel parameters | 1 hour | Phase 1 results |
| Test Phase 4 | 15 min | Reboot |
| **Total Phase 4** | **~1.5 hours** | Phase 1 |
| PREEMPT_RT research | 2 hours | Phase 4 results |
| PREEMPT_RT build | 4+ hours | Research |
| **Total Phase 5** | **~8 hours** | Phase 4 |

---

## Action Items (Prioritized)

1. **NOW:** Implement SCHED_FIFO + mlockall in control_loop.c
2. **NOW:** Sync to Thor and test
3. **NOW:** Run native vs container comparison
4. **NOW:** Check kernel config
5. **NEXT:** Document results, decide on Phase 4
6. **LATER:** Kernel parameters if needed
7. **FUTURE:** PREEMPT_RT if still needed

---

## The Honest Pitch

After Phase 1:
> "Sub-microsecond median coordination with <100μs worst-case on commodity Linux"

After Phase 4:
> "Sub-microsecond coordination with <20μs bounded latency for 10kHz robotics"

After Phase 5:
> "Guaranteed sub-10μs latency for hard real-time robotics at 10kHz+"

Each phase is a valid stopping point with an honest, defensible claim.

---

*"Perfection is achieved not when there is nothing more to add, but when there is nothing left to take away."* - Antoine de Saint-Exupéry

*Synthesis complete. Execute Phase 1 immediately.*
