"""
Firmware update functionality.

PLACEHOLDER: OTA server infrastructure not yet deployed.
This module provides the interface for when OTA is available.
"""

from typing import Tuple, Optional
from pathlib import Path


class OTANotImplementedError(NotImplementedError):
    """Raised when OTA functionality is not yet available."""
    pass


def check_for_updates(port: str, server_url: str = "") -> Tuple[bool, str]:
    """
    Check if firmware updates are available.
    
    PLACEHOLDER: Requires OTA server infrastructure.
    """
    raise OTANotImplementedError(
        "OTA update server not yet deployed.\n"
        "For now, use 'reflex-cli install' to flash firmware directly via USB."
    )


def download_update(version: str, output_path: Path, 
                    server_url: str = "") -> Tuple[bool, str]:
    """
    Download firmware update from server.
    
    PLACEHOLDER: Requires OTA server infrastructure.
    """
    raise OTANotImplementedError(
        "OTA update server not yet deployed.\n"
        "Firmware must be obtained manually and installed via USB."
    )


def apply_update(port: str, firmware_path: Path, 
                 rollback_on_failure: bool = True) -> Tuple[bool, str]:
    """
    Apply firmware update to device.
    
    PLACEHOLDER: For now, redirects to USB installation.
    
    Future implementation will:
    1. Upload to OTA-B partition
    2. Verify checksum
    3. Set boot flag
    4. Reboot
    5. Verify successful boot
    6. Rollback if failed
    """
    raise OTANotImplementedError(
        "OTA updates not yet implemented.\n"
        "Use 'reflex-cli install' for USB-based firmware installation.\n"
        "\n"
        "Future OTA process will be:\n"
        "  1. Upload firmware to device OTA-B partition\n"
        "  2. Verify checksum\n"
        "  3. Set boot flag to OTA-B\n"
        "  4. Reboot and verify\n"
        "  5. Auto-rollback to OTA-A on failure"
    )


def rollback(port: str) -> Tuple[bool, str]:
    """
    Rollback to previous firmware version.
    
    PLACEHOLDER: Requires OTA partition scheme.
    """
    raise OTANotImplementedError(
        "Rollback requires OTA partition scheme.\n"
        "Current firmware uses single-app partition.\n"
        "\n"
        "To recover a device:\n"
        "  reflex-cli recover /dev/ttyACM0"
    )


def get_update_status() -> str:
    """Return status of OTA infrastructure."""
    return """
OTA Update Status: NOT DEPLOYED

Current capabilities:
  ✅ USB-based firmware installation (reflex-cli install)
  ✅ USB-based device recovery (reflex-cli recover)
  ❌ Over-the-air updates
  ❌ Remote firmware distribution
  ❌ Automatic rollback

Planned infrastructure:
  - OTA server for firmware distribution
  - Signed firmware packages
  - A/B partition scheme for safe updates
  - Automatic rollback on boot failure
  - Fleet-wide update management

For now, use USB installation:
  reflex-cli install /dev/ttyACM0 --config site.yaml
"""
