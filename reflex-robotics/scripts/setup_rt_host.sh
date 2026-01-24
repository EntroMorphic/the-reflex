#!/bin/bash
#
# setup_rt_host.sh - Configure Thor host for real-time operation
#
# Run this ON THE HOST (not in container):
#   sudo ./setup_rt_host.sh
#
# This script:
# 1. Sets CPU governor to performance
# 2. Moves IRQs off cores 0-2
# 3. Disables kernel watchdog on cores 0-2
# 4. Optionally modifies boot params for isolcpus
#

set -e

echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║       REFLEX ROBOTICS: HOST RT CONFIGURATION                  ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "ERROR: Must run as root (sudo)"
    exit 1
fi

# ============================================================================
# Phase 4a: CPU Governor
# ============================================================================
echo "=== Setting Performance Governor ==="
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance > "$cpu" 2>/dev/null && echo "  Set $cpu"
done
echo "  Current: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
echo

# ============================================================================
# Phase 4b: IRQ Affinity
# ============================================================================
echo "=== Moving IRQs to Cores 3-13 ==="
# 0xfff8 = cores 3-13 (bits 3-13 set)
echo fff8 > /proc/irq/default_smp_affinity

moved=0
failed=0
for irq in /proc/irq/[0-9]*/smp_affinity; do
    if echo fff8 > "$irq" 2>/dev/null; then
        ((moved++))
    else
        ((failed++))
    fi
done
echo "  Moved: $moved IRQs"
echo "  Failed: $failed IRQs (some are CPU-bound)"
echo "  Default affinity: $(cat /proc/irq/default_smp_affinity)"
echo

# ============================================================================
# Phase 4c: Kernel Watchdog (if available)
# ============================================================================
echo "=== Disabling Kernel Watchdog on RT Cores ==="
if [ -f /proc/sys/kernel/watchdog_cpumask ]; then
    # Set watchdog to only run on cores 3-13
    echo fff8 > /proc/sys/kernel/watchdog_cpumask
    echo "  Watchdog mask: $(cat /proc/sys/kernel/watchdog_cpumask)"
else
    echo "  Watchdog cpumask not available"
fi
echo

# ============================================================================
# Verification
# ============================================================================
echo "=== Verification ==="
echo "  Governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
echo "  IRQ affinity: $(cat /proc/irq/default_smp_affinity)"
echo "  Isolated cores: $(cat /sys/devices/system/cpu/isolated 2>/dev/null || echo 'none')"
echo

# ============================================================================
# Instructions for Boot Parameters
# ============================================================================
echo "=== OPTIONAL: Boot Parameters for Full Isolation ==="
echo ""
echo "To add isolcpus at boot, edit /boot/extlinux/extlinux.conf:"
echo ""
echo "  1. Backup: sudo cp /boot/extlinux/extlinux.conf /boot/extlinux/extlinux.conf.bak"
echo ""
echo "  2. Edit the APPEND line to add:"
echo "     isolcpus=0,1,2 rcu_nocbs=0,1,2"
echo ""
echo "  3. Reboot: sudo reboot"
echo ""
echo "  4. Verify after reboot:"
echo "     cat /sys/devices/system/cpu/isolated"
echo "     Should show: 0-2"
echo

# ============================================================================
# Run Benchmark
# ============================================================================
echo "=== Ready to Run Benchmark ==="
echo ""
echo "Run the benchmark with:"
echo "  sudo taskset -c 0-2 /path/to/control_loop"
echo ""
echo "Or from container with host networking:"
echo "  docker exec --privileged entromorphic-dev taskset -c 0-2 ./build/control_loop"
echo ""
