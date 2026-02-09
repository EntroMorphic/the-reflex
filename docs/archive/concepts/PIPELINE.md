# The Reflex Development Pipeline

> Workstation → Pi4 → ESP32-C6 → Rerun visualization
>
> Last verified: 2026-01-26

---

## Topology

```
┌─────────────────────────────────────────────────────────────┐
│ WORKSTATION (development machine)                           │
│ IP: 10.64.24.9                                              │
│                                                             │
│ • Claude Code / IDE                                         │
│ • Rerun viewer (DISPLAY=:1, port 9877)                     │
│ • rsync to Pi4                                              │
└─────────────────────────────────────────────────────────────┘
         │
         │ SSH (port 11965)
         │ Rerun gRPC (port 9877)
         ▼
┌─────────────────────────────────────────────────────────────┐
│ Pi4 (sirius)                                                │
│ IP: 10.64.24.48                                             │
│ SSH: ssh -p 11965 ztflynn@10.64.24.48                       │
│                                                             │
│ • ESP-IDF environment (~/esp/esp-idf)                       │
│ • Reflex firmware source (~/reflex-os)                      │
│ • Rerun receiver scripts (~/reflex-tools)                   │
│ • Python venv with rerun-sdk (~/reflex-tools/venv)          │
└─────────────────────────────────────────────────────────────┘
         │
         │ USB Serial (/dev/ttyACM0, 115200 baud)
         ▼
┌─────────────────────────────────────────────────────────────┐
│ ESP32-C6 (the body)                                         │
│ Chip: ESP32-C6FH4 (QFN32) rev v0.2                          │
│ MAC: b4:3a:45:8a:c4:d4                                      │
│                                                             │
│ • Single HP Core + LP Core (asymmetric RISC-V)              │
│ • 160MHz, 320KB SRAM                                        │
│ • Wi-Fi 6, BT 5, IEEE802.15.4                               │
│ • The Reflex runs here                                      │
└─────────────────────────────────────────────────────────────┘
```

---

## Quick Reference

### 1. Start Rerun Viewer (Workstation)

```bash
DISPLAY=:1 rerun --bind 0.0.0.0 --port 9877 &
```

### 2. Stream C6 Output to Workstation

```bash
ssh -p 11965 ztflynn@10.64.24.48 'source ~/reflex-tools/venv/bin/activate && \
  python3 ~/reflex-tools/rerun_receiver_remote.py /dev/ttyACM0 10.64.24.9 9877'
```

### 3. SSH to Pi4

```bash
ssh -p 11965 ztflynn@10.64.24.48
```

### 4. Identify C6 Chip

```bash
ssh -p 11965 ztflynn@10.64.24.48 'source ~/esp-env/bin/activate && \
  esptool chip-id --port /dev/ttyACM0'
```

### 5. Build and Flash Firmware

```bash
# On Pi4
source ~/esp-env/bin/activate
cd ~/reflex-os
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### 6. Monitor Serial Output (without Rerun)

```bash
ssh -p 11965 ztflynn@10.64.24.48 'source ~/esp-env/bin/activate && \
  python3 -m serial.tools.miniterm /dev/ttyACM0 115200'
```

### 7. Sync Code to Pi4

```bash
# Push reflex-os from workstation to Pi4
rsync -avz -e 'ssh -p 11965' \
  /home/ztflynn/001/the-reflex/reflex-os/ \
  ztflynn@10.64.24.48:~/reflex-os/
```

---

## Network Details

| Property | Value |
|----------|-------|
| Workstation IP | 10.64.24.9 |
| Pi4 IP | 10.64.24.48 |
| SSH Port | 11965 |
| Rerun Port | 9877 |
| Serial Baud | 115200 |
| Latency (Pi4 ↔ WS) | ~1.4ms |

---

## File Locations

### Workstation

| Path | Contents |
|------|----------|
| `/home/ztflynn/001/the-reflex/` | Main repository |
| `/home/ztflynn/001/the-reflex/reflex-os/` | ESP32-C6 firmware source |
| `/home/ztflynn/001/the-reflex/docs/` | Documentation |

### Pi4

| Path | Contents |
|------|----------|
| `~/reflex-os/` | Firmware source (synced from workstation) |
| `~/reflex-tools/` | Rerun receiver scripts, .rrd recordings |
| `~/reflex-tools/venv/` | Python venv with rerun-sdk, pyserial |
| `~/esp/esp-idf/` | ESP-IDF framework |
| `~/esp-env/` | Python venv for esptool |

---

## Streaming Protocol

The C6 sends 48-byte binary packets at ~10Hz:

```c
// Packet structure (reflex_stream.h)
struct reflex_packet {
    uint8_t  magic;           // 0x52 ('R')
    uint8_t  version;         // Protocol version
    uint16_t tick;            // Sequence number
    uint8_t  slow_scores[8];  // Slow layer scores
    uint8_t  med_scores[8];   // Medium layer scores
    uint8_t  fast_scores[8];  // Fast layer scores
    uint8_t  chosen_output;   // Selected GPIO
    uint8_t  chosen_state;    // Output state
    uint8_t  agreement;       // Layer agreement (0-255)
    uint8_t  disagreement;    // Layer disagreement
    int16_t  adc_deltas[4];   // ADC readings
    uint8_t  inputs[8];       // Input GPIO states
};
```

The `rerun_receiver_remote.py` script:
1. Reads serial from C6
2. Parses packets (syncs on magic byte 0x52)
3. Logs to Rerun via gRPC
4. Saves to local .rrd file as backup

---

## Verification Checklist

Run after setup or when debugging connectivity:

```bash
# 1. Pi4 reachable
ssh -p 11965 ztflynn@10.64.24.48 'echo "Pi4 OK"'

