"""
Device recovery tools.
"""

import time
from typing import Tuple

from .util import (
    open_serial,
    force_download_mode,
    get_chip_info,
    erase_flash,
    flash_firmware,
)
from pathlib import Path


def attempt_recovery(port: str, verbose: bool = False) -> Tuple[bool, str]:
    """
    Attempt to recover a bricked/unresponsive device.
    
    Recovery steps:
    1. Try to enter download mode
    2. Check if chip responds to esptool
    3. Erase flash if accessible
    4. Flash safe-mode firmware if available
    """
    steps = []
    
    # Step 1: Force download mode
    if verbose:
        print(f"Step 1: Forcing download mode on {port}...")
    
    if force_download_mode(port):
        steps.append("Download mode: OK")
    else:
        steps.append("Download mode: MANUAL INTERVENTION REQUIRED")
        steps.append("  - Hold BOOT button")
        steps.append("  - Press and release EN/RST button")
        steps.append("  - Release BOOT button")
        steps.append("  - Retry this command")
        return False, "\n".join(steps)
    
    # Step 2: Check chip response
    if verbose:
        print("Step 2: Checking chip response...")
    
    time.sleep(1)
    chip_info = get_chip_info(port)
    
    if chip_info:
        steps.append(f"Chip detected: {chip_info.chip_type} ({chip_info.chip_id})")
    else:
        steps.append("Chip not responding to esptool")
        steps.append("  - Check USB cable")
        steps.append("  - Try different USB port")
        steps.append("  - Power cycle the device")
        return False, "\n".join(steps)
    
    # Step 3: Erase flash
    if verbose:
        print("Step 3: Erasing flash...")
    
    success, output = erase_flash(port)
    
    if success:
        steps.append("Flash erased successfully")
    else:
        steps.append(f"Flash erase failed: {output}")
        return False, "\n".join(steps)
    
    # Step 4: Check for safe-mode firmware
    safe_mode_paths = [
        Path("firmware/safe_mode/reflex_os.bin"),
        Path("safe_mode.bin"),
        Path("/usr/share/reflex/safe_mode.bin"),
    ]
    
    safe_mode_path = None
    for path in safe_mode_paths:
        if path.exists():
            safe_mode_path = path
            break
    
    if safe_mode_path:
        if verbose:
            print(f"Step 4: Flashing safe-mode firmware from {safe_mode_path}...")
        
        success, message, checksums = flash_firmware(
            port,
            app_path=safe_mode_path,
        )
        
        if success:
            steps.append(f"Safe-mode firmware flashed: {safe_mode_path}")
            steps.append("Device should now boot to safe mode")
        else:
            steps.append(f"Safe-mode flash failed: {message}")
            steps.append("Device is erased but has no firmware")
    else:
        steps.append("No safe-mode firmware found")
        steps.append("Device is erased - reflash with full firmware")
    
    return True, "\n".join(steps)


def check_device_health(port: str) -> Tuple[str, str]:
    """
    Quick health check on a device.
    Returns (status, message).
    
    Status can be:
    - "healthy": Device is responding normally
    - "degraded": Device responds but with issues
    - "unresponsive": Device not responding
    - "bricked": Device in unknown/failed state
    """
    try:
        # Try to open serial
        ser = open_serial(port, timeout=2)
    except Exception as e:
        return "unresponsive", f"Cannot open port: {e}"
    
    try:
        # Toggle DTR to reset
        ser.dtr = False
        time.sleep(0.1)
        ser.dtr = True
        time.sleep(2)
        
        # Check for output
        output = ser.read(1000)
        text = output.decode('utf-8', errors='ignore')
        
        ser.close()
        
        if len(output) == 0:
            return "unresponsive", "No output from device"
        
        # Check for signs of life
        if "reflex" in text.lower() or "spine" in text.lower():
            return "healthy", "Reflex firmware running"
        elif "boot:" in text.lower():
            if "error" in text.lower() or "fail" in text.lower():
                return "degraded", "Boot errors detected"
            return "healthy", "Device booting"
        elif "rst:" in text.lower():
            return "degraded", "Device resetting repeatedly"
        else:
            return "degraded", "Unknown output pattern"
    
    except Exception as e:
        try:
            ser.close()
        except:
            pass
        return "bricked", f"Communication error: {e}"


def force_safe_mode(port: str) -> Tuple[bool, str]:
    """
    Attempt to force device into safe mode.
    
    This sends a special command sequence that the firmware
    should recognize and enter safe mode.
    """
    try:
        ser = open_serial(port)
        
        # Send safe mode command (firmware must support this)
        # Standard sequence: send "SAFE" within 2 seconds of boot
        ser.dtr = False
        time.sleep(0.1)
        ser.dtr = True
        time.sleep(0.5)
        
        # Send command multiple times to catch boot window
        for _ in range(10):
            ser.write(b"SAFE\n")
            time.sleep(0.2)
        
        time.sleep(2)
        output = ser.read(1000)
        text = output.decode('utf-8', errors='ignore')
        
        ser.close()
        
        if "safe mode" in text.lower():
            return True, "Device entered safe mode"
        else:
            return False, "Safe mode command not recognized (firmware may not support)"
    
    except Exception as e:
        return False, f"Error: {e}"
