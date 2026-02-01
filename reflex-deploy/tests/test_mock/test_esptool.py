"""
Mock tests for esptool wrapper.
"""

import pytest
from unittest.mock import patch, MagicMock
from pathlib import Path

from reflex_cli.util.esptool_wrapper import (
    run_esptool,
    get_chip_info,
    read_flash,
    write_flash,
    erase_flash,
    verify_flash,
    flash_firmware,
    ChipInfo,
)


class TestRunEsptool:
    """Tests for raw esptool command execution."""
    
    @pytest.mark.mock
    def test_run_esptool_success(self, mock_esptool):
        """Verify successful esptool command."""
        success, output = run_esptool("/dev/ttyACM0", "chip_id")
        
        assert success is True
        assert "ESP32-C6" in output
    
    @pytest.mark.mock
    def test_run_esptool_failure(self):
        """Verify failed esptool command returns False."""
        with patch('subprocess.run') as mock_run:
            mock_run.return_value = MagicMock(returncode=1, stdout="", stderr="Error")
            
            success, output = run_esptool("/dev/ttyACM0", "bad_command")
            
            assert success is False
    
    @pytest.mark.mock
    def test_run_esptool_timeout(self):
        """Verify timeout is handled gracefully."""
        import subprocess
        with patch('subprocess.run') as mock_run:
            mock_run.side_effect = subprocess.TimeoutExpired(cmd="esptool", timeout=30)
            
            success, output = run_esptool("/dev/ttyACM0", "chip_id", timeout=30)
            
            assert success is False
            assert "timed out" in output.lower()


class TestGetChipInfo:
    """Tests for chip info retrieval."""
    
    @pytest.mark.mock
    def test_get_chip_info_success(self, mock_esptool):
        """Verify chip info is parsed correctly."""
        info = get_chip_info("/dev/ttyACM0")
        
        assert info is not None
        assert "ESP32-C6" in info.chip_type
        assert info.mac_address == "40:4c:ca:ff:ff:ff"
    
    @pytest.mark.mock
    def test_get_chip_info_parses_flash_size(self, mock_esptool):
        """Verify flash size is parsed."""
        info = get_chip_info("/dev/ttyACM0")
        
        assert info is not None
        assert info.flash_size == 4 * 1024 * 1024  # 4MB
    
    @pytest.mark.mock
    def test_get_chip_info_failure(self):
        """Verify None returned on failure."""
        with patch('reflex_cli.util.esptool_wrapper.run_esptool') as mock_run:
            mock_run.return_value = (False, "Error")
            
            info = get_chip_info("/dev/ttyACM0")
            
            assert info is None


class TestFlashOperations:
    """Tests for flash read/write/erase operations."""
    
    @pytest.mark.mock
    def test_read_flash(self, mock_esptool, tmp_path):
        """Verify flash read command is formed correctly."""
        output_path = tmp_path / "flash_dump.bin"
        
        success, output = read_flash("/dev/ttyACM0", 0x0, 0x1000, output_path)
        
        assert success is True
    
    @pytest.mark.mock
    def test_write_flash(self, mock_esptool, tmp_path):
        """Verify flash write command is formed correctly."""
        firmware = tmp_path / "firmware.bin"
        firmware.write_bytes(b"\x00" * 1024)
        
        success, output = write_flash("/dev/ttyACM0", 0x10000, firmware)
        
        assert success is True
    
    @pytest.mark.mock
    def test_write_flash_with_verify(self, mock_esptool, tmp_path):
        """Verify write flash includes verify flag."""
        firmware = tmp_path / "firmware.bin"
        firmware.write_bytes(b"\x00" * 1024)
        
        with patch('subprocess.run') as mock_run:
            mock_run.return_value = MagicMock(returncode=0, stdout="OK", stderr="")
            
            write_flash("/dev/ttyACM0", 0x10000, firmware, verify=True)
            
            # Check that --verify was in the command
            call_args = mock_run.call_args[0][0]
            assert "--verify" in call_args
    
    @pytest.mark.mock
    def test_erase_flash(self, mock_esptool):
        """Verify erase flash command."""
        success, output = erase_flash("/dev/ttyACM0")
        
        assert success is True
    
    @pytest.mark.mock
    def test_verify_flash(self, mock_esptool, tmp_path):
        """Verify flash verification command."""
        firmware = tmp_path / "firmware.bin"
        firmware.write_bytes(b"\x00" * 1024)
        
        success, output = verify_flash("/dev/ttyACM0", 0x10000, firmware)
        
        assert success is True


class TestFlashFirmware:
    """Tests for complete firmware flashing."""
    
    @pytest.mark.mock
    def test_flash_firmware_all_components(self, mock_esptool, tmp_firmware_dir):
        """Verify all firmware components are flashed."""
        bootloader = tmp_firmware_dir / "bootloader.bin"
        partition = tmp_firmware_dir / "partition-table.bin"
        app = tmp_firmware_dir / "reflex-spine.bin"
        
        success, message, checksums = flash_firmware(
            "/dev/ttyACM0",
            bootloader_path=bootloader,
            partition_table_path=partition,
            app_path=app,
        )
        
        assert success is True
        assert "bootloader" in checksums
        assert "partition_table" in checksums
        assert "application" in checksums
    
    @pytest.mark.mock
    def test_flash_firmware_checksums_calculated(self, mock_esptool, tmp_firmware_dir):
        """Verify checksums are calculated for all components."""
        app = tmp_firmware_dir / "reflex-spine.bin"
        
        success, message, checksums = flash_firmware(
            "/dev/ttyACM0",
            app_path=app,
        )
        
        # Checksum should be 64 char hex string
        assert len(checksums["application"]) == 64
    
    @pytest.mark.mock
    def test_flash_firmware_missing_file(self, mock_esptool, tmp_path):
        """Verify handling of missing firmware file."""
        nonexistent = tmp_path / "does_not_exist.bin"
        
        success, message, checksums = flash_firmware(
            "/dev/ttyACM0",
            app_path=nonexistent,
        )
        
        # Should not crash, just skip the missing file
        assert "application" not in checksums
    
    @pytest.mark.mock
    def test_flash_firmware_failure_handling(self, tmp_firmware_dir):
        """Verify flash failure is handled."""
        app = tmp_firmware_dir / "reflex-spine.bin"
        
        with patch('reflex_cli.util.esptool_wrapper.write_flash') as mock_write:
            mock_write.return_value = (False, "Flash failed: write error")
            
            success, message, checksums = flash_firmware(
                "/dev/ttyACM0",
                app_path=app,
            )
            
            assert success is False
            assert "failed" in message.lower()
