# Silicon Grail - Manual Deployment Guide

**CPU-Free Turing Complete ETM Fabric Deployment**

---

## Quick Start (Automated)

```bash
cd /home/ztflynn/001/the-reflex
./silicon_grail_deploy.sh
```

This will:
1. Build the firmware
2. Deploy to all 3 C6 devices
3. Verify installation

---

## Manual Step-by-Step Deployment

### Step 1: Build Firmware

```bash
cd /home/ztflynn/001/the-reflex/reflex-os

# Ensure correct source file
# Edit main/CMakeLists.txt:
#   SRCS "silicon_grail_cpu_free.c"

# Build
source ~/esp/esp-idf/export.sh
idf.py build
```

**Expected output:**
- Build succeeds
- Binary created: `build/reflex_os.bin`
- Size: ~150-200KB

### Step 2: Verify Devices

```bash
cd /home/ztflynn/001/the-reflex/reflex-deploy
python -m reflex_cli scan
```

**Expected:** 3 devices found on /dev/ttyACM0, /dev/ttyACM1, /dev/ttyACM2

### Step 3: Pre-Flight Check

```bash
python -m reflex_cli verify /dev/ttyACM0
```

**Expected:** All 6 checks pass

### Step 4: Deploy Firmware

**Option A: Deploy to all devices:**
```bash
python -m reflex_cli install all \
    --config configs/silicon_grail.yaml \
    --firmware-dir ../reflex-os/build
```

**Option B: Deploy to specific device:**
```bash
python -m reflex_cli install /dev/ttyACM0 \
    --config configs/silicon_grail.yaml \
    --firmware-dir ../reflex-os/build \
    --backup
```

### Step 5: Monitor Output

```bash
idf.py -p /dev/ttyACM0 monitor
```

**Expected output:**
```
╔════════════════════════════════════════════════════════════════╗
║           SILICON GRAIL - CPU-FREE TURING FABRIC               ║
╚════════════════════════════════════════════════════════════════╝

Initializing hardware...
  ✓ RMT initialized (CH0, GPIO4, 10MHz)
  ✓ PCNT initialized (UNIT0, threshold=50)
  ✓ GDMA initialized (CH0=fast/prio15, CH1=default/prio0)
  ✓ ETM crossbar wired (6 channels)

✓ Hardware initialization complete

╔════════════════════════════════════════════════════════════════╗
║         CPU-FREE DEMO (with periodic stats output)             ║
╚════════════════════════════════════════════════════════════════╝

Running 100 cycles with CPU waking every 10 cycles for stats...
  Progress: 10/100 cycles
  Progress: 20/100 cycles
  ...
  Progress: 100/100 cycles

╔════════════════════════════════════════════════════════════════╗
║                    FABRIC STATISTICS                           ║
╠════════════════════════════════════════════════════════════════╣
║                                                                ║
║  Total cycles:             100                                 ║
║  Fast path taken:          42 (42.0%)                         ║
║  Default path taken:       58 (58.0%)                         ║
║  Errors:                   0                                   ║
║                                                                ║
╚════════════════════════════════════════════════════════════════╝

Silicon Grail demo complete.
Status: PARTIAL

To achieve FULL CPU-free operation:
  1. Implement Timer1 for timeout race
  2. Complete ETM wiring for both paths
  3. Run start_autonomous_fabric()
  4. Verify 1000+ cycles without CPU wake
```

---

## Success Criteria

### Minimum Success
- [ ] Firmware boots successfully
- [ ] 100 autonomous cycles complete
- [ ] Both fast and default paths taken
- [ ] Zero errors

### Full Success (Turing Complete)
- [ ] 1000+ cycles without CPU wake
- [ ] Timer race branching verified
- [ ] Power consumption < 50μA measured
- [ ] GDMA M2M to RMT RAM confirmed

---

## Troubleshooting

### Build Fails
```bash
# Clean and rebuild
idf.py fullclean
idf.py build
```

### Device Not Found
```bash
# Check permissions
sudo usermod -a -G dialout $USER
# Log out and back in

# Check devices
ls -la /dev/ttyACM*
```

### Flash Fails
```bash
# Try lower baud rate
idf.py -p /dev/ttyACM0 -b 460800 flash

# Or use reflex-cli with force
python -m reflex_cli install /dev/ttyACM0 --force
```

### Monitor Shows Garbage
```bash
# Reset device
python -m reflex_cli recover /dev/ttyACM0
```

---

## Files Created

| File | Purpose |
|------|---------|
| `silicon_grail_deploy.sh` | Automated deployment script |
| `reflex-os/main/silicon_grail_cpu_free.c` | CPU-free test firmware |
| `reflex-deploy/configs/silicon_grail.yaml` | Deployment configuration |
| `docs/PRD_ETM_FABRIC_COMPLETION.md` | Requirements & test plan |
| `docs/ETM_FABRIC_PHASE1_SUMMARY.md` | Implementation details |

---

## Next Steps After Deployment

1. **Verify GDMA M2M** - Pattern written to RMT RAM
2. **Test Timer Race** - Priority-based winner selection
3. **Complete ETM Wiring** - Full autonomous loop
4. **Demonstrate WFI** - 1000+ cycles CPU-free
5. **Measure Power** - Verify ~17μW target

---

**The silicon is waiting. Let's prove the Turing Complete fabric.**
