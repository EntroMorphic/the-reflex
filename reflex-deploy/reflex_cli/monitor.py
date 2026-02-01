"""
Live monitoring of Reflex devices.
"""

import time
import re
import json
import yaml
from datetime import datetime
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Any
from threading import Thread, Event

from .util import (
    open_serial,
    reset_device,
)


@dataclass
class DeviceMetrics:
    """Metrics for a single device."""
    port: str
    timestamp: str = ""
    heartbeat: bool = False
    uptime_s: int = 0
    reflex_min_ns: int = 0
    reflex_max_ns: int = 0
    reflex_avg_ns: int = 0
    anomaly_count: int = 0
    signal_count: int = 0
    temperature_c: float = 0.0
    free_heap: int = 0
    last_error: str = ""
    status: str = "unknown"
    
    def to_dict(self) -> dict:
        return {
            "port": self.port,
            "timestamp": self.timestamp,
            "heartbeat": self.heartbeat,
            "uptime_s": self.uptime_s,
            "reflex": {
                "min_ns": self.reflex_min_ns,
                "max_ns": self.reflex_max_ns,
                "avg_ns": self.reflex_avg_ns,
            },
            "anomaly_count": self.anomaly_count,
            "signal_count": self.signal_count,
            "temperature_c": self.temperature_c,
            "free_heap": self.free_heap,
            "last_error": self.last_error,
            "status": self.status,
        }
    
    def to_yaml(self) -> str:
        return yaml.dump(self.to_dict(), default_flow_style=False)
    
    def to_prometheus(self) -> str:
        """Format as Prometheus metrics."""
        port_label = self.port.replace("/", "_")
        lines = [
            f'reflex_heartbeat{{port="{port_label}"}} {1 if self.heartbeat else 0}',
            f'reflex_uptime_seconds{{port="{port_label}"}} {self.uptime_s}',
            f'reflex_latency_min_ns{{port="{port_label}"}} {self.reflex_min_ns}',
            f'reflex_latency_max_ns{{port="{port_label}"}} {self.reflex_max_ns}',
            f'reflex_latency_avg_ns{{port="{port_label}"}} {self.reflex_avg_ns}',
            f'reflex_anomaly_count{{port="{port_label}"}} {self.anomaly_count}',
            f'reflex_signal_count{{port="{port_label}"}} {self.signal_count}',
            f'reflex_temperature_celsius{{port="{port_label}"}} {self.temperature_c}',
            f'reflex_free_heap_bytes{{port="{port_label}"}} {self.free_heap}',
        ]
        return "\n".join(lines)
    
    def to_markdown(self) -> str:
        """Format as markdown table."""
        status_icon = "✅" if self.status == "ok" else "⚠️" if self.status == "warning" else "❌"
        
        return f"""## {self.port}

| Metric | Value |
|--------|-------|
| Status | {status_icon} {self.status} |
| Heartbeat | {'Yes' if self.heartbeat else 'No'} |
| Uptime | {self.uptime_s}s |
| Reflex Avg | {self.reflex_avg_ns} ns |
| Reflex Max | {self.reflex_max_ns} ns |
| Signals | {self.signal_count} |
| Anomalies | {self.anomaly_count} |
| Last Error | {self.last_error or 'None'} |

*Updated: {self.timestamp}*
"""


def parse_telemetry(text: str) -> Dict[str, Any]:
    """Parse telemetry data from device output."""
    metrics = {}
    
    # Parse signal/anomaly counts
    # Looking for: "Signals:       5009" and "Anomalies:     2532"
    signals_match = re.search(r'Signals:\s*(\d+)', text)
    if signals_match:
        metrics['signal_count'] = int(signals_match.group(1))
    
    anomalies_match = re.search(r'Anomalies:\s*(\d+)', text)
    if anomalies_match:
        metrics['anomaly_count'] = int(anomalies_match.group(1))
    
    # Parse latency
    min_match = re.search(r'Min:\s*(\d+)\s*ns', text)
    if min_match:
        metrics['reflex_min_ns'] = int(min_match.group(1))
    
    max_match = re.search(r'Max:\s*(\d+)\s*ns', text)
    if max_match:
        metrics['reflex_max_ns'] = int(max_match.group(1))
    
    avg_match = re.search(r'Avg:\s*(\d+)\s*ns', text)
    if avg_match:
        metrics['reflex_avg_ns'] = int(avg_match.group(1))
    
    # Check for errors
    if 'error' in text.lower() or 'fault' in text.lower():
        metrics['last_error'] = "Error detected in output"
    
    return metrics


