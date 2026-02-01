"""
Integration tests for device verification.
Requires real ESP32 hardware connected.
"""

import pytest
from reflex_cli.verify import (
    verify_device,
    check_usb_connection,
    check_chip_type,
    check_flash_size,
    check_bootloader_entry,
    check_uart_echo,
    check_current_firmware,
)


class TestVerificationChecks:
    """Tests for individual verification checks."""
    
    @pytest.mark.hardware
    def test_usb_connection_check(self, real_device):
        """Verify USB connection check passes."""
        result = check_usb_connection(real_device.port)
        
        assert result.passed is True
        assert "accessible" in result.message.lower()
    
    @pytest.mark.hardware
    def test_chip_type_check_c6(self, real_device):
        """Verify chip type check for ESP32-C6."""
        result = check_chip_type(real_device.port, expected="ESP32-C6")
        
        # May pass or fail depending on actual chip
        # But should not error
        assert result.name == "Chip Type"
        assert isinstance(result.passed, bool)
    
    @pytest.mark.hardware
    def test_flash_size_check(self, real_device):
        """Verify flash size check."""
        result = check_flash_size(real_device.port, min_size_mb=2)
        
        assert result.name == "Flash Size"
        if result.passed:
            assert "MB" in result.message
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_bootloader_entry_check(self, real_device):
        """Verify bootloader entry check."""
        result = check_bootloader_entry(real_device.port)
        
        assert result.name == "Bootloader Entry"
        # May or may not succeed depending on device state
        assert isinstance(result.passed, bool)
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_uart_echo_check(self, real_device):
        """Verify UART communication check."""
        result = check_uart_echo(real_device.port)
        
        assert result.name == "UART Communication"
        # Should receive some data after reset
        assert isinstance(result.passed, bool)
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_current_firmware_check(self, real_device):
        """Verify firmware detection check."""
        result = check_current_firmware(real_device.port)
        
        assert result.name == "Current Firmware"
        assert isinstance(result.passed, bool)


class TestFullVerification:
    """Tests for complete verification report."""
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_full_verification(self, real_device):
        """Verify complete device verification."""
        report = verify_device(real_device.port)
        
        assert report.port == real_device.port
        assert report.timestamp is not None
        assert len(report.checks) >= 5  # Should have multiple checks
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_verification_report_status(self, real_device):
        """Verify report has valid overall status."""
        report = verify_device(real_device.port)
        
        assert report.overall_status in ["passed", "warning", "failed"]
    
    @pytest.mark.hardware
    @pytest.mark.slow
    def test_verification_report_markdown(self, real_device):
        """Verify markdown report generation."""
        report = verify_device(real_device.port)
        markdown = report.to_markdown()
        
        assert "# Pre-Flight Verification Report" in markdown
        assert real_device.port in markdown
        assert "✅" in markdown or "❌" in markdown


class TestVerificationEdgeCases:
    """Edge case tests for verification."""
    
    @pytest.mark.hardware
    def test_wrong_chip_type_expected(self, real_device):
        """Verify chip type mismatch is detected."""
        # Expect a chip type that doesn't exist
        result = check_chip_type(real_device.port, expected="ESP32-FAKE")
        
        # Should fail because chip won't match
        if result.passed:
            # Unless the device somehow is that type
            assert "ESP32-FAKE" in result.message
        else:
            assert "Expected" in result.message or "got" in result.message
    
    @pytest.mark.hardware
    def test_insufficient_flash_requirement(self, real_device):
        """Verify flash size check with unrealistic requirement."""
        # Require 1TB of flash - should fail
        result = check_flash_size(real_device.port, min_size_mb=1024*1024)
        
        assert result.passed is False
        assert "minimum" in result.message.lower() or "<" in result.message
