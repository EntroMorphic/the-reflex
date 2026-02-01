# PRD: reflex-cli Testing & Falsification

**Date:** February 1, 2026
**Author:** EntroMorphic Research
**Status:** Draft
**Version:** 0.1.0

---

## Executive Summary

The reflex-cli deployment tool claims to safely deploy, validate, and monitor ESP32-C6 devices running The Reflex. Before trusting it with production hardware, we must falsify these claims systematically.

**Scope:** 10 commands, ~2,000 LOC, targeting ESP32-C6 hardware

**Philosophy:** Same falsification mindset we applied to The Reflex primitive. If we can't break it, we can trust it.

---

## Current State

### What We Built

| Command | Purpose | LOC |
|---------|---------|-----|
| `scan` | Discover ESP32 devices | 112 |
| `verify` | Pre-flight hardware checks | 288 |
| `install` | Flash firmware | 300 |
| `validate` | Post-install test suite | 290 |
| `monitor` | Live telemetry | 400 |
| `health` | Quick status check | 50 |
| `recover` | Unbrick devices | 150 |
| `safe` | Force safe mode | 50 |
| `update` | OTA placeholder | 80 |
| `version` | Version info | 10 |

### Dependencies

- `typer` 0.17.4 - CLI framework
- `pyserial` 3.5 - Serial communication
- `esptool` 5.1.0 - ESP32 flashing
- `pyyaml` - Configuration parsing

### Hardware Target

- ESP32-C6 DevKit (×3 available)
- USB-C connection
- No WiFi/network (USB-C only for now)

---

## Test Categories

### Category 1: Unit Tests (No Hardware)

Test pure Python logic without serial connections.

| Test | What It Verifies | Priority |
|------|------------------|----------|
| Config parsing | YAML config loads correctly | High |
| Manifest generation | Device manifests serialize/deserialize | High |
| Output formatting | YAML/JSON/Markdown/Prometheus output | Medium |
| Checksum calculation | SHA256 matches known values | High |
| Telemetry parsing | Regex correctly extracts metrics | High |
| Audit log format | JSON audit entries are valid | Medium |
| Error handling | Graceful failures on bad input | High |

### Category 2: Mock Tests (Simulated Hardware)

Test serial communication logic with mock serial port.

| Test | What It Verifies | Priority |
|------|------------------|----------|
| Serial open/close | Connection lifecycle works | High |
| DTR/RTS toggle | Reset sequence is correct | High |
| Read timeout | Doesn't hang on no data | High |
| Write command | Commands sent correctly | Medium |
| Buffer overflow | Handles large data streams | Medium |
| Partial data | Handles incomplete reads | Medium |

### Category 3: Integration Tests (Real Hardware)

Test against actual ESP32-C6 devices.

| Test | What It Verifies | Priority |
|------|------------------|----------|
| Device discovery | `scan` finds connected C6s | Critical |
| Chip identification | `verify` correctly identifies ESP32-C6 | Critical |
| Flash size detection | Reports correct flash size | High |
| Bootloader entry | Can enter download mode | Critical |
| Firmware flash | `install` successfully flashes | Critical |
| Boot verification | Device boots after flash | Critical |
| Benchmark parsing | `validate` extracts latency numbers | High |
| Live monitoring | `monitor` receives telemetry | High |
| Recovery | `recover` can reset stuck device | High |

### Category 4: Adversarial Tests (Break It)

Intentionally try to break the tool.

| Test | Attack Vector | Expected Behavior |
|------|---------------|-------------------|
| Unplug during flash | Remove USB mid-write | Graceful failure, no corruption |
| Wrong device | Target non-ESP32 device | Reject with clear error |
| Corrupt config | Malformed YAML | Parse error, don't proceed |
| Missing firmware | Non-existent firmware path | Error before flash |
| Permission denied | No dialout group access | Clear permission error |
| Device busy | Serial port locked | Detect and report |
| Rapid reconnect | Unplug/replug during operation | Handle reconnection |
| Out of space | Fill flash completely | Detect and report |
| Wrong chip | ESP32-S3 instead of C6 | Reject with warning |
| Timeout storm | Device not responding | Timeout gracefully |

