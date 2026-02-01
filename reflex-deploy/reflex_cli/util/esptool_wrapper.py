"""
Wrapper around esptool for ESP32 flash operations.
"""

import subprocess
import re
from dataclasses import dataclass
from typing import Optional, Tuple
import hashlib
from pathlib import Path


@dataclass
class ChipInfo:
    """Information retrieved from ESP32 chip."""
    chip_type: str
    chip_id: str
    mac_address: str
    flash_size: int
    crystal_freq: str
    features: list


def run_esptool(port: str, *args, timeout: int = 30) -> Tuple[bool, str]:
    """Run esptool command and return (success, output)."""
    cmd = ["python3", "-m", "esptool", "--port", port] + list(args)
    
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout
        )
        output = result.stdout + result.stderr
        return result.returncode == 0, output
    except subprocess.TimeoutExpired:
        return False, "Command timed out"
    except Exception as e:
        return False, str(e)


def get_chip_info(port: str) -> Optional[ChipInfo]:
    """Get chip information using esptool."""
    success, output = run_esptool(port, "chip_id")
    
    if not success:
        return None
    
    # Parse output
    chip_type = None
    chip_id = None
    mac = None
    flash_size = None
    crystal = None
    features = []
    
    for line in output.split("\n"):
        line = line.strip()
        
        # esptool 4.x format: "Chip is ESP32-C6"
        # esptool 5.x format: "Chip type:          ESP32-C6FH4 (QFN32)"
        if "Chip is" in line:
            match = re.search(r"Chip is (\S+)", line)
            if match:
                chip_type = match.group(1)
        elif "Chip type:" in line:
            match = re.search(r"Chip type:\s+(\S+)", line)
            if match:
                chip_type = match.group(1)
        
        elif "Chip ID:" in line:
            match = re.search(r"Chip ID: (0x[0-9a-fA-F]+)", line)
            if match:
                chip_id = match.group(1)
        
        # esptool 4.x: "MAC: aa:bb:cc:dd:ee:ff"
        # esptool 5.x: "MAC:                aa:bb:cc:dd:ee:ff"
        elif "MAC:" in line and "BASE" not in line and "EXT" not in line:
            match = re.search(r"MAC:\s+([0-9a-fA-F:]+)", line)
            if match:
                mac = match.group(1)
        
        # esptool 4.x: "Crystal is 40MHz"
        # esptool 5.x: "Crystal frequency:  40MHz"
        elif "Crystal is" in line:
            match = re.search(r"Crystal is (\d+MHz)", line)
            if match:
                crystal = match.group(1)
        elif "Crystal frequency:" in line:
            match = re.search(r"Crystal frequency:\s+(\d+MHz)", line)
            if match:
                crystal = match.group(1)
        
        elif "Features:" in line:
            match = re.search(r"Features:\s+(.+)", line)
            if match:
                features = [f.strip() for f in match.group(1).split(",")]
    
    # Get flash size separately
    success, output = run_esptool(port, "flash_id")
    if success:
        for line in output.split("\n"):
            if "Detected flash size:" in line:
                match = re.search(r"Detected flash size: (\d+)([KMG]B)", line)
                if match:
                    size = int(match.group(1))
                    unit = match.group(2)
                    if unit == "KB":
                        flash_size = size * 1024
                    elif unit == "MB":
                        flash_size = size * 1024 * 1024
                    elif unit == "GB":
                        flash_size = size * 1024 * 1024 * 1024
    
    if chip_type:
        return ChipInfo(
            chip_type=chip_type,
            chip_id=chip_id or "unknown",
            mac_address=mac or "unknown",
            flash_size=flash_size or 0,
            crystal_freq=crystal or "unknown",
            features=features,
        )
    
    return None


def read_flash(port: str, address: int, size: int, output_path: Path) -> Tuple[bool, str]:
    """Read flash contents to file."""
    return run_esptool(
        port,
        "read_flash",
        hex(address),
        hex(size),
        str(output_path),
        timeout=120
    )


def write_flash(port: str, address: int, firmware_path: Path, 
                verify: bool = True) -> Tuple[bool, str]:
    """Write firmware to flash."""
    args = ["write_flash"]
    if verify:
        args.append("--verify")
    args.extend([hex(address), str(firmware_path)])
    
    return run_esptool(port, *args, timeout=120)


def erase_flash(port: str) -> Tuple[bool, str]:
    """Erase entire flash."""
    return run_esptool(port, "erase_flash", timeout=60)


def erase_region(port: str, address: int, size: int) -> Tuple[bool, str]:
    """Erase specific flash region."""
    return run_esptool(
        port,
        "erase_region",
        hex(address),
        hex(size),
        timeout=60
    )


def verify_flash(port: str, address: int, firmware_path: Path) -> Tuple[bool, str]:
    """Verify flash contents match file."""
    return run_esptool(
        port,
        "verify_flash",
        hex(address),
        str(firmware_path),
        timeout=120
    )


def calculate_checksum(file_path: Path) -> str:
    """Calculate SHA256 checksum of file."""
    sha256 = hashlib.sha256()
    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            sha256.update(chunk)
    return sha256.hexdigest()


def flash_firmware(port: str, 
                   bootloader_path: Optional[Path] = None,
                   partition_table_path: Optional[Path] = None,
                   app_path: Optional[Path] = None,
                   bootloader_addr: int = 0x0,
                   partition_addr: int = 0x8000,
                   app_addr: int = 0x10000) -> Tuple[bool, str, dict]:
    """
    Flash complete firmware set.
    Returns (success, message, checksums).
    """
    checksums = {}
    
    # Flash bootloader
    if bootloader_path and bootloader_path.exists():
        checksums["bootloader"] = calculate_checksum(bootloader_path)
        success, output = write_flash(port, bootloader_addr, bootloader_path)
        if not success:
            return False, f"Bootloader flash failed: {output}", checksums
    
    # Flash partition table
    if partition_table_path and partition_table_path.exists():
        checksums["partition_table"] = calculate_checksum(partition_table_path)
        success, output = write_flash(port, partition_addr, partition_table_path)
        if not success:
            return False, f"Partition table flash failed: {output}", checksums
    
    # Flash application
    if app_path and app_path.exists():
        checksums["application"] = calculate_checksum(app_path)
        success, output = write_flash(port, app_addr, app_path)
        if not success:
            return False, f"Application flash failed: {output}", checksums
    
    return True, "Flash complete", checksums
