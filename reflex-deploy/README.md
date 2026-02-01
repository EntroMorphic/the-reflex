# reflex-cli: The Reflex Deployment Tool

Deploy, validate, and monitor The Reflex CNS on ESP32 hardware.

## Installation

```bash
cd reflex-deploy
pip install -e .
```

## Quick Start

```bash
# Discover devices
reflex-cli scan

# Verify a device
reflex-cli verify /dev/ttyACM0

# Install firmware
reflex-cli install /dev/ttyACM0 --config configs/example_site.yaml

# Validate installation
reflex-cli validate /dev/ttyACM0

# Monitor device
reflex-cli monitor /dev/ttyACM0
```

## Commands

| Command | Description |
|---------|-------------|
| `scan` | Discover connected ESP32 devices |
| `verify` | Pre-flight hardware verification |
| `install` | Flash firmware and configuration |
| `validate` | Run post-installation test suite |
| `monitor` | Live telemetry monitoring |
| `health` | Quick device health check |
| `recover` | Attempt to recover bricked device |
| `safe` | Force device into safe mode |
| `update` | Check for firmware updates (placeholder) |

## Workflow

### 1. Pre-Flight (Scan + Verify)

```bash
# Find all devices
reflex-cli scan

# Verify specific device
reflex-cli verify /dev/ttyACM0 --chip ESP32-C6 --flash 2
```

### 2. Installation

```bash
# Create site config
cp configs/example_site.yaml configs/my_site.yaml
# Edit my_site.yaml...

# Install firmware
reflex-cli install /dev/ttyACM0 --config configs/my_site.yaml --user "john"
```

### 3. Validation

```bash
# Run test suite
reflex-cli validate /dev/ttyACM0 --save report.md
```

### 4. Monitoring

```bash
# Single device
reflex-cli monitor /dev/ttyACM0

# All devices
reflex-cli monitor --fleet

# Prometheus format
reflex-cli monitor --fleet --output prometheus
```

### 5. Recovery

```bash
# Quick health check
reflex-cli health /dev/ttyACM0

# Attempt recovery
reflex-cli recover /dev/ttyACM0 --verbose

# Force safe mode
reflex-cli safe /dev/ttyACM0
```

## Output Formats

All commands support multiple output formats:

- `--output yaml` (default for most commands)
- `--output json`
- `--output markdown`
- `--output prometheus` (monitor only)

## Configuration

Site configuration is YAML-based. See `configs/example_site.yaml` for full options.

Key sections:
- `device`: Name, role, description
- `reflex`: Threshold, hysteresis, safe values
- `watchdog`: Timeout settings for various watchdogs
- `telemetry`: Reporting interval and format
- `pins`: GPIO assignments
- `compliance`: Audit and logging settings

## Compliance

The tool maintains JSON audit logs for certification compliance:

- All installations logged with user, timestamp, checksums
- Config changes tracked
- Append-only audit file (`audit.json`)

## Architecture

```
Pre-Flight          Installation        Validation          Production
──────────          ────────────        ──────────          ──────────

  scan                backup              boot test           monitor
    │                   │                    │                   │
    ▼                   ▼                    ▼                   ▼
  verify              erase               self test           metrics
    │                   │                    │                   │
    ▼                   ▼                    ▼                   ▼
  manifest            flash               benchmark           alerts
                        │                    │
                        ▼                    ▼
                      verify              stress test
                        │                    │
                        ▼                    ▼
                      boot                 report
```

## Safe Mode / Recovery

The tool follows Voyager-inspired reliability principles:

1. **Hardware Watchdog**: Chip-level reset if software hangs
2. **Task Watchdog**: Catches hung FreeRTOS tasks
3. **Reflex Watchdog**: Catches stalled reflex loop
4. **Safe Mode**: Minimal known-good state on failure
5. **Rollback**: A/B partition scheme (future OTA)

Recovery commands:
```bash
reflex-cli health /dev/ttyACM0  # Quick check
reflex-cli safe /dev/ttyACM0    # Force safe mode
reflex-cli recover /dev/ttyACM0 # Full recovery attempt
```

## License

MIT License - EntroMorphic Research
