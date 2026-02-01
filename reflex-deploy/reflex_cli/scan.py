"""
Device scanning and discovery.
"""

import socket
from datetime import datetime
from typing import List, Optional

from .util import (
    DeviceInfo,
    find_esp32_devices,
    open_serial,
    reset_device,
    read_until_timeout,
    get_chip_info,
    DeviceManifest,
)


def scan_devices(verbose: bool = False) -> List[DeviceInfo]:
    """
    Scan for all connected ESP32 devices.
    Returns list of DeviceInfo with basic USB info.
    """
    return find_esp32_devices()


def probe_device(device: DeviceInfo, verbose: bool = False) -> DeviceInfo:
    """
    Probe a device for detailed information.
    Updates DeviceInfo with chip_type, chip_id, flash_size, etc.
    """
    # Get chip info via esptool
    chip_info = get_chip_info(device.port)
    
    if chip_info:
        device.chip_type = chip_info.chip_type
        device.chip_id = chip_info.chip_id
        device.flash_size = chip_info.flash_size
        device.mac_address = chip_info.mac_address
    
    # Try to get firmware version from running firmware
    try:
        ser = open_serial(device.port)
        reset_device(ser)
        
        # Wait for boot output
        import time
        time.sleep(2)
        
        output = read_until_timeout(ser, timeout=1)
        text = output.decode('utf-8', errors='ignore')
        
        # Look for version string in boot output
        for line in text.split('\n'):
            if 'REFLEX' in line.upper() or 'VERSION' in line.upper():
                device.firmware_version = line.strip()[:50]
                break
        
        ser.close()
    except Exception:
        pass
    
    return device


def scan_and_probe(verbose: bool = False) -> List[DeviceInfo]:
    """
    Scan for devices and probe each one for details.
    """
    devices = scan_devices(verbose)
    
    probed = []
    for device in devices:
        try:
            probed.append(probe_device(device, verbose))
        except Exception as e:
            if verbose:
                print(f"  Warning: Could not probe {device.port}: {e}")
            probed.append(device)
    
    return probed


def generate_manifest(devices: List[DeviceInfo]) -> DeviceManifest:
    """Generate a device manifest from scanned devices."""
    return DeviceManifest(
        timestamp=datetime.now().isoformat(),
        host=socket.gethostname(),
        devices=[d.to_dict() for d in devices],
    )


def format_device_table(devices: List[DeviceInfo]) -> str:
    """Format devices as a markdown table."""
    if not devices:
        return "No devices found."
    
    lines = [
        "| # | Port | Chip | ID | Flash | MAC |",
        "|---|------|------|----|-------|-----|",
    ]
    
    for i, d in enumerate(devices, 1):
        chip = d.chip_type or "unknown"
        chip_id = d.chip_id[:10] if d.chip_id else "unknown"
        flash = f"{d.flash_size // (1024*1024)}MB" if d.flash_size else "unknown"
        mac = d.mac_address or "unknown"
        
        lines.append(f"| {i} | {d.port} | {chip} | {chip_id}... | {flash} | {mac} |")
    
    return "\n".join(lines)
