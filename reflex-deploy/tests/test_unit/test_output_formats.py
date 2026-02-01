"""
Unit tests for output formatting (YAML, JSON, Markdown, Prometheus).
"""

import pytest
import json
import yaml
from reflex_cli.monitor import DeviceMetrics
from reflex_cli.scan import format_device_table
from reflex_cli.util import DeviceInfo


class TestDeviceMetricsOutput:
    """Tests for DeviceMetrics output formatting."""
    
    @pytest.fixture
    def sample_metrics(self):
        """Sample metrics object."""
        return DeviceMetrics(
            port="/dev/ttyACM0",
            timestamp="2026-02-01T10:00:00",
            heartbeat=True,
            uptime_s=3600,
            reflex_min_ns=87,
            reflex_max_ns=3175,
            reflex_avg_ns=187,
            anomaly_count=100,
            signal_count=5000,
            temperature_c=45.5,
            free_heap=200000,
            last_error="",
            status="ok",
        )
    
    @pytest.mark.unit
    def test_to_dict(self, sample_metrics):
        """Verify to_dict produces correct structure."""
        d = sample_metrics.to_dict()
        assert d['port'] == "/dev/ttyACM0"
        assert d['heartbeat'] is True
        assert d['reflex']['avg_ns'] == 187
        assert d['anomaly_count'] == 100
    
    @pytest.mark.unit
    def test_to_yaml(self, sample_metrics):
        """Verify YAML output is valid and parseable."""
        yaml_str = sample_metrics.to_yaml()
        parsed = yaml.safe_load(yaml_str)
        assert parsed['port'] == "/dev/ttyACM0"
        assert parsed['reflex']['avg_ns'] == 187
    
    @pytest.mark.unit
    def test_to_prometheus(self, sample_metrics):
        """Verify Prometheus format is correct."""
        prom = sample_metrics.to_prometheus()
        
        assert 'reflex_heartbeat{port=' in prom
        assert 'reflex_latency_avg_ns{port=' in prom
        assert '187' in prom  # avg_ns value
        assert '_dev_ttyACM0' in prom  # sanitized port name
    
    @pytest.mark.unit
    def test_to_markdown(self, sample_metrics):
        """Verify Markdown output contains expected elements."""
        md = sample_metrics.to_markdown()
        
        assert "/dev/ttyACM0" in md
        assert "187 ns" in md
        assert "| Metric | Value |" in md
        assert "ok" in md


class TestDeviceTableFormatting:
    """Tests for device table formatting."""
    
    @pytest.mark.unit
    def test_format_empty_table(self):
        """Verify empty device list handling."""
        result = format_device_table([])
        assert "No devices found" in result
    
    @pytest.mark.unit
    def test_format_single_device(self):
        """Verify single device table."""
        device = DeviceInfo(
            port="/dev/ttyACM0",
            vid=0x303a,
            pid=0x1001,
            serial_number="12345",
            manufacturer="Espressif",
            product="USB JTAG",
            chip_type="ESP32-C6",
            chip_id="0xaabbccdd",
            flash_size=4*1024*1024,
            mac_address="40:4c:ca:00:11:22",
        )
        
        result = format_device_table([device])
        
        assert "| # | Port |" in result
        assert "/dev/ttyACM0" in result
        assert "ESP32-C6" in result
        assert "4MB" in result
    
    @pytest.mark.unit
    def test_format_multiple_devices(self):
        """Verify multiple device table."""
        devices = [
            DeviceInfo(
                port="/dev/ttyACM0", vid=0x303a, pid=0x1001,
                serial_number="111", manufacturer="Espressif", product="USB",
                chip_type="ESP32-C6", chip_id="0x111", flash_size=4*1024*1024,
            ),
            DeviceInfo(
                port="/dev/ttyACM1", vid=0x303a, pid=0x1001,
                serial_number="222", manufacturer="Espressif", product="USB",
                chip_type="ESP32-C6", chip_id="0x222", flash_size=4*1024*1024,
            ),
        ]
        
        result = format_device_table(devices)
        
        assert "/dev/ttyACM0" in result
        assert "/dev/ttyACM1" in result
        assert "| 1 |" in result
        assert "| 2 |" in result
    
    @pytest.mark.unit
    def test_format_unknown_values(self):
        """Verify handling of None/unknown values."""
        device = DeviceInfo(
            port="/dev/ttyACM0",
            vid=0x303a,
            pid=0x1001,
            serial_number=None,
            manufacturer=None,
            product=None,
            chip_type=None,
            chip_id=None,
            flash_size=None,
        )
        
        result = format_device_table([device])
        
        assert "unknown" in result
        assert "/dev/ttyACM0" in result


class TestDeviceInfoSerialization:
    """Tests for DeviceInfo serialization."""
    
    @pytest.mark.unit
    def test_to_dict(self):
        """Verify to_dict produces correct output."""
        device = DeviceInfo(
            port="/dev/ttyACM0",
            vid=0x303a,
            pid=0x1001,
            serial_number="12345",
            manufacturer="Espressif",
            product="USB JTAG",
        )
        
        d = device.to_dict()
        
        assert d['port'] == "/dev/ttyACM0"
        assert d['vid'] == "0x303a"
        assert d['pid'] == "0x1001"
    
    @pytest.mark.unit
    def test_to_json(self):
        """Verify to_json produces valid JSON."""
        device = DeviceInfo(
            port="/dev/ttyACM0",
            vid=0x303a,
            pid=0x1001,
            serial_number="12345",
            manufacturer="Espressif",
            product="USB JTAG",
        )
        
        json_str = device.to_json()
        parsed = json.loads(json_str)
        
        assert parsed['port'] == "/dev/ttyACM0"