### Category 5: Compliance Tests

Verify audit and compliance features.

| Test | What It Verifies | Priority |
|------|------------------|----------|
| Audit log creation | JSON file created on install | High |
| Audit log append | Entries append, don't overwrite | High |
| Timestamp accuracy | Timestamps are ISO8601 | Medium |
| User tracking | User field populated | Medium |
| Checksum logging | SHA256 recorded for all binaries | High |
| Config logging | Config hash recorded | Medium |

---

## Test Infrastructure

### Directory Structure

```
reflex-deploy/
├── tests/
│   ├── __init__.py
│   ├── conftest.py              # Pytest fixtures
│   ├── test_unit/
│   │   ├── test_config.py       # Config parsing
│   │   ├── test_manifest.py     # Manifest serialization
│   │   ├── test_output.py       # Output formatting
│   │   ├── test_checksum.py     # Checksum calculation
│   │   ├── test_telemetry.py    # Telemetry parsing
│   │   └── test_audit.py        # Audit log format
│   ├── test_mock/
│   │   ├── test_serial.py       # Mock serial communication
│   │   └── test_esptool.py      # Mock esptool commands
│   ├── test_integration/
│   │   ├── test_scan.py         # Real device discovery
│   │   ├── test_verify.py       # Real device verification
│   │   ├── test_install.py      # Real firmware flash
│   │   ├── test_validate.py     # Real validation suite
│   │   └── test_monitor.py      # Real monitoring
│   └── test_adversarial/
│       ├── test_disconnect.py   # Unplug scenarios
│       ├── test_wrong_device.py # Wrong hardware
│       └── test_timeout.py      # Timeout handling
└── pytest.ini
```

### Pytest Configuration

```ini
# pytest.ini
[pytest]
testpaths = tests
markers =
    unit: Unit tests (no hardware)
    mock: Mock serial tests
    hardware: Requires real ESP32 hardware
    adversarial: Intentionally tries to break things
    slow: Tests that take >10 seconds

# Run unit tests only (CI safe)
# pytest -m unit

# Run with hardware
# pytest -m hardware

# Run everything
# pytest
```

### Fixtures

```python
# conftest.py
import pytest
from unittest.mock import Mock, patch

@pytest.fixture
def mock_serial():
    """Mock serial port for testing."""
    with patch('serial.Serial') as mock:
        mock_instance = Mock()
        mock_instance.read.return_value = b""
        mock_instance.in_waiting = 0
        mock.return_value = mock_instance
        yield mock_instance

@pytest.fixture
def sample_config():
    """Sample site configuration."""
    return {
        "device": {"name": "test-c6", "role": "spine"},
        "reflex": {"threshold": 5000, "hysteresis": 100},
        "pins": {"led": 8, "sensor": 0},
    }

@pytest.fixture
def sample_telemetry():
    """Sample telemetry output from device."""
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
    """

@pytest.fixture
def real_device():
    """Real ESP32-C6 device (skip if not connected)."""
    from reflex_cli.util import find_esp32_devices
    devices = find_esp32_devices()
    if not devices:
        pytest.skip("No ESP32 devices connected")
    return devices[0]
```

---

## Test Implementation Plan

### Phase 1: Unit Tests (Day 1)

**Goal:** 100% coverage of pure Python logic

1. **test_config.py**
   - Load valid YAML config
   - Reject malformed YAML
   - Handle missing required fields
   - Handle extra unknown fields

2. **test_manifest.py**
   - Create device manifest
   - Serialize to JSON
   - Serialize to YAML
   - Deserialize and round-trip

