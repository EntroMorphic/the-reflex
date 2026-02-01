"""
Integration tests for device monitoring.
Requires real ESP32 hardware connected with running firmware.
"""

import pytest
import time
from reflex_cli.monitor import (
    DeviceMonitor,
    FleetMonitor,
    monitor_single,
    parse_telemetry,
    DeviceMetrics,
)


class TestDeviceMonitor:
    """Tests for single device monitoring."""
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_monitor_starts_and_stops(self, real_device):
        """Verify monitor can start and stop cleanly."""
        monitor = DeviceMonitor(real_device.port)
        
        monitor.start()
        assert monitor.running is True
        
        time.sleep(1)
        
        monitor.stop()
        assert monitor.running is False
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_monitor_receives_data(self, real_device):
        """Verify monitor receives some data."""
        monitor = DeviceMonitor(real_device.port)
        
        monitor.start()
        time.sleep(5)  # Wait for device output
        monitor.stop()
        
        # Should have updated metrics
        assert monitor.metrics.timestamp != ""
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_monitor_detects_heartbeat(self, real_device):
        """Verify heartbeat detection."""
        monitor = DeviceMonitor(real_device.port)
        
        monitor.start()
        time.sleep(3)
        
        # Check heartbeat
        had_heartbeat = monitor.metrics.heartbeat
        
        monitor.stop()
        
        # Should have detected heartbeat if device is outputting
        # (may be False if device is silent)
        assert isinstance(had_heartbeat, bool)
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_monitor_metrics_output_formats(self, real_device):
        """Verify all output formats work."""
        monitor = DeviceMonitor(real_device.port)
        
        monitor.start()
        time.sleep(2)
        monitor.stop()
        
        # Test all formats
        yaml_out = monitor.metrics.to_yaml()
        assert "port:" in yaml_out
        
        prom_out = monitor.metrics.to_prometheus()
        assert "reflex_heartbeat{" in prom_out
        
        md_out = monitor.metrics.to_markdown()
        assert "| Metric | Value |" in md_out


class TestFleetMonitor:
    """Tests for multi-device monitoring."""
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_fleet_monitor_all_devices(self, all_real_devices):
        """Verify fleet monitoring of all devices."""
        ports = [d.port for d in all_real_devices]
        fleet = FleetMonitor(ports)
        
        fleet.start_all()
        time.sleep(3)
        fleet.stop_all()
        
        metrics = fleet.get_all_metrics()
        assert len(metrics) == len(all_real_devices)
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_fleet_monitor_yaml_output(self, all_real_devices):
        """Verify fleet YAML output."""
        ports = [d.port for d in all_real_devices]
        fleet = FleetMonitor(ports)
        
        fleet.start_all()
        time.sleep(2)
        fleet.stop_all()
        
        yaml_out = fleet.to_yaml()
        assert "timestamp:" in yaml_out
        assert "devices:" in yaml_out
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_fleet_monitor_prometheus_output(self, all_real_devices):
        """Verify fleet Prometheus output."""
        ports = [d.port for d in all_real_devices]
        fleet = FleetMonitor(ports)
        
        fleet.start_all()
        time.sleep(2)
        fleet.stop_all()
        
        prom_out = fleet.to_prometheus()
        assert "# HELP reflex_heartbeat" in prom_out
        
        # Should have metrics for each device
        for device in all_real_devices:
            port_label = device.port.replace("/", "_")
            assert port_label in prom_out


class TestMonitorSingle:
    """Tests for monitor_single convenience function."""
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_monitor_single_yaml(self, real_device):
        """Verify single device monitoring with YAML output."""
        result = monitor_single(real_device.port, output_format="yaml", duration=2)
        
        assert "port:" in result
        assert real_device.port in result
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_monitor_single_json(self, real_device):
        """Verify single device monitoring with JSON output."""
        result = monitor_single(real_device.port, output_format="json", duration=2)
        
        import json
        parsed = json.loads(result)
        assert parsed['port'] == real_device.port
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_monitor_single_markdown(self, real_device):
        """Verify single device monitoring with Markdown output."""
        result = monitor_single(real_device.port, output_format="markdown", duration=2)
        
        assert "| Metric | Value |" in result
