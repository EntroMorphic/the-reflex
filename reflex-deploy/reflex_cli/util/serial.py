"""
Serial communication utilities for ESP32 devices.
"""

import serial
import serial.tools.list_ports
import time
from dataclasses import dataclass
from typing import Optional, List
import json


@dataclass
class DeviceInfo:
    """Information about a discovered device."""
    port: str
    vid: int
    pid: int
    serial_number: Optional[str]
    manufacturer: Optional[str]
    product: Optional[str]
    chip_type: Optional[str] = None
    chip_id: Optional[str] = None
    flash_size: Optional[int] = None
    mac_address: Optional[str] = None
    firmware_version: Optional[str] = None
    
    def to_dict(self) -> dict:
        return {
            "port": self.port,
            "vid": hex(self.vid) if self.vid else None,
            "pid": hex(self.pid) if self.pid else None,
            "serial_number": self.serial_number,
            "manufacturer": self.manufacturer,
            "product": self.product,
            "chip_type": self.chip_type,
            "chip_id": self.chip_id,
            "flash_size": self.flash_size,
            "mac_address": self.mac_address,
            "firmware_version": self.firmware_version,
        }
    
    def to_json(self) -> str:
        return json.dumps(self.to_dict(), indent=2)


# Known ESP32 USB identifiers
ESP32_IDENTIFIERS = {
    (0x303a, 0x1001): "ESP32-C6/C3 (USB JTAG/serial)",
    (0x303a, 0x1002): "ESP32-S2",
    (0x303a, 0x1003): "ESP32-S3",
    (0x10c4, 0xea60): "CP210x (ESP32 DevKit)",
    (0x1a86, 0x7523): "CH340 (ESP32 Clone)",
    (0x0403, 0x6001): "FTDI (ESP32 DevKit)",
}


def list_serial_ports() -> List:
    """List all available serial ports."""
    return list(serial.tools.list_ports.comports())


def find_esp32_devices() -> List[DeviceInfo]:
    """
    Find all connected ESP32 devices.
    Filters by known VID/PID combinations.
    """
    devices = []
    
    for port in list_serial_ports():
        vid_pid = (port.vid, port.pid)
        
        if vid_pid in ESP32_IDENTIFIERS:
            device = DeviceInfo(
                port=port.device,
                vid=port.vid,
                pid=port.pid,
                serial_number=port.serial_number,
                manufacturer=port.manufacturer,
                product=port.product,
            )
            devices.append(device)
    
    return devices


def open_serial(port: str, baudrate: int = 115200, timeout: float = 1.0) -> serial.Serial:
    """Open a serial connection to the device."""
    return serial.Serial(port, baudrate, timeout=timeout)


def reset_device(ser: serial.Serial, delay: float = 0.1) -> None:
    """Reset device via DTR toggle."""
    ser.dtr = False
    time.sleep(delay)
    ser.dtr = True
    time.sleep(delay)


def read_until_timeout(ser: serial.Serial, timeout: float = 2.0) -> bytes:
    """Read all available data until timeout."""
    ser.timeout = timeout
    data = b""
    start = time.time()
    
    while time.time() - start < timeout:
        chunk = ser.read(1024)
        if chunk:
            data += chunk
        else:
            break
    
    return data


def send_command(ser: serial.Serial, cmd: str, timeout: float = 1.0) -> str:
    """Send a command and read response."""
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode())
    time.sleep(0.1)
    
    response = read_until_timeout(ser, timeout)
    return response.decode('utf-8', errors='ignore')


def enter_bootloader(port: str) -> bool:
    """
    Attempt to enter ESP32 bootloader mode.
    Returns True if successful.
    """
    try:
        ser = serial.Serial(port, 115200)
        
        # ESP32 bootloader entry: Hold BOOT, pulse EN
        ser.dtr = False  # EN high
        ser.rts = True   # BOOT low (GPIO0)
        time.sleep(0.1)
        ser.dtr = True   # EN low (reset)
        time.sleep(0.1)
        ser.dtr = False  # EN high (run)
        time.sleep(0.5)
        ser.rts = False  # Release BOOT
        
        ser.close()
        return True
    except Exception:
        return False


def force_download_mode(port: str) -> bool:
    """
    Force device into download mode for recovery.
    Requires manual intervention if automatic method fails.
    """
    # Try automatic method first
    if enter_bootloader(port):
        return True
    
    # Return False - manual intervention needed
    return False
