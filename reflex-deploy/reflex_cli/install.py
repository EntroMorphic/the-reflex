"""
Firmware installation.
"""

import yaml
import time
from pathlib import Path
from datetime import datetime
from typing import Optional, Dict, Any

from .util import (
    get_chip_info,
    erase_flash,
    flash_firmware,
    calculate_checksum,
    open_serial,
    reset_device,
    read_until_timeout,
    read_flash,
    InstallManifest,
    AuditLog,
)


def load_config(config_path: Path) -> Dict[str, Any]:
    """Load site configuration from YAML."""
    with open(config_path) as f:
        return yaml.safe_load(f)


def backup_firmware(port: str, output_dir: Path) -> Optional[Path]:
    """
    Backup existing firmware before installation.
    Returns path to backup file.
    """
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    chip_info = get_chip_info(port)
    
    if not chip_info or not chip_info.flash_size:
        return None
    
    backup_path = output_dir / f"backup_{chip_info.chip_id}_{timestamp}.bin"
    
    success, message = read_flash(
        port, 
        address=0x0, 
        size=chip_info.flash_size, 
        output_path=backup_path
    )
    
    if success:
        return backup_path
    return None


def verify_installation(port: str, timeout: float = 5.0) -> Dict[str, Any]:
    """Verify device boots after installation."""
    try:
        ser = open_serial(port)
        reset_device(ser)
        time.sleep(timeout)
        
        output = read_until_timeout(ser, timeout=2)
        text = output.decode('utf-8', errors='ignore')
        ser.close()
        
        # Check for successful boot
        if "reflex" in text.lower() or "spine" in text.lower():
            return {"success": True, "message": "Firmware booted successfully"}
        elif "boot:" in text.lower():
            return {"success": True, "message": "Boot detected, app starting"}
        else:
            return {"success": False, "message": "No boot output detected"}
    except Exception as e:
        return {"success": False, "message": str(e)}


def install_firmware(
    port: str,
    config_path: Path,
    firmware_dir: Path,
    backup: bool = True,
    backup_dir: Optional[Path] = None,
    audit_log: Optional[AuditLog] = None,
    user: str = "unknown"
) -> InstallManifest:
    """
    Install firmware on a device.
    
    Args:
        port: Serial port
        config_path: Path to site config YAML
        firmware_dir: Directory containing firmware binaries
        backup: Whether to backup existing firmware
        backup_dir: Where to store backups
        audit_log: Audit log for compliance
        user: User performing installation
    
    Returns:
        InstallManifest with installation details
    """
    # Get chip info
    chip_info = get_chip_info(port)
    if not chip_info:
        manifest = InstallManifest(
            timestamp=datetime.now().isoformat(),
            device_port=port,
            device_chip_id="unknown",
            firmware_version="unknown",
            config_path=str(config_path),
            status="failed",
            error="Could not read chip info",
        )
        return manifest
    
    # Load config
    try:
        config = load_config(config_path)
    except Exception as e:
        manifest = InstallManifest(
            timestamp=datetime.now().isoformat(),
            device_port=port,
            device_chip_id=chip_info.chip_id,
            firmware_version="unknown",
            config_path=str(config_path),
            status="failed",
            error=f"Could not load config: {e}",
        )
        return manifest
    
    # Initialize manifest
    firmware_version = config.get("firmware", {}).get("version", "unknown")
    manifest = InstallManifest(
        timestamp=datetime.now().isoformat(),
        device_port=port,
        device_chip_id=chip_info.chip_id,
        firmware_version=firmware_version,
        config_path=str(config_path),
    )
    
    # Log start
    if audit_log:
        audit_log.log(
            action="install_start",
            device=chip_info.chip_id,
            user=user,
            details={"port": port, "firmware": firmware_version},
        )
    
    # Step 1: Backup existing firmware
    if backup:
        manifest.add_step("Backup", "running")
        backup_path = backup_firmware(port, backup_dir or Path("."))
        if backup_path:
            manifest.checksums["backup"] = calculate_checksum(backup_path)
            manifest.add_step("Backup", "passed", {"path": str(backup_path)})
        else:
            manifest.add_step("Backup", "skipped", {"reason": "Could not read flash"})
    
    # Step 2: Verify config
    manifest.add_step("Config Validation", "running")
    required_keys = ["device", "reflex"]
    missing = [k for k in required_keys if k not in config]
    if missing:
        manifest.add_step("Config Validation", "failed", {"missing": missing})
        manifest.status = "failed"
        manifest.error = f"Missing config keys: {missing}"
        return manifest
    manifest.add_step("Config Validation", "passed")
    
    # Step 3: Locate firmware files
    manifest.add_step("Locate Firmware", "running")
    
    bootloader_path = firmware_dir / "bootloader.bin"
    partition_path = firmware_dir / "partition-table.bin"
    app_path = firmware_dir / "reflex_os.bin"
    
    missing_files = []
    if not bootloader_path.exists():
        missing_files.append("bootloader.bin")
    if not partition_path.exists():
        missing_files.append("partition-table.bin")
    if not app_path.exists():
        missing_files.append("reflex_os.bin")
    
    if missing_files:
        manifest.add_step("Locate Firmware", "failed", {"missing": missing_files})
        manifest.status = "failed"
        manifest.error = f"Missing firmware files: {missing_files}"
        return manifest
    
    manifest.add_step("Locate Firmware", "passed", {
        "bootloader": str(bootloader_path),
        "partition": str(partition_path),
        "app": str(app_path),
    })
    
    # Step 4: Erase flash
    manifest.add_step("Erase Flash", "running")
    success, output = erase_flash(port)
    if not success:
        manifest.add_step("Erase Flash", "failed", {"output": output})
        manifest.status = "failed"
        manifest.error = "Flash erase failed"
        return manifest
    manifest.add_step("Erase Flash", "passed")
    
    # Step 5: Flash firmware
    manifest.add_step("Flash Firmware", "running")
    success, message, checksums = flash_firmware(
        port,
        bootloader_path=bootloader_path,
        partition_table_path=partition_path,
        app_path=app_path,
    )
    
    manifest.checksums.update(checksums)
    
    if not success:
        manifest.add_step("Flash Firmware", "failed", {"message": message})
        manifest.status = "failed"
        manifest.error = message
        return manifest
    manifest.add_step("Flash Firmware", "passed", {"checksums": checksums})
    
    # Step 6: Verify boot
    manifest.add_step("Verify Boot", "running")
    result = verify_installation(port)
    if result["success"]:
        manifest.add_step("Verify Boot", "passed", {"message": result["message"]})
    else:
        manifest.add_step("Verify Boot", "failed", {"message": result["message"]})
        manifest.status = "failed"
        manifest.error = result["message"]
        return manifest
    
    # Success
    manifest.status = "success"
    
    # Log completion
    if audit_log:
        audit_log.log(
            action="install_complete",
            device=chip_info.chip_id,
            user=user,
            details={
                "port": port,
                "firmware": firmware_version,
                "checksums": checksums,
            },
            status="success",
        )
    
    return manifest
