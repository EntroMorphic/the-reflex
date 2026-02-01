"""
Integration tests for device scanning.
Requires real ESP32 hardware connected.
"""

import pytest
from reflex_cli.scan import scan_and_probe, scan_devices, probe_device, format_device_table
from reflex_cli.util import find_esp32_devices


class TestDeviceScanning:
    """Integration tests for device discovery."""
    
    @pytest.mark.hardware
    def test_scan_finds_devices(self, all_real_devices):
        """Verify scan finds connected devices."""
        devices = find_esp32_devices()
        
        assert len(devices) >= 1
        assert all(d.port is not None for d in devices)
    
    @pytest.mark.hardware
    def test_scan_returns_device_info(self, real_device):
        """Verify device info contains expected fields."""
        assert real_device.port is not None
        assert real_device.vid is not None
        assert real_device.pid is not None
    
    @pytest.mark.hardware
    def test_probe_gets_chip_type(self, real_device):
        """Verify probing retrieves chip type."""
        probed = probe_device(real_device)
        
        assert probed.chip_type is not None
        # Should be some variant of ESP32
        assert "ESP32" in probed.chip_type or "esp32" in probed.chip_type.lower()
    
    @pytest.mark.hardware
    def test_probe_gets_flash_size(self, real_device):
        """Verify probing retrieves flash size."""
        probed = probe_device(real_device)
        
        assert probed.flash_size is not None
        # Should be at least 2MB
        assert probed.flash_size >= 2 * 1024 * 1024
    
    @pytest.mark.hardware
    def test_probe_gets_mac_address(self, real_device):
        """Verify probing retrieves MAC address."""
        probed = probe_device(real_device)
        
        assert probed.mac_address is not None
        # MAC should be 6 bytes in XX:XX:XX:XX:XX:XX format
        assert len(probed.mac_address.split(':')) == 6
    
    @pytest.mark.hardware
    def test_scan_and_probe_combined(self, all_real_devices):
        """Verify combined scan and probe works."""
        devices = scan_and_probe(verbose=True)
        
        assert len(devices) >= 1
        
        for device in devices:
            # Each device should have been probed
            assert device.chip_type is not None or device.chip_id is not None
    
    @pytest.mark.hardware
    def test_format_device_table_real(self, all_real_devices):
        """Verify table formatting with real devices."""
        table = format_device_table(all_real_devices)
        
        assert "| # | Port |" in table
        for device in all_real_devices:
            assert device.port in table


class TestMultipleDevices:
    """Tests for multiple device scenarios."""
    
    @pytest.mark.hardware
    def test_scan_finds_all_devices(self, all_real_devices):
        """Verify all connected devices are found."""
        devices = find_esp32_devices()
        
        # Should find all devices
        ports = [d.port for d in devices]
        for expected in all_real_devices:
            assert expected.port in ports
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_probe_all_devices(self, all_real_devices):
        """Verify all devices can be probed."""
        probed_devices = []
        
        for device in all_real_devices:
            probed = probe_device(device)
            probed_devices.append(probed)
            
            # Each device should have chip info after probing
            assert probed.chip_type is not None
        
        # All devices probed successfully
        assert len(probed_devices) == len(all_real_devices)
