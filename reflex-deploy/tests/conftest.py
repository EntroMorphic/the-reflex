"""
Pytest fixtures for reflex-cli tests.
"""

import pytest
from unittest.mock import Mock, patch, MagicMock
from pathlib import Path
import tempfile
import os


# ============================================================================
# Sample Data Fixtures
# ============================================================================

@pytest.fixture
def sample_config():
    """Sample site configuration dictionary."""
    return {
        "device": {
            "name": "test-c6-01",
            "role": "spine",
            "description": "Test device for unit testing",
        },
        "reflex": {
            "threshold": 5000,
            "hysteresis": 100,
            "safe_value": 0,
        },
        "watchdog": {
            "hardware_timeout_ms": 5000,
            "task_timeout_ms": 3000,
            "reflex_timeout_us": 100,
        },
        "telemetry": {
            "interval_ms": 1000,
            "format": "text",
        },
        "pins": {
            "led": 8,
            "sensor": 0,
            "actuator": 5,
        },
        "compliance": {
            "audit_enabled": True,
            "log_path": "audit.json",
        },
    }


@pytest.fixture
def sample_config_yaml(sample_config, tmp_path):
    """Sample config written to a YAML file."""
    import yaml
    config_path = tmp_path / "test_config.yaml"
    with open(config_path, "w") as f:
        yaml.dump(sample_config, f)
    return config_path


@pytest.fixture
def sample_telemetry_output():
    """Sample telemetry output from a running device."""
    return """
═══════════════════════════════════════════════════════════
       REFLEX SPINE: ESP32-C6 Benchmark
═══════════════════════════════════════════════════════════

Test 1: Baseline Reflex (with synthetic threshold)
───────────────────────────────────────────────────────────
Samples:   10000
Signals:   5009
Anomalies: 2532

Min:    14 cycles =    87 ns
Max:   508 cycles =  3175 ns
Avg:    14 cycles =    87 ns

LED toggled on anomaly detection.
───────────────────────────────────────────────────────────
"""


@pytest.fixture
def sample_boot_output():
    """Sample boot output from device."""
    return """
ESP-ROM:esp32c6-20220919
Build:Sep 19 2022
rst:0x1 (POWERON),boot:0xc (SPI_FAST_FLASH_BOOT)
SPIWP:0xee
mode:DIO, clock div:2
entry 0x4086c818
I (23) boot: ESP-IDF v5.4 2nd stage bootloader
I (24) boot: compile time Feb  1 2026 09:00:00
I (24) boot: chip revision: v0.0
I (27) boot.esp32c6: SPI Speed      : 80MHz
I (32) boot.esp32c6: SPI Mode       : DIO
I (36) boot.esp32c6: SPI Flash Size : 4MB
I (100) app_main: Starting Reflex Spine...
I (101) reflex: Initialized with threshold=5000
"""


@pytest.fixture
def sample_chip_id_output():
    """Sample esptool chip_id output."""
    return """
esptool.py v4.7.0
Serial port /dev/ttyACM0
Connecting....
Detecting chip type... ESP32-C6
Chip is ESP32-C6 (QFN40) (revision v0.0)
Features: WiFi 6, BT 5, IEEE 802.15.4
Crystal is 40MHz
MAC: 40:4c:ca:ff:ff:ff
Uploading stub...
Running stub...
Stub running...
Chip ID: 0x0c6fff0cafe
"""


@pytest.fixture
def sample_flash_id_output():
    """Sample esptool flash_id output."""
    return """
esptool.py v4.7.0
Serial port /dev/ttyACM0
Connecting....
Detecting chip type... ESP32-C6
Manufacturer: c8
Device: 4016
Detected flash size: 4MB
"""


# ============================================================================
# Mock Fixtures
# ============================================================================