3. **test_telemetry.py**
   - Parse signal count from output
   - Parse anomaly count
   - Parse min/max/avg latency
   - Handle missing fields
   - Handle corrupt output

4. **test_checksum.py**
   - SHA256 of known file
   - Empty file
   - Large file

5. **test_output.py**
   - YAML output format
   - JSON output format
   - Markdown table format
   - Prometheus metrics format

### Phase 2: Mock Tests (Day 1-2)

**Goal:** Test serial logic without hardware

1. **test_serial.py**
   - Open/close connection
   - Reset via DTR toggle
   - Read with timeout
   - Send command and read response
   - Handle serial exceptions

2. **test_esptool.py**
   - Mock chip_id command
   - Mock flash_id command
   - Mock write_flash
   - Handle command timeout
   - Handle command failure

### Phase 3: Integration Tests (Day 2-3)

**Goal:** Verify against real hardware

**Prerequisites:**
- At least 1 ESP32-C6 connected
- User in dialout group
- Known-good firmware available

1. **test_scan.py**
   - Find connected devices
   - Probe for chip info
   - Generate manifest

2. **test_verify.py**
   - Run pre-flight checks
   - All checks pass on good device
   - Correct chip type detected
   - Flash size detected

3. **test_validate.py**
   - Boot test passes
   - Benchmark numbers extracted
   - Latency within expected range

4. **test_monitor.py**
   - Receive telemetry
   - Parse metrics correctly
   - Heartbeat detected

### Phase 4: Adversarial Tests (Day 3)

**Goal:** Try to break things

1. **test_disconnect.py** (manual intervention required)
   - Start long operation
   - Unplug device
   - Verify graceful failure

2. **test_wrong_device.py**
   - Target non-ESP32 serial device
   - Verify rejection

3. **test_timeout.py**
   - Device in bootloader (no response)
   - Verify timeout handling

---

## Success Criteria

### Unit Tests
- [ ] 100% of pure Python functions covered
- [ ] All edge cases handled
- [ ] No crashes on malformed input

### Mock Tests
- [ ] Serial logic works without hardware
- [ ] Timeouts behave correctly
- [ ] Exceptions handled gracefully

### Integration Tests
- [ ] `scan` finds all connected devices
- [ ] `verify` correctly identifies ESP32-C6
- [ ] `validate` extracts benchmark numbers
- [ ] `monitor` receives and parses telemetry
- [ ] All output formats work (YAML/JSON/Markdown/Prometheus)

### Adversarial Tests
- [ ] Unplug during flash doesn't corrupt
- [ ] Wrong device rejected clearly
- [ ] Timeout doesn't hang forever
- [ ] Permission errors reported clearly

### Compliance Tests
- [ ] Audit log created and appended
- [ ] Timestamps accurate
- [ ] Checksums recorded

---

## Falsification Checklist

| Claim | Test | Status |
|-------|------|--------|
| "Discovers ESP32 devices" | test_scan.py | ✅ PASSED (9/9) |
| "Verifies hardware before flash" | test_verify.py | ✅ PASSED (11/11) |
| "Safely flashes firmware" | test_install.py | ⬜ Not tested (no firmware binary) |
| "Validates installation" | test_validate.py | ⬜ Not tested |
| "Monitors live telemetry" | test_monitor.py | ✅ PASSED (10/10) |
| "Recovers bricked devices" | test_recover.py | ⬜ Not tested |
| "Handles disconnects gracefully" | test_disconnect.py | ⬜ Planned |
| "Rejects wrong hardware" | test_wrong_device.py | ⬜ Planned |
| "Maintains audit trail" | test_audit.py | ⬜ Not tested |

## Test Results (February 1, 2026)

**Total: 97 tests passing**

| Category | Tests | Status |
|----------|-------|--------|
| Unit | 36 | ✅ All passing |
| Mock | 31 | ✅ All passing |
| Integration (scan) | 9 | ✅ All passing |
| Integration (verify) | 11 | ✅ All passing |
| Integration (monitor) | 10 | ✅ All passing |

