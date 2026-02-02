"""
reflex-cli: The Reflex Deployment Tool

Main CLI entry point using Typer.
"""

import typer
from typing import Optional, List
from pathlib import Path
from datetime import datetime
import json

from . import __version__
from .scan import scan_and_probe, generate_manifest, format_device_table
from .verify import verify_device
from .validate import validate_device
from .monitor import monitor_single, FleetMonitor
from .install import install_firmware, load_config
from .recover import attempt_recovery, check_device_health, force_safe_mode
from .update import get_update_status, OTANotImplementedError
from .util import find_esp32_devices, get_chip_info, AuditLog


app = typer.Typer(
    name="reflex-cli",
    help="The Reflex Deployment Tool - Deploy, validate, and monitor CNS on ESP32",
    add_completion=False,
)


def print_header():
    """Print CLI header."""
    typer.echo("")
    typer.echo("═══════════════════════════════════════════════════════════")
    typer.echo("           reflex-cli - The Reflex Deployment Tool")
    typer.echo(f"                       v{__version__}")
    typer.echo("═══════════════════════════════════════════════════════════")
    typer.echo("")


def select_devices(devices: list, select_count: Optional[int] = None) -> list:
    """Interactive device selection."""
    if not devices:
        typer.echo("No devices found.")
        raise typer.Exit(1)
    
    typer.echo("\nDiscovered devices:")
    typer.echo("")
    for i, d in enumerate(devices, 1):
        chip = d.chip_type or "unknown"
        typer.echo(f"  [{i}] {d.port} - {chip}")
    typer.echo("")
    
    if select_count is None:
        selection = typer.prompt(
            "Select devices (comma-separated numbers, or 'all')",
            default="all"
        )
    else:
        selection = typer.prompt(
            f"Select {select_count} device(s) (comma-separated numbers)",
        )
    
    if selection.lower() == "all":
        return devices
    
    try:
        indices = [int(x.strip()) - 1 for x in selection.split(",")]
        return [devices[i] for i in indices if 0 <= i < len(devices)]
    except (ValueError, IndexError):
        typer.echo("Invalid selection.")
        raise typer.Exit(1)


@app.command()
def scan(
    probe: bool = typer.Option(True, "--probe/--no-probe", help="Probe devices for details"),
    output: str = typer.Option("table", "--output", "-o", help="Output format: table, json, yaml"),
    save: Optional[Path] = typer.Option(None, "--save", "-s", help="Save manifest to file"),
    verbose: bool = typer.Option(False, "--verbose", "-v", help="Verbose output"),
):
    """Discover connected ESP32 devices."""
    print_header()
    typer.echo("Scanning for devices...")
    typer.echo("")
    
    if probe:
        devices = scan_and_probe(verbose)
    else:
        devices = find_esp32_devices()
    
    if not devices:
        typer.echo("No ESP32 devices found.")
        typer.echo("")
        typer.echo("Troubleshooting:")
        typer.echo("  - Check USB connections")
        typer.echo("  - Verify device is powered on")
        typer.echo("  - Check permissions (user in 'dialout' group?)")
        raise typer.Exit(1)
    
    typer.echo(f"Found {len(devices)} device(s):")
    typer.echo("")
    
    if output == "table":
        typer.echo(format_device_table(devices))
    elif output == "json":
        manifest = generate_manifest(devices)
        typer.echo(manifest.to_json())
    elif output == "yaml":
        manifest = generate_manifest(devices)
        typer.echo(manifest.to_yaml())
    
    if save:
        manifest = generate_manifest(devices)
        manifest.save(save, format="json" if save.suffix == ".json" else "yaml")
        typer.echo(f"\nManifest saved to {save}")


@app.command()
def verify(
    device: str = typer.Argument(..., help="Device port (e.g., /dev/ttyACM0)"),
    chip: str = typer.Option("ESP32-C6", "--chip", "-c", help="Expected chip type"),
    flash: int = typer.Option(2, "--flash", "-f", help="Minimum flash size in MB"),
    output: str = typer.Option("markdown", "--output", "-o", help="Output format: markdown, json"),
):
    """Run pre-flight verification on a device."""
    print_header()
    typer.echo(f"Verifying device: {device}")
    typer.echo("")
    
    report = verify_device(device, expected_chip=chip, min_flash_mb=flash)
    
    if output == "markdown":
        typer.echo(report.to_markdown())
    elif output == "json":
        typer.echo(json.dumps({
            "port": report.port,
            "timestamp": report.timestamp,
            "status": report.overall_status,
            "checks": [
                {"name": c.name, "passed": c.passed, "message": c.message}
                for c in report.checks
            ],
        }, indent=2))


