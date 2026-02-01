"""
Unit tests for checksum calculation.
"""

import pytest
import hashlib
from pathlib import Path
from reflex_cli.util.esptool_wrapper import calculate_checksum


class TestChecksum:
    """Tests for SHA256 checksum calculation."""
    
    @pytest.mark.unit
    def test_checksum_known_value(self, tmp_path):
        """Verify checksum matches expected value for known content."""
        test_file = tmp_path / "test.bin"
        content = b"Hello, World!"
        test_file.write_bytes(content)
        
        # Calculate expected checksum
        expected = hashlib.sha256(content).hexdigest()
        
        result = calculate_checksum(test_file)
        assert result == expected
    
    @pytest.mark.unit
    def test_checksum_empty_file(self, tmp_path):
        """Verify checksum of empty file."""
        test_file = tmp_path / "empty.bin"
        test_file.write_bytes(b"")
        
        expected = hashlib.sha256(b"").hexdigest()
        result = calculate_checksum(test_file)
        
        assert result == expected
    
    @pytest.mark.unit
    def test_checksum_binary_content(self, tmp_path):
        """Verify checksum handles binary content correctly."""
        test_file = tmp_path / "binary.bin"
        content = bytes(range(256))  # All possible byte values
        test_file.write_bytes(content)
        
        expected = hashlib.sha256(content).hexdigest()
        result = calculate_checksum(test_file)
        
        assert result == expected
    
    @pytest.mark.unit
    def test_checksum_large_file(self, tmp_path):
        """Verify checksum handles large files correctly."""
        test_file = tmp_path / "large.bin"
        # 1MB of pattern data
        content = b"REFLEX" * (1024 * 1024 // 6)
        test_file.write_bytes(content)
        
        expected = hashlib.sha256(content).hexdigest()
        result = calculate_checksum(test_file)
        
        assert result == expected
    
    @pytest.mark.unit
    def test_checksum_deterministic(self, tmp_path):
        """Verify same file produces same checksum."""
        test_file = tmp_path / "deterministic.bin"
        content = b"Test content for determinism check"
        test_file.write_bytes(content)
        
        result1 = calculate_checksum(test_file)
        result2 = calculate_checksum(test_file)
        result3 = calculate_checksum(test_file)
        
        assert result1 == result2 == result3
    
    @pytest.mark.unit
    def test_checksum_different_content(self, tmp_path):
        """Verify different content produces different checksum."""
        file1 = tmp_path / "file1.bin"
        file2 = tmp_path / "file2.bin"
        
        file1.write_bytes(b"Content A")
        file2.write_bytes(b"Content B")
        
        result1 = calculate_checksum(file1)
        result2 = calculate_checksum(file2)
        
        assert result1 != result2
    
    @pytest.mark.unit
    def test_checksum_is_hex_string(self, tmp_path):
        """Verify checksum is a valid hex string."""
        test_file = tmp_path / "hex.bin"
        test_file.write_bytes(b"test")
        
        result = calculate_checksum(test_file)
        
        # SHA256 produces 64 hex characters
        assert len(result) == 64
        assert all(c in '0123456789abcdef' for c in result)
