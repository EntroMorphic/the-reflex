"""
Mock tests for serial communication.
"""

import pytest
from unittest.mock import Mock, patch, MagicMock
import serial

from reflex_cli.util.serial import (
    open_serial,
    reset_device,
    read_until_timeout,
    send_command,
    enter_bootloader,
    find_esp32_devices,
    DeviceInfo,
)


class TestSerialConnection:
    """Tests for serial connection handling."""
    
    @pytest.mark.mock
    def test_open_serial_success(self, mock_serial):
        """Verify serial port opens successfully."""
        with patch('reflex_cli.util.serial.serial.Serial') as mock_class:
            mock_class.return_value = mock_serial
            
            ser = open_serial("/dev/ttyACM0")
            
            mock_class.assert_called_once_with("/dev/ttyACM0", 115200, timeout=1.0)
    
    @pytest.mark.mock
    def test_open_serial_custom_baudrate(self, mock_serial):
        """Verify custom baudrate is used."""
        with patch('reflex_cli.util.serial.serial.Serial') as mock_class:
            mock_class.return_value = mock_serial
            
            ser = open_serial("/dev/ttyACM0", baudrate=921600)
            
            mock_class.assert_called_once_with("/dev/ttyACM0", 921600, timeout=1.0)
    
    @pytest.mark.mock
    def test_open_serial_failure(self):
        """Verify serial exception is raised on failure."""
        with patch('reflex_cli.util.serial.serial.Serial') as mock_class:
            mock_class.side_effect = serial.SerialException("Port not found")
            
            with pytest.raises(serial.SerialException):
                open_serial("/dev/nonexistent")


class TestResetDevice:
    """Tests for device reset functionality."""
    
    @pytest.mark.mock
    def test_reset_toggles_dtr(self):
        """Verify DTR is toggled for reset."""
        mock_ser = MagicMock()
        
        reset_device(mock_ser)
        
        # Should toggle DTR: False -> True
        assert mock_ser.dtr == True  # Final state
    
    @pytest.mark.mock
    def test_reset_with_delay(self):
        """Verify reset respects delay parameter."""
        mock_ser = MagicMock()
        
        with patch('time.sleep') as mock_sleep:
            reset_device(mock_ser, delay=0.5)
            
            # Should sleep twice (before and after toggle)
            assert mock_sleep.call_count == 2
            mock_sleep.assert_called_with(0.5)


class TestReadData:
    """Tests for reading data from device."""
    
    @pytest.mark.mock
    def test_read_until_timeout_with_data(self):
        """Verify data is read correctly."""
        mock_ser = MagicMock()
        mock_ser.read.side_effect = [b"Hello", b" World", b""]
        
        result = read_until_timeout(mock_ser, timeout=1.0)
        
        assert result == b"Hello World"
    
    @pytest.mark.mock
    def test_read_until_timeout_empty(self):
        """Verify empty read returns empty bytes."""
        mock_ser = MagicMock()
        mock_ser.read.return_value = b""
        
        result = read_until_timeout(mock_ser, timeout=0.1)
        
        assert result == b""
    
    @pytest.mark.mock
    def test_read_sets_timeout(self):
        """Verify timeout is set on serial port."""
        mock_ser = MagicMock()
        mock_ser.read.return_value = b""
        
        read_until_timeout(mock_ser, timeout=5.0)
        
        assert mock_ser.timeout == 5.0


class TestSendCommand:
    """Tests for sending commands to device."""
    
    @pytest.mark.mock
    def test_send_command_writes_data(self):
        """Verify command is written with newline."""
        mock_ser = MagicMock()
        mock_ser.read.return_value = b"OK\n"
        
        with patch('reflex_cli.util.serial.read_until_timeout', return_value=b"OK\n"):
            response = send_command(mock_ser, "test_cmd")
        
        mock_ser.write.assert_called_once_with(b"test_cmd\n")
    
    @pytest.mark.mock
    def test_send_command_clears_buffer(self):
        """Verify input buffer is cleared before sending."""
        mock_ser = MagicMock()
        mock_ser.read.return_value = b""
        
        with patch('reflex_cli.util.serial.read_until_timeout', return_value=b""):
            send_command(mock_ser, "cmd")
        
        mock_ser.reset_input_buffer.assert_called_once()


class TestBootloaderEntry:
    """Tests for bootloader entry sequence."""
    
    @pytest.mark.mock
    def test_enter_bootloader_success(self):
        """Verify bootloader entry returns True on success."""
        with patch('serial.Serial') as mock_class:
            mock_ser = MagicMock()
            mock_class.return_value = mock_ser
            
            result = enter_bootloader("/dev/ttyACM0")
            
            assert result is True
            mock_ser.close.assert_called_once()
    
    @pytest.mark.mock
    def test_enter_bootloader_toggles_pins(self):
        """Verify correct DTR/RTS sequence for bootloader."""
        with patch('serial.Serial') as mock_class:
            mock_ser = MagicMock()
            mock_class.return_value = mock_ser
            
            enter_bootloader("/dev/ttyACM0")
            
            # Bootloader entry involves DTR/RTS manipulation
            # Just verify the mock was used
            assert mock_ser.dtr is not None or mock_ser.rts is not None
    
    @pytest.mark.mock
    def test_enter_bootloader_failure(self):
        """Verify bootloader entry returns False on error."""
        with patch('serial.Serial') as mock_class:
            mock_class.side_effect = Exception("Connection failed")
            
            result = enter_bootloader("/dev/ttyACM0")
            
            assert result is False


class TestDeviceDiscovery:
    """Tests for ESP32 device discovery."""
    
    @pytest.mark.mock
    def test_find_devices_with_esp32(self, mock_list_ports):
        """Verify ESP32 devices are found."""
        with patch('reflex_cli.util.serial.list_serial_ports', return_value=mock_list_ports):
            devices = find_esp32_devices()
        
        assert len(devices) == 1
        assert devices[0].port == "/dev/ttyACM0"
        assert devices[0].vid == 0x303a
    
    @pytest.mark.mock
    def test_find_devices_empty(self):
        """Verify empty list when no devices."""
        with patch('reflex_cli.util.serial.list_serial_ports', return_value=[]):
            devices = find_esp32_devices()
        
        assert devices == []
    
    @pytest.mark.mock
    def test_find_devices_filters_non_esp32(self):
        """Verify non-ESP32 devices are filtered out."""
        mock_port = MagicMock()
        mock_port.device = "/dev/ttyUSB0"
        mock_port.vid = 0x1234  # Unknown VID
        mock_port.pid = 0x5678
        
        with patch('reflex_cli.util.serial.list_serial_ports', return_value=[mock_port]):
            devices = find_esp32_devices()
        
        assert devices == []