@app.command()
def install(
    device: str = typer.Argument(..., help="Device port (e.g., /dev/ttyACM0)"),
    config: Path = typer.Option(..., "--config", "-c", help="Path to site config YAML"),
    firmware: Path = typer.Option(Path("firmware"), "--firmware", "-f", help="Firmware directory"),
    no_backup: bool = typer.Option(False, "--no-backup", help="Skip firmware backup"),
    backup_dir: Path = typer.Option(Path("backups"), "--backup-dir", "-b", help="Backup directory"),
    user: str = typer.Option("unknown", "--user", "-u", help="User performing installation"),
):
    """Install firmware on a device."""
    print_header()
    typer.echo(f"Installing firmware on: {device}")
    typer.echo(f"Config: {config}")
    typer.echo("")
    
    if not config.exists():
        typer.echo(f"Error: Config file not found: {config}")
        raise typer.Exit(1)
    
    if not firmware.exists():
        typer.echo(f"Error: Firmware directory not found: {firmware}")
        raise typer.Exit(1)
    
    # Create backup directory
    if not no_backup:
        backup_dir.mkdir(parents=True, exist_ok=True)
    
    # Initialize audit log
    audit_log = AuditLog()
    
    manifest = install_firmware(
        port=device,
        config_path=config,
        firmware_dir=firmware,
        backup=not no_backup,
        backup_dir=backup_dir,
        audit_log=audit_log,
        user=user,
    )
    
    # Display results
    typer.echo("Installation Steps:")
    typer.echo("")
    for step in manifest.steps:
        status = "✅" if step["status"] == "passed" else "❌" if step["status"] == "failed" else "⏭️"
        typer.echo(f"  {status} {step['name']}")
    
    typer.echo("")
    if manifest.status == "success":
        typer.echo("✅ Installation successful!")
    else:
        typer.echo(f"❌ Installation failed: {manifest.error}")
        raise typer.Exit(1)
    
    # Save audit log
    audit_path = Path("audit.json")
    audit_log.save(audit_path)
    typer.echo(f"\nAudit log appended to: {audit_path}")


@app.command()
def validate(
    device: str = typer.Argument(..., help="Device port (e.g., /dev/ttyACM0)"),
    output: str = typer.Option("markdown", "--output", "-o", help="Output format: markdown, json"),
    save: Optional[Path] = typer.Option(None, "--save", "-s", help="Save report to file"),
):
    """Run validation test suite on a device."""
    print_header()
    typer.echo(f"Validating device: {device}")
    typer.echo("Running test suite...")
    typer.echo("")
    
    report = validate_device(device)
    
    if output == "markdown":
        result = report.to_markdown()
    else:
        result = report.to_json()
    
    typer.echo(result)
    
    if save:
        with open(save, "w") as f:
            f.write(result)
        typer.echo(f"\nReport saved to: {save}")
    
    if report.status == "failed":
        raise typer.Exit(1)


@app.command()
def monitor(
    device: Optional[str] = typer.Argument(None, help="Device port (or omit for all)"),
    duration: Optional[float] = typer.Option(None, "--duration", "-d", help="Monitor duration in seconds"),
    output: str = typer.Option("yaml", "--output", "-o", help="Output format: yaml, json, markdown, prometheus"),
    fleet: bool = typer.Option(False, "--fleet", help="Monitor all discovered devices"),
):
    """Monitor device(s) in real-time."""
    print_header()
    
    if fleet or device is None:
        devices = find_esp32_devices()
        if not devices:
            typer.echo("No devices found.")
            raise typer.Exit(1)
        
        typer.echo(f"Monitoring {len(devices)} device(s)...")
        typer.echo("")
        
        fm = FleetMonitor([d.port for d in devices])
        fm.start_all()
        
        try:
            import time
            if duration:
                time.sleep(duration)
            else:
                time.sleep(5)  # Default snapshot duration
        finally:
            fm.stop_all()
        
        if output == "yaml":
            typer.echo(fm.to_yaml())
        elif output == "markdown":
            typer.echo(fm.to_markdown())
        elif output == "prometheus":
            typer.echo(fm.to_prometheus())
        elif output == "json":
            import json
            typer.echo(json.dumps(
                {"devices": [m.to_dict() for m in fm.get_all_metrics()]},
                indent=2
            ))
    else:
        typer.echo(f"Monitoring: {device}")
        typer.echo("")
        result = monitor_single(device, output_format=output, duration=duration)
        typer.echo(result)


