# ESP32-C6 Flash & Serial Guide

Board: ESP32-C6FH4 (QFN32) revision v0.2, built-in USB Serial/JTAG (VID 0x303a, PID 0x1001).

## Prerequisites

```bash
source ~/esp/v5.4/export.sh
```

## Reset Synchronization (TEST 14C / Multi-Seed)

When running transition-mode or multi-seed experiments, Board B (sender) must be reset within ~30s of Board A so the sender's 90s enrollment cycling window overlaps Board A's Test 11 Phase 0a (30s, starting ~10s after boot).

**Verification protocol:** After resetting Board B via DTR/RTS toggle, briefly read its serial output and confirm you see the boot line:
```
[MODE] TRANSITION: ENROLL cycle (90s) -> P1 (90s) -> P2 (30s) -> repeat
```
or (for cycling mode):
```
[MODE] CYCLING: P0->P1->P2->P3 at 5s each
```

If no boot output appears within 5 seconds, the reset failed — the ESP32-C6 USB-CDC DTR/RTS reset is sometimes flaky. Retry, or use `esptool.py run` to force a reset.

**Measured timing (from `data/apr11_2026/full_suite_label_free_final.log`):**
- Board A boot + init: ~10s
- Phase 0a (enrollment observation): t=10-40s
- Phase 0d (TriX Cube): t=40-55s
- Phase 1 (ensemble): t=55-115s
- Board B cycling window: t=8-98s
- Overlap margin: ~28s (Phase 0d ends at t=55, cycling ends at t=98)

---

## Build Targets

`embedded/main/CMakeLists.txt` defines two targets via `REFLEX_TARGET`:

- `gie` (default): GIE + LP core + VDB + test suite (Board A / receiver)
- `sender`: ESP-NOW pattern sender (Board B)

Additional cmake defines:
- `-DLP_SEED=0x...` — override the LP init seed
- `-DSKIP_TO_14C=1` — skip Tests 1–13, run Test 11 enrollment + Test 14C
- `-DTRANSITION_MODE=1` — (sender only) P1 (90s) → P2 (30s) transition pattern

## Build

```bash
cd embedded
idf.py -DREFLEX_TARGET=gie build          # Board A
idf.py -DREFLEX_TARGET=sender build       # Board B
```

Output binaries land in `build/`:
- `build/bootloader/bootloader.bin`
- `build/reflex_os.bin`
- `build/partition_table/partition-table.bin`

## Flash

### 1. Enter Download Mode

Hold **BOOT** button, press **RESET**, release **BOOT**. The device enters download mode on `/dev/esp32c6a` (or `esp32c6b` for the second board).

### 2. Flash

```bash
esptool.py --chip esp32c6 -p /dev/esp32c6a -b 460800 \
  --before=no_reset --after=no_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB \
  0x0     build/bootloader/bootloader.bin \
  0x10000 build/reflex_os.bin \
  0x8000  build/partition_table/partition-table.bin
```

The `--before=no_reset --after=no_reset` flags are required because software reset via DTR/RTS does not work on this board's USB JTAG (throws BrokenPipeError).

### 3. Run and Capture Output

Press **RESET** (without BOOT). The USB disconnects briefly and re-enumerates.

**Python capture script:**

```python
import serial, time, glob, os

# Open port before reset, detect disconnect via exception
s = serial.Serial('/dev/esp32c6a', 115200, timeout=0.1)
s.reset_input_buffer()
print('Press RESET now')

output = b''
try:
    while True:
        c = s.read(4096)
        if c: output += c
except serial.SerialException:
    s.close()

# Wait for reconnect
for _ in range(400):
    if glob.glob('/dev/ttyACM*'): break
    time.sleep(0.025)
time.sleep(0.3)

# Read boot output
ports = sorted(glob.glob('/dev/ttyACM*'))
s = serial.Serial(ports[0], 115200, timeout=2)
idle = 0
while True:
    c = s.read(4096)
    if c: output += c; idle = 0
    else:
        idle += 1
        if idle > 5 and len(output) > 500: break
s.close()
print(output.decode('utf-8', errors='replace'))
```

The key insight: the USB Serial/JTAG disconnects on reset. Detect the disconnect via the `SerialException`, then poll for reconnection.

## Multi-Board Setup (Board A + Board B)

The `espnow_sender.c` firmware contains a hardcoded `PEER_MAC` pointing to Board A's Wi-Fi
STA MAC. This MAC is a point-in-time constant — it must match the actual Board A in use.

**Step 1: Read Board A's MAC.** Boot Board A and read the boot log:
```
BASE MAC: b4:3a:45:8a:c8:24
```

**Step 2: Update PEER_MAC in espnow_sender.c** if it does not match:
```c
static const uint8_t PEER_MAC[ESP_NOW_ETH_ALEN] = {
    0xB4, 0x3A, 0x45, 0x8A, 0xC8, 0x24   /* Board A Wi-Fi STA MAC */
};
```

**Current Board A MAC:** `b4:3a:45:8a:c8:24` (verified March 22, 2026)

**Step 3: Build and flash Board B** (swap CMakeLists, comment out ULP block):
```cmake
set(app_sources "espnow_sender.c")
# set(app_sources "geometry_cfc_freerun.c" "reflex_vdb.c")
# (comment out ulp_embed_binary block too)
```
```bash
idf.py build && idf.py -p /dev/ttyACM1 flash
```

**Step 4: Verify Board B is sending successfully.** Board B prints per-pattern send stats.
`ok=N fail=0` means packets are ACKed. `ok=0 fail=N` means the PEER_MAC is wrong or
Board A is not listening on channel 1 (check `[ESPNOW] Listening on channel 1` in Board A log).

**Step 5: Restore CMakeLists** for Board A firmware:
```cmake
# set(app_sources "espnow_sender.c")
set(app_sources "geometry_cfc_freerun.c" "reflex_vdb.c")
# (uncomment ulp_embed_binary block)
```
```bash
idf.py build && idf.py -p /dev/ttyACM0 flash
```

## Troubleshooting

**"No port found after reconnect"** — USB re-enumeration can take up to 2 seconds. Increase the poll count.

**Flash fails with "Failed to connect"** — Board is not in download mode. Repeat the BOOT+RESET sequence.

**`idf.py monitor` loses connection on reset** — Expected behavior. Use the Python script above instead.

**LEDC channel config hangs** — Don't use GPIO 10-12, they conflict with SPI flash on QFN32 module. Use GPIO 0-2.

**PARLIO transmit hangs** — Bare-metal GDMA setup during init corrupts PARLIO's DMA channel. See `HARDWARE_ERRATA.md`.

## Console Configuration

The sdkconfig uses USB Serial/JTAG for console:
```
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```
