# ESP32-C6 Flash & Serial Guide

Board: ESP32-C6FH4 (QFN32) revision v0.2, built-in USB Serial/JTAG (VID 0x303a, PID 0x1001).

## Prerequisites

```bash
source ~/esp/v5.4/export.sh
```

## Selecting the Active Firmware

Edit `reflex-os/main/CMakeLists.txt` — the `SRCS` line controls which `.c` file compiles:

```cmake
idf_component_register(SRCS "raid_etm_fabric.c"   # <-- change this
                       INCLUDE_DIRS "../include")
```

## Build

```bash
cd reflex-os
idf.py build
```

Output binaries land in `build/`:
- `build/bootloader/bootloader.bin`
- `build/reflex_os.bin`
- `build/partition_table/partition-table.bin`

## Flash

### 1. Enter Download Mode

Hold **BOOT** button, press **RESET**, release **BOOT**. The device enters download mode on `/dev/ttyACM0`.

### 2. Flash

```bash
esptool.py --chip esp32c6 -p /dev/ttyACM0 -b 460800 \
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
s = serial.Serial('/dev/ttyACM0', 115200, timeout=0.1)
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