@app.command()
def recover(
    device: str = typer.Argument(..., help="Device port (e.g., /dev/ttyACM0)"),
    verbose: bool = typer.Option(False, "--verbose", "-v", help="Verbose output"),
):
    """Attempt to recover a bricked device."""
    print_header()
    typer.echo(f"Attempting recovery on: {device}")
    typer.echo("")
    
    success, message = attempt_recovery(device, verbose)
    typer.echo(message)
    
    if not success:
        raise typer.Exit(1)


@app.command()
def safe(
    device: str = typer.Argument(..., help="Device port (e.g., /dev/ttyACM0)"),
):
    """Force device into safe mode."""
    print_header()
    typer.echo(f"Forcing safe mode on: {device}")
    typer.echo("")
    
    success, message = force_safe_mode(device)
    typer.echo(message)
    
    if not success:
        raise typer.Exit(1)


@app.command()
def health(
    device: str = typer.Argument(..., help="Device port (e.g., /dev/ttyACM0)"),
):
    """Quick health check on a device."""
    print_header()
    
    status, message = check_device_health(device)
    
    status_icons = {
        "healthy": "✅",
        "degraded": "⚠️",
        "unresponsive": "❌",
        "bricked": "💀",
    }
    
    typer.echo(f"Device: {device}")
    typer.echo(f"Status: {status_icons.get(status, '?')} {status.upper()}")
    typer.echo(f"Detail: {message}")


@app.command()
def update():
    """Check for firmware updates (placeholder)."""
    print_header()
    typer.echo(get_update_status())