# 2. C6 connected
ssh -p 11965 ztflynn@10.64.24.48 'ls /dev/ttyACM0 && echo "C6 OK"'

# 3. C6 identified
ssh -p 11965 ztflynn@10.64.24.48 'source ~/esp-env/bin/activate && \
  esptool chip-id --port /dev/ttyACM0 2>&1 | grep ESP32-C6'

# 4. C6 streaming data
ssh -p 11965 ztflynn@10.64.24.48 'timeout 2 cat /dev/ttyACM0 | od -A x -t x1 | head -5'

# 5. Rerun viewer running (workstation)
ps aux | grep -E "[r]erun.*9877"

# 6. Full pipeline test (10 seconds)
ssh -p 11965 ztflynn@10.64.24.48 'source ~/reflex-tools/venv/bin/activate && \
  timeout 10 python3 ~/reflex-tools/rerun_receiver_remote.py /dev/ttyACM0 10.64.24.9 9877'
```

---

## Power Cycling the C6

```bash
# Unbind USB (power off C6)
ssh -p 11965 ztflynn@10.64.24.48 'echo "1-1.1" | sudo tee /sys/bus/usb/drivers/usb/unbind'

# Rebind USB (power on C6)
ssh -p 11965 ztflynn@10.64.24.48 'echo "1-1.1" | sudo tee /sys/bus/usb/drivers/usb/bind'
```

---

## Thor (Optional - for echip/entropy field)

Thor is a separate system for higher-level processing:

```bash
# SSH to Thor
ssh -p 11965 ztflynn@10.42.0.2

# Execute in container
ssh -p 11965 ztflynn@10.42.0.2 'docker exec entromorphic-dev <command>'
```

See `trixV/zor/docs/THOR_PERSISTENT_SETUP.md` for full Thor documentation.

---

## Troubleshooting

### C6 not found at /dev/ttyACM0

```bash
# Check USB devices
ssh -p 11965 ztflynn@10.64.24.48 'lsusb | grep -i espressif'

# Power cycle
ssh -p 11965 ztflynn@10.64.24.48 'echo "1-1.1" | sudo tee /sys/bus/usb/drivers/usb/unbind'
sleep 2
ssh -p 11965 ztflynn@10.64.24.48 'echo "1-1.1" | sudo tee /sys/bus/usb/drivers/usb/bind'
```

### Rerun not receiving data

1. Check viewer is running: `ps aux | grep rerun`
2. Check port is open: `sudo ufw status | grep 9877`
3. Test connectivity: `ssh -p 11965 ztflynn@10.64.24.48 'ping -c 2 10.64.24.9'`

### Serial permission denied

```bash
ssh -p 11965 ztflynn@10.64.24.48 'sudo usermod -a -G dialout ztflynn'
# Then logout and back in
```

### Build fails on Pi4

```bash
# Ensure ESP-IDF is sourced
ssh -p 11965 ztflynn@10.64.24.48 'source ~/esp/esp-idf/export.sh && cd ~/reflex-os && idf.py build'
```

---

## Related Documents

| Document | Purpose |
|----------|---------|
| [PI4_SETUP.md](./PI4_SETUP.md) | Pi4 hardware details, OBSBOT cameras |
| [PRD_SUBSTRATE_DISCOVERY.md](./PRD_SUBSTRATE_DISCOVERY.md) | Next implementation target |
| [UNDERSTANDING_THE_REFLEX_V2.md](./UNDERSTANDING_THE_REFLEX_V2.md) | What The Reflex is |

---

*"The pipeline is the nervous system. The C6 is the body. The workstation is the mirror."*