class DeviceMonitor:
    """Monitor a single device."""
    
    def __init__(self, port: str):
        self.port = port
        self.metrics = DeviceMetrics(port=port)
        self.running = False
        self.thread: Optional[Thread] = None
        self.stop_event = Event()
    
    def _monitor_loop(self):
        """Main monitoring loop."""
        try:
            ser = open_serial(self.port)
        except Exception as e:
            self.metrics.status = "error"
            self.metrics.last_error = str(e)
            return
        
        buffer = ""
        last_data_time = time.time()
        
        while not self.stop_event.is_set():
            try:
                # Read available data
                if ser.in_waiting:
                    chunk = ser.read(ser.in_waiting)
                    buffer += chunk.decode('utf-8', errors='ignore')
                    last_data_time = time.time()
                    self.metrics.heartbeat = True
                    
                    # Parse telemetry from buffer
                    parsed = parse_telemetry(buffer)
                    
                    if parsed.get('signal_count'):
                        self.metrics.signal_count = parsed['signal_count']
                    if parsed.get('anomaly_count'):
                        self.metrics.anomaly_count = parsed['anomaly_count']
                    if parsed.get('reflex_min_ns'):
                        self.metrics.reflex_min_ns = parsed['reflex_min_ns']
                    if parsed.get('reflex_max_ns'):
                        self.metrics.reflex_max_ns = parsed['reflex_max_ns']
                    if parsed.get('reflex_avg_ns'):
                        self.metrics.reflex_avg_ns = parsed['reflex_avg_ns']
                    if parsed.get('last_error'):
                        self.metrics.last_error = parsed['last_error']
                    
                    # Keep buffer manageable
                    if len(buffer) > 10000:
                        buffer = buffer[-5000:]
                
                # Check heartbeat timeout
                if time.time() - last_data_time > 5:
                    self.metrics.heartbeat = False
                
                # Update timestamp
                self.metrics.timestamp = datetime.now().isoformat()
                
                # Determine status
                if not self.metrics.heartbeat:
                    self.metrics.status = "offline"
                elif self.metrics.last_error:
                    self.metrics.status = "error"
                elif self.metrics.reflex_avg_ns > 1000:
                    self.metrics.status = "warning"
                else:
                    self.metrics.status = "ok"
                
                time.sleep(0.1)
                
            except Exception as e:
                self.metrics.last_error = str(e)
                self.metrics.status = "error"
                time.sleep(1)
        
        ser.close()
    
    def start(self):
        """Start monitoring."""
        self.stop_event.clear()
        self.running = True
        self.thread = Thread(target=self._monitor_loop, daemon=True)
        self.thread.start()
    
    def stop(self):
        """Stop monitoring."""
        self.stop_event.set()
        self.running = False
        if self.thread:
            self.thread.join(timeout=2)


class FleetMonitor:
    """Monitor multiple devices."""
    
    def __init__(self, ports: List[str]):
        self.monitors: Dict[str, DeviceMonitor] = {}
        for port in ports:
            self.monitors[port] = DeviceMonitor(port)
    
    def start_all(self):
        """Start monitoring all devices."""
        for monitor in self.monitors.values():
            monitor.start()
    
    def stop_all(self):
        """Stop monitoring all devices."""
        for monitor in self.monitors.values():
            monitor.stop()
    
    def get_all_metrics(self) -> List[DeviceMetrics]:
        """Get metrics from all devices."""
        return [m.metrics for m in self.monitors.values()]
    
    def to_yaml(self) -> str:
        """Export all metrics as YAML."""
        data = {
            "timestamp": datetime.now().isoformat(),
            "devices": [m.to_dict() for m in self.get_all_metrics()]
        }
        return yaml.dump(data, default_flow_style=False)
    
    def to_markdown(self) -> str:
        """Export all metrics as markdown."""
        lines = [
            "# Fleet Monitor",
            "",
            f"*Updated: {datetime.now().isoformat()}*",
            "",
        ]
        
        for metrics in self.get_all_metrics():
            lines.append(metrics.to_markdown())
            lines.append("")
        
        return "\n".join(lines)
    
    def to_prometheus(self) -> str:
        """Export all metrics as Prometheus format."""
        lines = [
            "# HELP reflex_heartbeat Device heartbeat status",
            "# TYPE reflex_heartbeat gauge",
            "# HELP reflex_latency_avg_ns Average reflex latency",
            "# TYPE reflex_latency_avg_ns gauge",
        ]
        
        for metrics in self.get_all_metrics():
            lines.append(metrics.to_prometheus())
        
        return "\n".join(lines)


def monitor_single(port: str, output_format: str = "yaml", 
                   duration: Optional[float] = None) -> str:
    """
    Monitor a single device.
    Returns output in requested format.
    """
    monitor = DeviceMonitor(port)
    monitor.start()
    
    try:
        if duration:
            time.sleep(duration)
        else:
            # Just get current snapshot
            time.sleep(3)
    finally:
        monitor.stop()
    
    if output_format == "yaml":
        return monitor.metrics.to_yaml()
    elif output_format == "markdown":
        return monitor.metrics.to_markdown()
    elif output_format == "prometheus":
        return monitor.metrics.to_prometheus()
    elif output_format == "json":
        return json.dumps(monitor.metrics.to_dict(), indent=2)
    else:
        return monitor.metrics.to_yaml()
