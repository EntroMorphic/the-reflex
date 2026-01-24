# Phase 4: Kernel Parameter Isolation

> To take this all the way to sub-microsecond P99

---

## Current Status

| Metric | Baseline | Phase 1 (SCHED_FIFO) | Target |
|--------|----------|----------------------|--------|
| P99 | 236 μs | 2.4 μs | <1 μs |
| Slow path | 8.7% | 3.8% | <0.1% |

Phase 1 got us 98x improvement. Phase 4 should get us another 2-3x.

---

## Why Phase 4 is Needed

The remaining 3.8% slow samples are caused by:

1. **Timer interrupts** - Kernel sends ticks even to SCHED_FIFO threads
2. **Kernel housekeeping** - RCU, workqueues run on our cores
3. **IRQs landing on our cores** - Hardware interrupts

These require kernel-level configuration that can't be done from within a container.

---

## Requirements

1. **SSH access to Thor host** (not container)
2. **sudo privileges**
3. **Willingness to reboot** (for boot parameters)

---

## Step-by-Step Instructions

### Step 1: Copy Scripts to Thor

```bash
# From workstation
rsync -avz -e 'ssh -p 11965' \
  /home/ztflynn/001/trixV/zor/reflex-robotics/scripts/ \
  ztflynn@10.42.0.2:/home/ztflynn/entromorphic-workspace/001/trixV/zor/reflex-robotics/scripts/
```

### Step 2: Apply Runtime Settings (No Reboot)

```bash
# SSH to Thor
ssh -p 11965 ztflynn@10.42.0.2

# Run setup script
cd /home/ztflynn/entromorphic-workspace/001/trixV/zor/reflex-robotics
chmod +x scripts/setup_rt_host.sh
sudo ./scripts/setup_rt_host.sh
```

This applies:
- Performance CPU governor
- IRQ affinity to cores 3-13
- Watchdog mask to cores 3-13

### Step 3: Test with Runtime Settings

```bash
# Still on Thor host
cd /home/ztflynn/entromorphic-workspace/001/trixV/zor/reflex-robotics

# Rebuild (in case container binary differs)
make clean && make all

# Run with RT priority
sudo taskset -c 0-2 ./build/control_loop
```

Measure and compare to Phase 1 results.

### Step 4: Add Boot Parameters (Requires Reboot)

```bash
# Apply boot parameters
sudo ./scripts/add_isolcpus.sh

# Reboot Thor
sudo reboot
```

**Warning:** Thor will be offline during reboot (~2 minutes).

### Step 5: Verify After Reboot

```bash
# SSH back to Thor
ssh -p 11965 ztflynn@10.42.0.2

# Verify isolation
cat /sys/devices/system/cpu/isolated
# Should show: 0-2

cat /proc/cmdline | grep isolcpus
# Should contain: isolcpus=0,1,2

# Run benchmark
cd /home/ztflynn/entromorphic-workspace/001/trixV/zor/reflex-robotics
sudo taskset -c 0-2 ./build/control_loop
```

### Step 6: Restart Container (After Reboot)

```bash
# Container should auto-restart, but verify
docker ps | grep entromorphic-dev

# If not running
docker start entromorphic-dev
```

---

## Expected Results

After Phase 4 (isolcpus + rcu_nocbs):

| Metric | Phase 1 | Phase 4 (Expected) |
|--------|---------|-------------------|
| Median | 666 ns | ~600 ns |
| P99 | 2.4 μs | ~800 ns |
| P99.9 | 3.5 μs | ~1.5 μs |
| Slow path | 3.8% | <0.5% |

---

## Kernel Limitation

**Important:** The JetPack kernel does NOT have `CONFIG_NO_HZ_FULL=y`.

This means:
- `nohz_full` parameter will be ignored
- Timer interrupts will still occur (though less frequent)
- For full tickless operation, need kernel rebuild (Phase 5)

However, `isolcpus` and `rcu_nocbs` will work and should still provide significant improvement.

---

## Rollback Procedure

If Thor fails to boot or has issues:

1. Connect monitor/keyboard to Thor
2. At boot menu, select "backup kernel" (if configured)
3. Or boot recovery and restore:
   ```bash
   cp /boot/extlinux/extlinux.conf.bak.* /boot/extlinux/extlinux.conf
   reboot
   ```

---

## After Phase 4

If P99 is still > 1μs, proceed to Phase 5 (PREEMPT_RT kernel rebuild).

If P99 < 1μs: **Mission accomplished.** We've achieved sub-microsecond worst-case latency.

---

*"We take this all the way, or we go home."*
