"""
Pre-flight verification of devices.
"""

from dataclasses import dataclass
from typing import List, Tuple, Optional
from datetime import datetime

from .util import (
    DeviceInfo,
    open_serial,
    reset_device,
    read_until_timeout,
    send_command,
    get_chip_info,
    enter_bootloader,
    ChipInfo,
)


@dataclass
class VerificationResult:
    """Result of a single verification check."""
    name: str
    passed: bool
    message: str
    details: Optional[dict] = None


@dataclass
class VerificationReport:
    """Complete verification report for a device."""
    port: str
    timestamp: str
    checks: List[VerificationResult]
    chip_info: Optional[ChipInfo]
    overall_status: str = "pending"
    
    def to_markdown(self) -> str:
        passed = sum(1 for c in self.checks if c.passed)
        failed = sum(1 for c in self.checks if not c.passed)
        
        lines = [
            f"# Pre-Flight Verification Report",
            f"",
            f"**Device:** {self.port}",
            f"**Date:** {self.timestamp}",
            f"**Status:** {self.overall_status.upper()}",
            f"",
        ]
        
        if self.chip_info:
            lines.extend([
                f"## Chip Information",
                f"",
                f"| Property | Value |",
                f"|----------|-------|",
                f"| Type | {self.chip_info.chip_type} |",
                f"| ID | {self.chip_info.chip_id} |",
                f"| MAC | {self.chip_info.mac_address} |",
                f"| Flash | {self.chip_info.flash_size // 1024}KB |",
                f"| Crystal | {self.chip_info.crystal_freq} |",
                f"",
            ])
        
        lines.extend([
            f"## Verification Checks",
            f"",
            f"| Check | Status | Message |",
            f"|-------|--------|---------|",
        ])
        
        for check in self.checks:
            status = "✅" if check.passed else "❌"
            lines.append(f"| {check.name} | {status} | {check.message} |")
        
        lines.extend([
            f"",
            f"## Summary",
            f"",
            f"- **Passed:** {passed}",
            f"- **Failed:** {failed}",
            f"- **Total:** {len(self.checks)}",
        ])
        
        return "\n".join(lines)


def check_usb_connection(port: str) -> VerificationResult:
    """Check if USB serial connection works."""
    try:
        ser = open_serial(port, timeout=2)
        ser.close()
        return VerificationResult(
            name="USB Connection",
            passed=True,
            message="Serial port accessible"
        )
    except Exception as e:
        return VerificationResult(
            name="USB Connection",
            passed=False,
            message=f"Failed: {e}"
        )


def check_chip_type(port: str, expected: str = "ESP32-C6") -> VerificationResult:
    """Verify chip type matches expected."""
    chip_info = get_chip_info(port)
    
    if not chip_info:
        return VerificationResult(
            name="Chip Type",
            passed=False,
            message="Could not read chip info"
        )
    
    if expected.upper() in chip_info.chip_type.upper():
        return VerificationResult(
            name="Chip Type",
            passed=True,
            message=f"Chip is {chip_info.chip_type}",
            details={"chip_type": chip_info.chip_type}
        )
    else:
        return VerificationResult(
            name="Chip Type",
            passed=False,
            message=f"Expected {expected}, got {chip_info.chip_type}"
        )


def check_flash_size(port: str, min_size_mb: int = 2) -> VerificationResult:
    """Verify flash size is sufficient."""
    chip_info = get_chip_info(port)
    
    if not chip_info or not chip_info.flash_size:
        return VerificationResult(
            name="Flash Size",
            passed=False,
            message="Could not read flash size"
        )
    
    size_mb = chip_info.flash_size // (1024 * 1024)
    
    if size_mb >= min_size_mb:
        return VerificationResult(
            name="Flash Size",
            passed=True,
            message=f"{size_mb}MB (minimum: {min_size_mb}MB)",
            details={"flash_size_bytes": chip_info.flash_size}
        )
    else:
        return VerificationResult(
            name="Flash Size",
            passed=False,
            message=f"{size_mb}MB < {min_size_mb}MB minimum"
        )


def check_bootloader_entry(port: str) -> VerificationResult:
    """Verify device can enter bootloader mode."""
    if enter_bootloader(port):
        return VerificationResult(
            name="Bootloader Entry",
            passed=True,
            message="Can enter download mode"
        )
    else:
        return VerificationResult(
            name="Bootloader Entry",
            passed=False,
            message="Failed to enter download mode"
        )


def check_uart_echo(port: str) -> VerificationResult:
    """Check UART communication works."""
    try:
        ser = open_serial(port)
        reset_device(ser)
        
        # Wait for boot
        import time
        time.sleep(2)
        
        output = read_until_timeout(ser, timeout=2)
        ser.close()
        
        if len(output) > 10:
            return VerificationResult(
                name="UART Communication",
                passed=True,
                message=f"Received {len(output)} bytes",
                details={"bytes_received": len(output)}
            )
        else:
            return VerificationResult(
                name="UART Communication",
                passed=False,
                message="No data received"
            )
    except Exception as e:
        return VerificationResult(
            name="UART Communication",
            passed=False,
            message=f"Error: {e}"
        )


def check_current_firmware(port: str) -> VerificationResult:
    """Check if device has firmware installed."""
    try:
        ser = open_serial(port)
        reset_device(ser)
        
        import time
        time.sleep(2)
        
        output = read_until_timeout(ser, timeout=2)
        text = output.decode('utf-8', errors='ignore')
        ser.close()
        
        # Look for signs of running firmware
        if "boot:" in text.lower() or "app_main" in text.lower() or "reflex" in text.lower():
            return VerificationResult(
                name="Current Firmware",
                passed=True,
                message="Firmware detected",
                details={"has_firmware": True}
            )
        elif "rst:" in text.lower():
            return VerificationResult(
                name="Current Firmware",
                passed=True,
                message="Bootloader only (no app)",
                details={"has_firmware": False, "has_bootloader": True}
            )
        else:
            return VerificationResult(
                name="Current Firmware",
                passed=True,
                message="Unknown state",
                details={"has_firmware": None}
            )
    except Exception as e:
        return VerificationResult(
            name="Current Firmware",
            passed=False,
            message=f"Error: {e}"
        )


def verify_device(port: str, 
                  expected_chip: str = "ESP32-C6",
                  min_flash_mb: int = 2) -> VerificationReport:
    """
    Run full pre-flight verification on a device.
    """
    checks = []
    
    # Run all checks
    checks.append(check_usb_connection(port))
    checks.append(check_chip_type(port, expected_chip))
    checks.append(check_flash_size(port, min_flash_mb))
    checks.append(check_bootloader_entry(port))
    checks.append(check_uart_echo(port))
    checks.append(check_current_firmware(port))
    
    # Get chip info for report
    chip_info = get_chip_info(port)
    
    # Determine overall status
    failed = sum(1 for c in checks if not c.passed)
    if failed == 0:
        status = "passed"
    elif failed <= 2:
        status = "warning"
    else:
        status = "failed"
    
    return VerificationReport(
        port=port,
        timestamp=datetime.now().isoformat(),
        checks=checks,
        chip_info=chip_info,
        overall_status=status,
    )
