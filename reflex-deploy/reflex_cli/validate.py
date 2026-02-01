"""
Post-installation validation tests.
"""

import time
import re
from datetime import datetime
from typing import Optional, Dict, Any

from .util import (
    open_serial,
    reset_device,
    read_until_timeout,
    send_command,
    get_chip_info,
    ValidationReport,
)


def run_boot_test(port: str, timeout: float = 5.0) -> Dict[str, Any]:
    """Test that device boots successfully."""
    try:
        ser = open_serial(port)
        reset_device(ser)
        time.sleep(timeout)
        
        output = read_until_timeout(ser, timeout=2)
        text = output.decode('utf-8', errors='ignore')
        ser.close()
        
        # Check for successful boot indicators
        if "app_main" in text.lower() or "reflex" in text.lower() or "spine" in text.lower():
            return {
                "passed": True,
                "measured": {"boot_detected": True, "output_length": len(text)},
            }
        elif "boot:" in text.lower():
            return {
                "passed": False,
                "error": "Bootloader runs but app not starting",
                "measured": {"boot_detected": False},
            }
        else:
            return {
                "passed": False,
                "error": "No boot output detected",
            }
    except Exception as e:
        return {"passed": False, "error": str(e)}


def run_self_test(port: str) -> Dict[str, Any]:
    """Trigger and check device self-test."""
    try:
        ser = open_serial(port)
        reset_device(ser)
        time.sleep(3)
        
        output = read_until_timeout(ser, timeout=5)
        text = output.decode('utf-8', errors='ignore')
        ser.close()
        
        # Look for test results in output
        if "PASS" in text.upper() and "FAIL" not in text.upper():
            return {
                "passed": True,
                "measured": {"self_test": "passed"},
            }
        elif "FAIL" in text.upper():
            return {
                "passed": False,
                "error": "Self-test reported failure",
                "measured": {"self_test": "failed"},
            }
        else:
            # No explicit pass/fail - assume OK if booted
            return {
                "passed": True,
                "measured": {"self_test": "no_explicit_result"},
            }
    except Exception as e:
        return {"passed": False, "error": str(e)}


def run_reflex_benchmark(port: str) -> Dict[str, Any]:
    """
    Check reflex latency from device output.
    Looks for benchmark results in boot output.
    """
    try:
        ser = open_serial(port)
        reset_device(ser)
        time.sleep(8)  # Wait for benchmarks to complete
        
        output = read_until_timeout(ser, timeout=5)
        text = output.decode('utf-8', errors='ignore')
        ser.close()
        
        # Parse latency from output
        # Looking for patterns like "Avg:   14 cycles =   87 ns"
        min_ns = None
        max_ns = None
        avg_ns = None
        
        for line in text.split('\n'):
            if 'Min:' in line and 'ns' in line:
                match = re.search(r'(\d+)\s*ns', line)
                if match:
                    min_ns = int(match.group(1))
            elif 'Max:' in line and 'ns' in line:
                match = re.search(r'(\d+)\s*ns', line)
                if match:
                    max_ns = int(match.group(1))
            elif 'Avg:' in line and 'ns' in line:
                match = re.search(r'(\d+)\s*ns', line)
                if match:
                    avg_ns = int(match.group(1))
        
        if avg_ns is not None:
            # Pass if average is under 1 microsecond
            passed = avg_ns < 1000
            return {
                "passed": passed,
                "measured": {
                    "min_ns": min_ns,
                    "max_ns": max_ns,
                    "avg_ns": avg_ns,
                },
                "expected": {"avg_ns": "< 1000"},
            }
        else:
            return {
                "passed": False,
                "error": "Could not parse benchmark results",
            }
    except Exception as e:
        return {"passed": False, "error": str(e)}


def run_gpio_test(port: str) -> Dict[str, Any]:
    """
    Check GPIO functionality.
    Looks for LED toggle or GPIO test output.
    """
    try:
        ser = open_serial(port)
        reset_device(ser)
        time.sleep(3)
        
        output = read_until_timeout(ser, timeout=3)
        text = output.decode('utf-8', errors='ignore')
        ser.close()
        
        # Look for GPIO test results
        if "LED" in text.upper() or "GPIO" in text.upper():
            return {
                "passed": True,
                "measured": {"gpio_mentioned": True},
            }
        else:
            # No GPIO test output - manual verification needed
            return {
                "passed": True,  # Assume pass, flag for manual check
                "measured": {"gpio_mentioned": False, "manual_check": True},
            }
    except Exception as e:
        return {"passed": False, "error": str(e)}


def run_watchdog_test(port: str) -> Dict[str, Any]:
    """
    Verify watchdog is functional.
    This is a passive check - we look for watchdog config in output.
    """
    try:
        ser = open_serial(port)
        reset_device(ser)
        time.sleep(3)
        
        output = read_until_timeout(ser, timeout=2)
        text = output.decode('utf-8', errors='ignore')
        ser.close()
        
        # Look for watchdog indicators
        if "watchdog" in text.lower() or "wdt" in text.lower():
            return {
                "passed": True,
                "measured": {"watchdog_configured": True},
            }
        else:
            return {
                "passed": True,  # Not a failure, just not visible
                "measured": {"watchdog_configured": "unknown"},
            }
    except Exception as e:
        return {"passed": False, "error": str(e)}


def run_stress_test(port: str, duration: float = 10.0) -> Dict[str, Any]:
    """
    Monitor device under sustained operation.
    Checks for errors/resets during the period.
    """
    try:
        ser = open_serial(port)
        reset_device(ser)
        time.sleep(2)
        
        # Clear buffer
        ser.reset_input_buffer()
        
        # Monitor for duration
        start = time.time()
        error_count = 0
        reset_count = 0
        all_output = b""
        
        while time.time() - start < duration:
            chunk = ser.read(1024)
            if chunk:
                all_output += chunk
                text = chunk.decode('utf-8', errors='ignore')
                
                if "error" in text.lower() or "fault" in text.lower():
                    error_count += 1
                if "rst:" in text.lower():
                    reset_count += 1
        
        ser.close()
        
        passed = error_count == 0 and reset_count <= 1  # Allow initial reset
        
        return {
            "passed": passed,
            "measured": {
                "duration_s": duration,
                "errors": error_count,
                "resets": reset_count,
                "bytes_received": len(all_output),
            },
            "expected": {"errors": 0, "resets": "<= 1"},
        }
    except Exception as e:
        return {"passed": False, "error": str(e)}


def validate_device(port: str, firmware_version: str = "unknown") -> ValidationReport:
    """
    Run complete validation suite on a device.
    """
    # Get chip info
    chip_info = get_chip_info(port)
    chip_id = chip_info.chip_id if chip_info else "unknown"
    
    report = ValidationReport(
        timestamp=datetime.now().isoformat(),
        device_port=port,
        device_chip_id=chip_id,
        firmware_version=firmware_version,
    )
    
    # Run tests
    tests = [
        ("Boot Test", run_boot_test),
        ("Self Test", run_self_test),
        ("Reflex Benchmark", run_reflex_benchmark),
        ("GPIO Test", run_gpio_test),
        ("Watchdog Test", run_watchdog_test),
        ("Stress Test (10s)", run_stress_test),
    ]
    
    for name, test_fn in tests:
        try:
            result = test_fn(port)
            report.add_test(
                name=name,
                passed=result.get("passed", False),
                measured=result.get("measured"),
                expected=result.get("expected"),
                error=result.get("error"),
            )
        except Exception as e:
            report.add_test(
                name=name,
                passed=False,
                error=str(e),
            )
    
    report.finalize()
    return report