@pytest.fixture
def mock_serial():
    """Mock serial.Serial for testing without hardware."""
    with patch('serial.Serial') as mock_class:
        mock_instance = MagicMock()
        mock_instance.read.return_value = b""
        mock_instance.in_waiting = 0
        mock_instance.write.return_value = None
        mock_instance.dtr = False
        mock_instance.rts = False
        mock_class.return_value = mock_instance
        yield mock_instance


@pytest.fixture
def mock_serial_with_data(sample_telemetry_output):
    """Mock serial that returns telemetry data."""
    with patch('serial.Serial') as mock_class:
        mock_instance = MagicMock()
        mock_instance.read.return_value = sample_telemetry_output.encode()
        mock_instance.in_waiting = len(sample_telemetry_output)
        mock_class.return_value = mock_instance
        yield mock_instance


@pytest.fixture
def mock_esptool(sample_chip_id_output, sample_flash_id_output):
    """Mock esptool subprocess calls."""
    def mock_run(cmd, *args, **kwargs):
        result = MagicMock()
        result.returncode = 0
        
        if "chip_id" in cmd:
            result.stdout = sample_chip_id_output
            result.stderr = ""
        elif "flash_id" in cmd:
            result.stdout = sample_flash_id_output
            result.stderr = ""
        else:
            result.stdout = "OK"
            result.stderr = ""
        
        return result
    
    with patch('subprocess.run', side_effect=mock_run) as mock:
        yield mock


@pytest.fixture
def mock_list_ports():
    """Mock serial.tools.list_ports.comports()."""
    mock_port = MagicMock()
    mock_port.device = "/dev/ttyACM0"
    mock_port.vid = 0x303a
    mock_port.pid = 0x1001
    mock_port.serial_number = "12345678"
    mock_port.manufacturer = "Espressif"
    mock_port.product = "USB JTAG/serial debug unit"
    
    with patch('serial.tools.list_ports.comports', return_value=[mock_port]) as mock:
        yield [mock_port]


# ============================================================================
# Temporary File Fixtures
# ============================================================================

@pytest.fixture
def tmp_firmware_dir(tmp_path):
    """Create temporary firmware directory with dummy files."""
    fw_dir = tmp_path / "firmware"
    fw_dir.mkdir()
    
    # Create dummy firmware files
    (fw_dir / "bootloader.bin").write_bytes(b"\x00" * 1024)
    (fw_dir / "partition-table.bin").write_bytes(b"\x00" * 512)
    (fw_dir / "reflex-spine.bin").write_bytes(b"\x00" * 65536)
    
    return fw_dir


@pytest.fixture
def tmp_audit_file(tmp_path):
    """Temporary audit log file."""
    return tmp_path / "audit.json"


# ============================================================================
# Hardware Fixtures (skip if not available)
# ============================================================================

@pytest.fixture
def real_device():
    """
    Real ESP32 device fixture.
    Skips test if no hardware connected.
    """
    from reflex_cli.util import find_esp32_devices
    devices = find_esp32_devices()
    if not devices:
        pytest.skip("No ESP32 devices connected")
    return devices[0]


@pytest.fixture
def all_real_devices():
    """
    All connected ESP32 devices.
    Skips test if no hardware connected.
    """
    from reflex_cli.util import find_esp32_devices
    devices = find_esp32_devices()
    if not devices:
        pytest.skip("No ESP32 devices connected")
    return devices


# ============================================================================
# Helper Fixtures
# ============================================================================

@pytest.fixture
def capture_output():
    """Capture stdout/stderr for CLI testing."""
    import io
    import sys
    
    class OutputCapture:
        def __init__(self):
            self.stdout = io.StringIO()
            self.stderr = io.StringIO()
            self._stdout = sys.stdout
            self._stderr = sys.stderr
        
        def __enter__(self):
            sys.stdout = self.stdout
            sys.stderr = self.stderr
            return self
        
        def __exit__(self, *args):
            sys.stdout = self._stdout
            sys.stderr = self._stderr
        
        def get_stdout(self):
            return self.stdout.getvalue()
        
        def get_stderr(self):
            return self.stderr.getvalue()
    
    return OutputCapture
