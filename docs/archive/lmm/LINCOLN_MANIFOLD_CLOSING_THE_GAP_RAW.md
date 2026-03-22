# Lincoln Manifold: Closing the Gap - RAW

> Stream of consciousness exploration
> Target: Eliminate the 9% slow path, achieve bounded worst-case latency

---

## The Gap

We have 556ns median but 236μs P99. That's a 424x ratio. Unacceptable for hard real-time. 9% of cycles are hitting the slow path. Why? OS scheduler preemption.

The kernel doesn't know our threads are time-critical. It treats them like any other userspace threads. When the scheduler tick fires, it might preempt our spinning thread to run something else. Even with dedicated cores, the kernel still runs housekeeping.

---

## Stream of Consciousness

What causes the slow path? Let's enumerate:

1. **Scheduler preemption** - CFS doesn't know we're real-time
2. **Timer interrupts** - Even idle cores get timer ticks
3. **RCU callbacks** - Kernel deferred work
4. **Workqueues** - kworker threads
5. **Softirqs** - Network, block I/O
6. **Page faults** - If we touch new memory
7. **TLB shootdowns** - Other cores invalidating TLB
8. **Cache pollution** - Kernel code evicting our hot lines

Wait. We're in a container. Docker adds:
9. **cgroup accounting** - CPU time tracking
10. **Namespace overhead** - PID/network lookups

And we're on Jetson with:
11. **Thermal throttling** - Frequency scaling
12. **Power management** - Core sleep states
13. **GPU interrupts** - NVIDIA driver activity

---

## What Can We Control?

**Userspace (no kernel changes):**
- SCHED_FIFO with max priority
- mlockall() to prevent page faults
- CPU affinity (already doing)
- Disable interrupts? No, userspace can't
- isolcpus kernel parameter
- nohz_full for tickless operation
- rcu_nocbs to move RCU off our cores

**Kernel parameters:**
- isolcpus=0,1,2
- nohz_full=0,1,2
- rcu_nocbs=0,1,2
- irqaffinity (move IRQs off our cores)

**Kernel rebuild:**
- PREEMPT_RT patch
- Disable features we don't need

**Hardware:**
- Disable hyperthreading (N/A on ARM)
- Disable frequency scaling
- Disable C-states

---

## The SCHED_FIFO Question

Why aren't we using SCHED_FIFO? It's the obvious first step.

SCHED_FIFO means: "Never preempt me for a lower priority task."

With SCHED_FIFO at priority 99, only:
- Other SCHED_FIFO 99 threads
- Kernel threads with higher priority
- Hardware interrupts

...can preempt us.

This should eliminate most of the 9% slow path. Let's estimate:
- Scheduler preemption: eliminated
- Timer interrupts: still happen but we resume immediately
- RCU/workqueues: won't preempt us
- Softirqs: run at lower priority

Prediction: SCHED_FIFO should reduce slow path from 9% to <1%.

---

## The isolcpus Question

Even with SCHED_FIFO, the kernel runs things on our cores:
- Timer tick handler
- IPI handlers
- Kernel threads that haven't been migrated

isolcpus=0,1,2 tells the kernel: "Don't schedule normal tasks on these cores."

Combined with:
- nohz_full=0,1,2: "Don't send timer ticks to idle cores"
- rcu_nocbs=0,1,2: "Run RCU callbacks elsewhere"

This creates truly isolated cores.

Prediction: isolcpus + nohz_full + rcu_nocbs should reduce slow path to <0.1%.

---

## The PREEMPT_RT Question

Even with all the above, the kernel itself isn't fully preemptible. In vanilla Linux:
- Spinlock holders can't be preempted
- Interrupt handlers run to completion
- Some code paths disable preemption

PREEMPT_RT converts:
- Spinlocks to RT-mutexes (preemptible)
- Interrupt handlers to threads (schedulable)
- Adds priority inheritance

This is the gold standard for Linux real-time.

Prediction: PREEMPT_RT should bound worst-case to <50μs.

---

