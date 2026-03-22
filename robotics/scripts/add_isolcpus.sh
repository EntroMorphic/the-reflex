#!/bin/bash
#
# add_isolcpus.sh - Add CPU isolation to Thor boot configuration
#
# WARNING: This modifies boot configuration. Requires reboot.
#
# Run this ON THE HOST:
#   sudo ./add_isolcpus.sh
#   sudo reboot
#

set -e

EXTLINUX="/boot/extlinux/extlinux.conf"
BACKUP="/boot/extlinux/extlinux.conf.bak.$(date +%Y%m%d_%H%M%S)"
ISOLCPUS_PARAMS="isolcpus=0,1,2 nohz_full=0,1,2 rcu_nocbs=0,1,2"

echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║       REFLEX ROBOTICS: ADD ISOLCPUS TO BOOT                   ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "ERROR: Must run as root (sudo)"
    exit 1
fi

# Check if extlinux.conf exists
if [ ! -f "$EXTLINUX" ]; then
    echo "ERROR: $EXTLINUX not found"
    exit 1
fi

# Check if already configured
if grep -q "isolcpus" "$EXTLINUX"; then
    echo "isolcpus already configured in $EXTLINUX"
    echo "Current config:"
    grep "APPEND" "$EXTLINUX"
    exit 0
fi

# Note: nohz_full requires CONFIG_NO_HZ_FULL=y which is NOT set in JetPack kernel
# We'll add it anyway for documentation, but it won't take effect
echo "Note: nohz_full requires kernel rebuild (CONFIG_NO_HZ_FULL not set)"
echo "      Only isolcpus and rcu_nocbs will take effect."
echo

# Create backup
echo "Creating backup: $BACKUP"
cp "$EXTLINUX" "$BACKUP"

# Add isolcpus to APPEND line
echo "Adding: $ISOLCPUS_PARAMS"
sed -i "s/\(APPEND.*\)/\1 $ISOLCPUS_PARAMS/" "$EXTLINUX"

echo
echo "=== New Configuration ==="
grep "APPEND" "$EXTLINUX"

echo
echo "=== REBOOT REQUIRED ==="
echo ""
echo "Run: sudo reboot"
echo ""
echo "After reboot, verify with:"
echo "  cat /sys/devices/system/cpu/isolated"
echo "  (should show: 0-2)"
echo ""
echo "To restore original config if issues:"
echo "  sudo cp $BACKUP $EXTLINUX"
echo "  sudo reboot"
echo
