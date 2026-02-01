"""
Unit tests for telemetry parsing.
"""

import pytest
from reflex_cli.monitor import parse_telemetry


class TestParseTelemetry:
    """Tests for parse_telemetry function."""
    
    @pytest.mark.unit
    def test_parse_signal_count(self, sample_telemetry_output):
        """Verify signal count is extracted correctly."""
        result = parse_telemetry(sample_telemetry_output)
        assert result.get('signal_count') == 5009
    
    @pytest.mark.unit
    def test_parse_anomaly_count(self, sample_telemetry_output):
        """Verify anomaly count is extracted correctly."""
        result = parse_telemetry(sample_telemetry_output)
        assert result.get('anomaly_count') == 2532
    
    @pytest.mark.unit
    def test_parse_min_latency(self, sample_telemetry_output):
        """Verify minimum latency is extracted correctly."""
        result = parse_telemetry(sample_telemetry_output)
        assert result.get('reflex_min_ns') == 87
    
    @pytest.mark.unit
    def test_parse_max_latency(self, sample_telemetry_output):
        """Verify maximum latency is extracted correctly."""
        result = parse_telemetry(sample_telemetry_output)
        assert result.get('reflex_max_ns') == 3175
    
    @pytest.mark.unit
    def test_parse_avg_latency(self, sample_telemetry_output):
        """Verify average latency is extracted correctly."""
        result = parse_telemetry(sample_telemetry_output)
        assert result.get('reflex_avg_ns') == 87
    
    @pytest.mark.unit
    def test_parse_empty_output(self):
        """Verify empty output returns empty dict."""
        result = parse_telemetry("")
        assert result == {}
    
    @pytest.mark.unit
    def test_parse_garbage_output(self):
        """Verify garbage output returns empty dict."""
        result = parse_telemetry("random garbage xyz 123 !@#$%")
        assert result == {}
    
    @pytest.mark.unit
    def test_parse_partial_output(self):
        """Verify partial output extracts available fields."""
        partial = "Signals:   1234\nSome other text"
        result = parse_telemetry(partial)
        assert result.get('signal_count') == 1234
        assert 'anomaly_count' not in result
    
    @pytest.mark.unit
    def test_parse_with_error_keyword(self):
        """Verify error detection in output."""
        error_output = "Normal output\nERROR: Something went wrong\nMore text"
        result = parse_telemetry(error_output)
        assert 'last_error' in result
    
    @pytest.mark.unit
    def test_parse_different_formats(self):
        """Verify parsing handles different spacing."""
        variants = [
            "Signals: 100",
            "Signals:100",
            "Signals:   100",
            "Signals:\t100",
        ]
        for variant in variants:
            result = parse_telemetry(variant)
            assert result.get('signal_count') == 100, f"Failed on: {variant}"
    
    @pytest.mark.unit
    def test_parse_large_numbers(self):
        """Verify parsing handles large numbers."""
        large = "Signals: 999999999\nAnomalies: 888888888"
        result = parse_telemetry(large)
        assert result.get('signal_count') == 999999999
        assert result.get('anomaly_count') == 888888888