## The Bare Metal Question

Why not bypass Linux entirely for the control loop?

Options:
1. **Xenomai/RTAI** - Dual kernel, RT tasks run on co-kernel
2. **Jailhouse** - Hypervisor that partitions cores
3. **Zephyr RTOS** - Run on dedicated cores
4. **Bare metal** - No OS, just our code

Thor has 14 cores. We could:
- Run Linux on cores 3-13
- Run bare metal RT on cores 0-2

This is complex but achievable. NVIDIA's Drive platform does this.

Prediction: Bare metal could achieve <100ns worst-case.

---

## Quick Wins (No Kernel Changes)

What can we do RIGHT NOW without modifying the kernel?

1. **SCHED_FIFO** - Add to our code
2. **mlockall()** - Prevent page faults
3. **Disable CPU frequency scaling** - userspace governor
4. **Thread priority** - Nice -20 as fallback
5. **Memory prefault** - Touch all memory at startup
6. **Compiler hints** - likely/unlikely, prefetch

Let's implement SCHED_FIFO first and measure the improvement.

---

## The Container Problem

We're running in Docker. Does this matter?

Docker uses cgroups for:
- CPU limiting (not our issue, we have dedicated cores)
- Memory limiting (could cause OOM delays)
- I/O limiting (not relevant for CPU-bound)

And namespaces for:
- PID namespace (minimal overhead)
- Network namespace (using host mode)
- Mount namespace (minimal overhead)

The cgroup CPU accounting might add overhead on every context switch. But with SCHED_FIFO, we shouldn't be context switching.

Prediction: Container overhead is minimal with proper configuration.

---

## The Measurement Problem

Are we measuring correctly? Our measurement includes:
- Signal send (memory write + barrier)
- Cache invalidation propagation
- Signal detection (memory read in spin loop)
- Timestamp capture

But NOT:
- Time from "decision to signal" to "signal sent"
- Time from "signal detected" to "action taken"

The true end-to-end latency might be higher. We're measuring the coordination primitive, not the full control loop.

This is actually fine - we're benchmarking what we claim to benchmark.

---

## Implementation Priority

1. **SCHED_FIFO + mlockall** - Userspace, immediate
2. **Measure improvement** - Expect 9% → <1%
3. **Kernel parameters** - isolcpus, nohz_full, rcu_nocbs
4. **Measure improvement** - Expect <1% → <0.1%
5. **PREEMPT_RT** - Kernel rebuild
6. **Measure improvement** - Expect bounded <50μs worst-case
7. **Bare metal** - Future work if needed

---

## What Does "Good Enough" Look Like?

For 10kHz control (100μs period):
- P99 < 50μs: Acceptable (50% margin)
- P99.9 < 80μs: Good (20% margin)
- P99.99 < 95μs: Excellent (5% margin)

Current: P99 = 236μs (exceeds period!)

Target: P99 < 50μs (4.7x improvement needed)

---

## Random Thoughts

- Could we use the GPU for timing? CUDA has precise timers.
- What about using ARM's Generic Interrupt Controller directly?
- Is there a way to disable interrupts from userspace on ARM? (Probably not without kernel module)
- Could we use a kernel module to create a truly isolated execution environment?
- What about using the Cortex-R cores on Thor for real-time? (If they exist)

---

## The Thor-Specific Angle

Thor is designed for autonomous vehicles. NVIDIA must have solved this problem for Drive. What do they do?

Likely:
- Custom kernel with RT extensions
- Hypervisor partitioning (like Jailhouse)
- Safety-certified RTOS for critical tasks
- Linux for non-critical tasks

Can we access any of this on the dev kit?

---

## End of Stream

Key insight: The gap is not in our coordination primitive. The gap is in the OS. We have three paths:

1. **Harden userspace** - SCHED_FIFO, mlockall (easy)
2. **Harden kernel** - isolcpus, PREEMPT_RT (medium)
3. **Bypass kernel** - Bare metal, hypervisor (hard)

Start with #1, measure, decide if we need #2 or #3.
