"""
Manifest generation for device discovery and installation.
"""

import json
import yaml
from dataclasses import dataclass, field
from datetime import datetime
from typing import List, Optional, Dict, Any
from pathlib import Path


@dataclass
class DeviceManifest:
    """Manifest of discovered devices."""
    timestamp: str
    host: str
    devices: List[Dict[str, Any]]
    
    def to_dict(self) -> dict:
        return {
            "manifest_type": "device_discovery",
            "timestamp": self.timestamp,
            "host": self.host,
            "device_count": len(self.devices),
            "devices": self.devices,
        }
    
    def to_json(self) -> str:
        return json.dumps(self.to_dict(), indent=2)
    
    def to_yaml(self) -> str:
        return yaml.dump(self.to_dict(), default_flow_style=False)
    
    def save(self, path: Path, format: str = "json") -> None:
        with open(path, "w") as f:
            if format == "json":
                f.write(self.to_json())
            elif format == "yaml":
                f.write(self.to_yaml())


@dataclass
class InstallManifest:
    """Manifest for firmware installation."""
    timestamp: str
    device_port: str
    device_chip_id: str
    firmware_version: str
    config_path: str
    steps: List[Dict[str, Any]] = field(default_factory=list)
    checksums: Dict[str, str] = field(default_factory=dict)
    status: str = "pending"
    error: Optional[str] = None
    
    def add_step(self, name: str, status: str, details: Optional[Dict] = None) -> None:
        self.steps.append({
            "name": name,
            "status": status,
            "timestamp": datetime.now().isoformat(),
            "details": details or {},
        })
    
    def to_dict(self) -> dict:
        return {
            "manifest_type": "installation",
            "timestamp": self.timestamp,
            "device": {
                "port": self.device_port,
                "chip_id": self.device_chip_id,
            },
            "firmware_version": self.firmware_version,
            "config_path": self.config_path,
            "steps": self.steps,
            "checksums": self.checksums,
            "status": self.status,
            "error": self.error,
        }
    
    def to_json(self) -> str:
        return json.dumps(self.to_dict(), indent=2)


@dataclass 
class ValidationReport:
    """Report from validation test suite."""
    timestamp: str
    device_port: str
    device_chip_id: str
    firmware_version: str
    tests: List[Dict[str, Any]] = field(default_factory=list)
    passed: int = 0
    failed: int = 0
    status: str = "pending"
    
    def add_test(self, name: str, passed: bool, 
                 measured: Optional[Dict] = None, 
                 expected: Optional[Dict] = None,
                 error: Optional[str] = None) -> None:
        self.tests.append({
            "name": name,
            "passed": passed,
            "timestamp": datetime.now().isoformat(),
            "measured": measured,
            "expected": expected,
            "error": error,
        })
        if passed:
            self.passed += 1
        else:
            self.failed += 1
    
    def finalize(self) -> None:
        self.status = "passed" if self.failed == 0 else "failed"
    
    def to_dict(self) -> dict:
        return {
            "report_type": "validation",
            "timestamp": self.timestamp,
            "device": {
                "port": self.device_port,
                "chip_id": self.device_chip_id,
            },
            "firmware_version": self.firmware_version,
            "summary": {
                "total": len(self.tests),
                "passed": self.passed,
                "failed": self.failed,
                "status": self.status,
            },
            "tests": self.tests,
        }
    
    def to_json(self) -> str:
        return json.dumps(self.to_dict(), indent=2)
    
    def to_markdown(self) -> str:
        lines = [
            f"# Validation Report",
            f"",
            f"**Date:** {self.timestamp}",
            f"**Device:** {self.device_port} ({self.device_chip_id})",
            f"**Firmware:** {self.firmware_version}",
            f"",
            f"## Summary",
            f"",
            f"| Metric | Value |",
            f"|--------|-------|",
            f"| Total Tests | {len(self.tests)} |",
            f"| Passed | {self.passed} |",
            f"| Failed | {self.failed} |",
            f"| Status | **{self.status.upper()}** |",
            f"",
            f"## Test Results",
            f"",
            f"| Test | Status | Details |",
            f"|------|--------|---------|",
        ]
        
        for test in self.tests:
            status = "✅" if test["passed"] else "❌"
            details = ""
            if test.get("measured"):
                details = ", ".join(f"{k}={v}" for k, v in test["measured"].items())
            if test.get("error"):
                details = test["error"]
            lines.append(f"| {test['name']} | {status} | {details} |")
        
        return "\n".join(lines)


@dataclass
class AuditLog:
    """Audit log for certification compliance."""
    entries: List[Dict[str, Any]] = field(default_factory=list)
    
    def log(self, action: str, device: str, user: str, 
            details: Optional[Dict] = None, status: str = "success") -> None:
        self.entries.append({
            "timestamp": datetime.now().isoformat(),
            "action": action,
            "device": device,
            "user": user,
            "status": status,
            "details": details or {},
        })
    
    def to_json(self) -> str:
        return json.dumps({"audit_log": self.entries}, indent=2)
    
    def save(self, path: Path) -> None:
        # Append-only for audit compliance
        with open(path, "a") as f:
            for entry in self.entries:
                f.write(json.dumps(entry) + "\n")
        self.entries = []  # Clear after save