### Hardware Verified

| Device | Port | Chip | Flash | MAC |
|--------|------|------|-------|-----|
| C6 #1 | /dev/ttyACM0 | ESP32-C6FH4 | 4MB | b4:3a:45:ff:fe:8a:c4:d4 |
| C6 #2 | /dev/ttyACM1 | ESP32-C6FH4 | 4MB | b4:3a:45:ff:fe:8a:c7:d4 |
| C6 #3 | /dev/ttyACM2 | ESP32-C6FH4 | 4MB | b4:3a:45:ff:fe:8a:c8:24 |

### Benchmark Output (C6 #1)

```
TRUE REFLEX LATENCY
Min:   13 cycles =   81 ns
Max:  345 cycles = 2156 ns
Avg:   14 cycles =   87 ns
```

### Bugs Found & Fixed

1. **Telemetry regex** - Wasn't parsing `Min: 14 cycles = 87 ns` format
2. **esptool 5.x** - Output format changed (`Chip type:` vs `Chip is`)
3. **EUI-64 MAC** - C6 uses 8-byte MAC, test expected 6-byte

---

## Timeline

| Day | Focus | Deliverable |
|-----|-------|-------------|
| Day 1 | Unit + Mock tests | tests/test_unit/, tests/test_mock/ |
| Day 2 | Integration tests | tests/test_integration/ |
| Day 3 | Adversarial tests | tests/test_adversarial/ |
| Day 4 | Bug fixes + docs | All tests passing, coverage report |

---

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| No hardware available | Can't run integration tests | Mock tests cover logic |
| Device bricked during testing | Lost test hardware | Have 3 devices, backup firmware |
| esptool API changes | Tests break | Pin esptool version |
| Serial permission issues | Tests fail on CI | Document dialout requirement |

---

## Open Questions

1. **CI/CD:** Should unit tests run on every commit? (Recommendation: Yes)
2. **Hardware CI:** Can we add a self-hosted runner with C6 attached? (Future)
3. **Coverage target:** What's the minimum coverage? (Recommendation: 80% for unit)
4. **Flaky tests:** How to handle timing-dependent tests? (Retry with backoff)

---

## Appendix: Sample Test Cases

### test_telemetry.py (Example)

```python
import pytest
from reflex_cli.monitor import parse_telemetry

def test_parse_signal_count(sample_telemetry):
    result = parse_telemetry(sample_telemetry)
    assert result['signal_count'] == 5009

def test_parse_anomaly_count(sample_telemetry):
    result = parse_telemetry(sample_telemetry)
    assert result['anomaly_count'] == 2532

def test_parse_latency(sample_telemetry):
    result = parse_telemetry(sample_telemetry)
    assert result['reflex_avg_ns'] == 87

def test_parse_empty_output():
    result = parse_telemetry("")
    assert result == {}

def test_parse_corrupt_output():
    result = parse_telemetry("garbage data xyz123")
    assert result == {}
```

### test_scan.py (Example)

```python
import pytest
from reflex_cli.scan import scan_and_probe, format_device_table

@pytest.mark.hardware
def test_scan_finds_devices(real_device):
    """Verify scan finds at least one device."""
    from reflex_cli.util import find_esp32_devices
    devices = find_esp32_devices()
    assert len(devices) >= 1

@pytest.mark.hardware
def test_probe_gets_chip_info(real_device):
    """Verify probe retrieves chip information."""
    from reflex_cli.scan import probe_device
    probed = probe_device(real_device)
    assert probed.chip_type is not None
    assert "C6" in probed.chip_type or "c6" in probed.chip_type.lower()

@pytest.mark.unit
def test_format_empty_table():
    """Verify empty device list handling."""
    result = format_device_table([])
    assert "No devices found" in result
```

---

*"If we can't break it, we can trust it."*

*— February 1, 2026*
