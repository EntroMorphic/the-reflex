"""Utility modules for reflex-cli."""

from .serial import (
    DeviceInfo,
    find_esp32_devices,
    open_serial,
    reset_device,
    read_until_timeout,
    send_command,
    enter_bootloader,
    force_download_mode,
)

from .manifest import (
    DeviceManifest,
    InstallManifest,
    ValidationReport,
    AuditLog,
)

from .esptool_wrapper import (
    ChipInfo,
    get_chip_info,
    read_flash,
    write_flash,
    erase_flash,
    flash_firmware,
    calculate_checksum,
)

__all__ = [
    "DeviceInfo",
    "find_esp32_devices",
    "open_serial",
    "reset_device",
    "read_until_timeout",
    "send_command",
    "enter_bootloader",
    "force_download_mode",
    "DeviceManifest",
    "InstallManifest",
    "ValidationReport",
    "AuditLog",
    "ChipInfo",
    "get_chip_info",
    "read_flash",
    "write_flash",
    "erase_flash",
    "flash_firmware",
    "calculate_checksum",
]
