"""
Unit tests for configuration parsing.
"""

import pytest
import yaml
from pathlib import Path


class TestConfigParsing:
    """Tests for YAML configuration parsing."""
    
    @pytest.mark.unit
    def test_load_valid_config(self, sample_config_yaml):
        """Verify valid config loads successfully."""
        with open(sample_config_yaml) as f:
            config = yaml.safe_load(f)
        
        assert config['device']['name'] == "test-c6-01"
        assert config['reflex']['threshold'] == 5000
        assert config['pins']['led'] == 8
    
    @pytest.mark.unit
    def test_config_has_all_sections(self, sample_config_yaml):
        """Verify all expected sections are present."""
        with open(sample_config_yaml) as f:
            config = yaml.safe_load(f)
        
        expected_sections = ['device', 'reflex', 'watchdog', 'telemetry', 'pins', 'compliance']
        for section in expected_sections:
            assert section in config, f"Missing section: {section}"
    
    @pytest.mark.unit
    def test_malformed_yaml(self, tmp_path):
        """Verify malformed YAML raises exception."""
        bad_config = tmp_path / "bad.yaml"
        bad_config.write_text("key: [invalid yaml: missing bracket")
        
        with pytest.raises(yaml.YAMLError):
            with open(bad_config) as f:
                yaml.safe_load(f)
    
    @pytest.mark.unit
    def test_empty_config(self, tmp_path):
        """Verify empty config file is handled."""
        empty_config = tmp_path / "empty.yaml"
        empty_config.write_text("")
        
        with open(empty_config) as f:
            config = yaml.safe_load(f)
        
        assert config is None
    
    @pytest.mark.unit
    def test_config_types(self, sample_config_yaml):
        """Verify config values have correct types."""
        with open(sample_config_yaml) as f:
            config = yaml.safe_load(f)
        
        assert isinstance(config['device']['name'], str)
        assert isinstance(config['reflex']['threshold'], int)
        assert isinstance(config['compliance']['audit_enabled'], bool)
    
    @pytest.mark.unit
    def test_extra_fields_allowed(self, tmp_path):
        """Verify extra/unknown fields don't break parsing."""
        config_with_extra = tmp_path / "extra.yaml"
        config_with_extra.write_text("""
device:
  name: test
  unknown_field: some_value
  another_unknown:
    nested: true
""")
        
        with open(config_with_extra) as f:
            config = yaml.safe_load(f)
        
        assert config['device']['name'] == "test"
        assert config['device']['unknown_field'] == "some_value"
    
    @pytest.mark.unit
    def test_config_roundtrip(self, sample_config, tmp_path):
        """Verify config survives YAML roundtrip."""
        config_path = tmp_path / "roundtrip.yaml"
        
        # Write
        with open(config_path, "w") as f:
            yaml.dump(sample_config, f)
        
        # Read back
        with open(config_path) as f:
            loaded = yaml.safe_load(f)
        
        assert loaded == sample_config
    
    @pytest.mark.unit
    def test_unicode_in_config(self, tmp_path):
        """Verify Unicode content is handled."""
        unicode_config = tmp_path / "unicode.yaml"
        unicode_config.write_text("""
device:
  name: "测试设备"
  description: "Ça fonctionne with émojis 🎉"
""", encoding='utf-8')
        
        with open(unicode_config, encoding='utf-8') as f:
            config = yaml.safe_load(f)
        
        assert "测试设备" in config['device']['name']