@app.command()
def hologram(
    action: str = typer.Argument(..., help="Action: deploy, monitor, status"),
    node_id: Optional[int] = typer.Option(None, "--node", "-n", help="Node ID (1-3) for deploy"),
    device: Optional[str] = typer.Option(None, "--device", "-d", help="Specific device port"),
    output: str = typer.Option("yaml", "--output", "-o", help="Output format: yaml, json"),
):
    """Holographic Intelligence mesh operations.
    
    Actions:
      deploy  - Build and flash hologram firmware to mesh nodes
      monitor - Monitor holographic mesh (alias for monitor --fleet)
      status  - Quick status of all mesh nodes
    """
    from pathlib import Path
    import subprocess
    import os
    
    print_header()
    
    reflex_os_dir = Path(__file__).parent.parent.parent / "reflex-os"
    firmware_dir = Path(__file__).parent.parent / "firmware"
    
    if action == "deploy":
        typer.echo("╔═══════════════════════════════════════════════════════════╗")
        typer.echo("║           HOLOGRAPHIC INTELLIGENCE DEPLOYMENT             ║")
        typer.echo("╚═══════════════════════════════════════════════════════════╝")
        typer.echo("")
        
        # Find devices
        devices = find_esp32_devices()
        if not devices:
            typer.echo("No ESP32 devices found!")
            raise typer.Exit(1)
        
        typer.echo(f"Found {len(devices)} device(s):")
        for i, d in enumerate(devices):
            typer.echo(f"  [{i+1}] {d.port}")
        typer.echo("")
        
        if device:
            # Flash specific device with specific node ID
            if node_id is None:
                typer.echo("Error: --node required when using --device")
                raise typer.Exit(1)
            deploy_targets = [(device, node_id)]
        else:
            # Flash all devices with sequential node IDs
            deploy_targets = [(d.port, i+1) for i, d in enumerate(devices[:3])]
        
        # Source ESP-IDF
        esp_idf_path = os.path.expanduser("~/esp/v5.4/export.sh")
        
        for port, nid in deploy_targets:
            typer.echo(f"═══ Deploying NODE {nid} to {port} ═══")
            
            # Update NODE_ID in source
            demo_file = reflex_os_dir / "main" / "hologram_mesh_demo.c"
            with open(demo_file, 'r') as f:
                content = f.read()
            
            import re
            content = re.sub(
                r'#define NODE_ID\s+\d+',
                f'#define NODE_ID             {nid}',
                content
            )
            
            with open(demo_file, 'w') as f:
                f.write(content)
            
            typer.echo(f"  - Set NODE_ID = {nid}")
            
            # Build
            typer.echo("  - Building firmware...")
            result = subprocess.run(
                f"source {esp_idf_path} 2>/dev/null && cd {reflex_os_dir} && idf.py build",
                shell=True, capture_output=True, text=True, executable="/bin/bash"
            )
            if result.returncode != 0:
                typer.echo(f"  ✗ Build failed!")
                typer.echo(result.stderr[-500:] if result.stderr else "Unknown error")
                continue
            typer.echo("  - Build complete")
            
            # Flash
            typer.echo(f"  - Flashing to {port}...")
            result = subprocess.run(
                f"source {esp_idf_path} 2>/dev/null && cd {reflex_os_dir} && idf.py -p {port} flash",
                shell=True, capture_output=True, text=True, executable="/bin/bash"
            )
            if result.returncode != 0:
                typer.echo(f"  ✗ Flash failed!")
                continue
            typer.echo(f"  ✓ NODE {nid} deployed to {port}")
            typer.echo("")
        
        # Copy final firmware to firmware dir
        typer.echo("Copying firmware to deployment directory...")
        import shutil
        shutil.copy(reflex_os_dir / "build" / "bootloader" / "bootloader.bin", firmware_dir)
        shutil.copy(reflex_os_dir / "build" / "partition_table" / "partition-table.bin", firmware_dir)
        shutil.copy(reflex_os_dir / "build" / "reflex_os.bin", firmware_dir)
        
        typer.echo("")
        typer.echo("╔═══════════════════════════════════════════════════════════╗")
        typer.echo("║                  DEPLOYMENT COMPLETE                      ║")
        typer.echo("╚═══════════════════════════════════════════════════════════╝")
        typer.echo("")
        typer.echo("Run 'reflex-cli hologram monitor' to observe the mesh.")
        
    elif action == "monitor":
        from .monitor import FleetMonitor
        
        typer.echo("╔═══════════════════════════════════════════════════════════╗")
        typer.echo("║           HOLOGRAPHIC MESH MONITOR                        ║")
        typer.echo("╚═══════════════════════════════════════════════════════════╝")
        typer.echo("")
        
        devices = find_esp32_devices()
        if not devices:
            typer.echo("No devices found.")
            raise typer.Exit(1)
        
        ports = [d.port for d in devices]
        fleet = FleetMonitor(ports)
        fleet.start_all()
        
        import time
        try:
            while True:
                time.sleep(5)
                metrics = fleet.get_all_metrics()
                
                typer.echo("─" * 60)
                for m in metrics:
                    if m.firmware_type == "hologram":
                        icon = "🧠" if m.neighbors > 0 else "🔴"
                        typer.echo(
                            f"{icon} Node {m.node_id} ({m.port}): "
                            f"neighbors={m.neighbors} "
                            f"conf={m.confidence:.1f}% "
                            f"rx={m.radio_rx} "
                            f"rssi={m.rssi}dBm"
                        )
                    else:
                        typer.echo(f"📦 {m.port}: {m.status}")
        except KeyboardInterrupt:
            fleet.stop_all()
            typer.echo("\nMonitor stopped.")
    
    elif action == "status":
        typer.echo("Holographic Mesh Status:")
        typer.echo("")
        
        devices = find_esp32_devices()
        if not devices:
            typer.echo("No devices found.")
            raise typer.Exit(1)
        
        from .monitor import FleetMonitor
        import time
        
        ports = [d.port for d in devices]
        fleet = FleetMonitor(ports)
        fleet.start_all()
        time.sleep(3)  # Gather data
        
        metrics = fleet.get_all_metrics()
        fleet.stop_all()
        
        if output == "yaml":
            import yaml
            typer.echo(yaml.dump({"nodes": [m.to_dict() for m in metrics]}, default_flow_style=False))
        else:
            typer.echo(json.dumps({"nodes": [m.to_dict() for m in metrics]}, indent=2))
    
    else:
        typer.echo(f"Unknown action: {action}")
        typer.echo("Valid actions: deploy, monitor, status")
        raise typer.Exit(1)


@app.command()
def version():
    """Show version information."""
    typer.echo(f"reflex-cli v{__version__}")


def main():
    """Main entry point."""
    app()


if __name__ == "__main__":
    main()
