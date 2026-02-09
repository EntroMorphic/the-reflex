# Pi4 Development Environment

> Raspberry Pi 4 as OBSBOT controller and ESP32 dev host.

---

## Quick Reference

### SSH to Pi4
```bash
ssh -p 11965 ztflynn@10.64.24.48
```

### Run Stereo Vision Demo
```bash
ssh -p 11965 ztflynn@10.64.24.48 '
  v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=10 &
  v4l2-ctl -d /dev/video2 --stream-mmap --stream-count=10 &
  sleep 2
  cd /tmp/obsbot_test && ./sync_sweep /dev/video0 /dev/video2
'
```

---

## Hardware

### Pi4 (sirius)

| Property | Value |
|----------|-------|
| Hostname | sirius |
| OS | Debian 13 (trixie) |
| Kernel | 6.12.47+rpt-rpi-v8 PREEMPT |
| Arch | aarch64 |
| Python | 3.13.5 |

### Connected Devices

| Device | Port | Notes |
|--------|------|-------|
| OBSBOT Tiny #1 | /dev/video0, /dev/video1 | Right eye (usb-1.2) |
| OBSBOT Tiny #2 | /dev/video2, /dev/video3 | Left eye (usb-1.1) |
| ESP32-C6 | /dev/ttyACM0 | When bound |

---

## Network

| Property | Value |
|----------|-------|
| Pi4 IP | 10.64.24.48 |
| SSH Port | 11965 |

---

## OBSBOT Camera Control

### List Cameras
```bash
v4l2-ctl --list-devices | grep -A2 OBSBOT
```

### Check PTZ Controls
```bash
v4l2-ctl -d /dev/video0 --list-ctrls-menus | grep -E "(pan|tilt|zoom)"
```

### Direct PTZ Control
```bash
# Pan right 30°
v4l2-ctl -d /dev/video0 --set-ctrl=pan_absolute=108000

# Tilt up 20°
v4l2-ctl -d /dev/video0 --set-ctrl=tilt_absolute=72000

# Center
v4l2-ctl -d /dev/video0 --set-ctrl=pan_absolute=0,tilt_absolute=0
```

### Wake Camera from USB Suspend
```bash
v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=5
```

---

## Reflex Tools

### Build Tools
```bash
cd /tmp/obsbot_test
gcc -O2 -o obsbot_test obsbot_test.c
gcc -O2 -o stereo_demo stereo_demo.c -lpthread
gcc -O2 -o sync_sweep sync_sweep.c -lpthread
```

### Test Single Camera
```bash
./obsbot_test /dev/video0 --sweep
./obsbot_test /dev/video0 --latency
```

### Test Stereo Vision
```bash
# Wake both cameras first
v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=10 &
v4l2-ctl -d /dev/video2 --stream-mmap --stream-count=10 &
sleep 2

# Run synchronized sweep
./sync_sweep /dev/video0 /dev/video2
```

---

## ESP32-C6 Development

### Activate ESP Environment
```bash
source ~/esp-env/bin/activate
```

### Identify Chip
```bash
esptool.py --port /dev/ttyACM0 chip_id
```

### Flash Firmware
```bash
esptool.py --port /dev/ttyACM0 write_flash 0x0 firmware.bin
```

### Monitor Serial
```bash
python3 -m serial.tools.miniterm /dev/ttyACM0 115200
```

### Power Down C6 (Unbind USB)
```bash
echo '1-1.1' | sudo tee /sys/bus/usb/drivers/usb/unbind
```

### Power Up C6 (Rebind USB)
```bash
echo '1-1.1' | sudo tee /sys/bus/usb/drivers/usb/bind
```

---

## Syncing Code

### Push from Workstation
```bash
rsync -avz -e 'ssh -p 11965' \
  /home/ztflynn/001/the-reflex/reflex-os/tools/ \
  ztflynn@10.64.24.48:/tmp/obsbot_test/
```

### Pull from Pi4
```bash
rsync -avz -e 'ssh -p 11965' \
  ztflynn@10.64.24.48:/tmp/obsbot_test/ \
  /home/ztflynn/001/the-reflex/reflex-os/tools/
```

---

## Benchmarks

| Operation | Latency |
|-----------|---------|
| Stigmergy (reflex) | 167 ns |
| OBSBOT PTZ command | 121 µs |
| Stereo sync sweep | Working |

---

*"The Cathedral's eyes live here."*
