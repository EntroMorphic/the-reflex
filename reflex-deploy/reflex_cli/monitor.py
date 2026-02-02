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
    # Hologram-specific metrics
    firmware_type: str = "reflex"  # "reflex" or "hologram"
    node_id: int = 0
    neighbors: int = 0
    confidence: float = 0.0
    crystallized_bits: int = 0
    radio_tx: int = 0
    radio_rx: int = 0
    radio_errors: int = 0
    rssi: int = 0
    hidden_state: str = ""
    constructive: str = ""
    destructive: str = ""
    tick_count: int = 0
    entropy_map: str = ""
    
    def to_dict(self) -> dict:
        d = {
            "port": self.port,
            "timestamp": self.timestamp,
            "heartbeat": self.heartbeat,
            "status": self.status,
            "firmware_type": self.firmware_type,
        }
        
        if self.firmware_type == "hologram":
            d.update({
                "node_id": self.node_id,
                "hologram": {
                    "neighbors": self.neighbors,
                    "confidence": self.confidence,
                    "crystallized_bits": self.crystallized_bits,
                    "tick_count": self.tick_count,
                    "entropy_map": self.entropy_map,
                },
                "radio": {
                    "tx": self.radio_tx,
                    "rx": self.radio_rx,
                    "errors": self.radio_errors,
                    "rssi": self.rssi,
                },
                "interference": {
                    "hidden": self.hidden_state,
                    "constructive": self.constructive,
                    "destructive": self.destructive,
                },
                "last_error": self.last_error,
            })
        else:
            d.update({
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
            })
        
        return d
    
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
    
    # ═══════════════════════════════════════════════════════════════════════
    # HOLOGRAM TELEMETRY PARSING
    # ═══════════════════════════════════════════════════════════════════════
    
    # Parse node ID: "NODE 1 STATUS" or "NODE 2" etc
    node_match = re.search(r'NODE\s+(\d+)', text)
    if node_match:
        metrics['node_id'] = int(node_match.group(1))
        metrics['firmware_type'] = 'hologram'
    
    # Parse neighbors: "Neighbors:     1 active"
    neighbors_match = re.search(r'Neighbors:\s*(\d+)\s*active', text)
    if neighbors_match:
        metrics['neighbors'] = int(neighbors_match.group(1))
    
    # Parse confidence: "Confidence:    55.6%"
    confidence_match = re.search(r'Confidence:\s*([\d.]+)%', text)
    if confidence_match:
        metrics['confidence'] = float(confidence_match.group(1))
    
    # Parse crystallized bits: "Crystallized:  21 bits"
    crystallized_match = re.search(r'Crystallized:\s*(\d+)\s*bits', text)
    if crystallized_match:
        metrics['crystallized_bits'] = int(crystallized_match.group(1))
    
    # Parse radio stats: "Radio TX: 1996  RX: 4  Errors: 0  RSSI: -54 dBm"
    tx_match = re.search(r'Radio TX:\s*(\d+)', text)
    if tx_match:
        metrics['radio_tx'] = int(tx_match.group(1))
    
    rx_match = re.search(r'RX:\s*(\d+)', text)
    if rx_match:
        metrics['radio_rx'] = int(rx_match.group(1))
    
    radio_errors_match = re.search(r'Errors:\s*(\d+)', text)
    if radio_errors_match:
        metrics['radio_errors'] = int(radio_errors_match.group(1))
    
    rssi_match = re.search(r'RSSI:\s*(-?\d+)\s*dBm', text)
    if rssi_match:
        metrics['rssi'] = int(rssi_match.group(1))
    
    # Parse hidden state: "Hidden:        0x02000c1244888243"
    hidden_match = re.search(r'Hidden:\s*(0x[0-9a-fA-F]+)', text)
    if hidden_match:
        metrics['hidden_state'] = hidden_match.group(1)
    
    # Parse interference: "Constructive:  0x..." and "Destructive:   0x..."
    constructive_match = re.search(r'Constructive:\s*(0x[0-9a-fA-F]+)', text)
    if constructive_match:
        metrics['constructive'] = constructive_match.group(1)
    
    destructive_match = re.search(r'Destructive:\s*(0x[0-9a-fA-F]+)', text)
    if destructive_match:
        metrics['destructive'] = destructive_match.group(1)
    
    # Parse tick count from status header: "tick 20000"
    tick_match = re.search(r'tick\s+(\d+)', text)
    if tick_match:
        metrics['tick_count'] = int(tick_match.group(1))
    
    # Parse entropy map: "Entropy: ##....#..#.....##"
    entropy_match = re.search(r'Entropy:\s*([#.\-]+)', text)
    if entropy_match:
        metrics['entropy_map'] = entropy_match.group(1)
    
    # ═══════════════════════════════════════════════════════════════════════
    # STANDARD REFLEX TELEMETRY PARSING
    # ═══════════════════════════════════════════════════════════════════════
    
    # Parse signal/anomaly counts
    # Looking for: "Signals:       5009" and "Anomalies:     2532"
    signals_match = re.search(r'Signals:\s*(\d+)', text)
    if signals_match:
        metrics['signal_count'] = int(signals_match.group(1))
    
    anomalies_match = re.search(r'Anomalies:\s*(\d+)', text)
    if anomalies_match:
        metrics['anomaly_count'] = int(anomalies_match.group(1))
    
    # Parse latency - handles both "Min: 87 ns" and "Min: 14 cycles = 87 ns" formats
    min_match = re.search(r'Min:.*?(\d+)\s*ns', text)
    if min_match:
        metrics['reflex_min_ns'] = int(min_match.group(1))
    
    max_match = re.search(r'Max:.*?(\d+)\s*ns', text)
    if max_match:
        metrics['reflex_max_ns'] = int(max_match.group(1))
    
    avg_match = re.search(r'Avg:.*?(\d+)\s*ns', text)
    if avg_match:
        metrics['reflex_avg_ns'] = int(avg_match.group(1))
    
    # Check for real errors (not "Errors: N" from radio stats or "last_error" key)
    if re.search(r'panic|guru meditation|stack.*fault|watchdog', text, re.IGNORECASE):
        metrics['last_error'] = "Critical error detected"
    elif re.search(r'\berror\b', text, re.IGNORECASE):
        # Ignore "Radio ... Errors:" and "last_error" references
        if not re.search(r'(Radio.*Errors:|last_error)', text, re.IGNORECASE):
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
                    
                    # Update hologram metrics if detected
                    if parsed.get('firmware_type') == 'hologram':
                        self.metrics.firmware_type = 'hologram'
                    if parsed.get('node_id'):
                        self.metrics.node_id = parsed['node_id']
                    if 'neighbors' in parsed:
                        self.metrics.neighbors = parsed['neighbors']
                    if 'confidence' in parsed:
                        self.metrics.confidence = parsed['confidence']
                    if 'crystallized_bits' in parsed:
                        self.metrics.crystallized_bits = parsed['crystallized_bits']
                    if 'radio_tx' in parsed:
                        self.metrics.radio_tx = parsed['radio_tx']
                    if 'radio_rx' in parsed:
                        self.metrics.radio_rx = parsed['radio_rx']
                    if 'radio_errors' in parsed:
                        self.metrics.radio_errors = parsed['radio_errors']
                    if 'rssi' in parsed:
                        self.metrics.rssi = parsed['rssi']
                    if 'hidden_state' in parsed:
                        self.metrics.hidden_state = parsed['hidden_state']
                    if 'constructive' in parsed:
                        self.metrics.constructive = parsed['constructive']
                    if 'destructive' in parsed:
                        self.metrics.destructive = parsed['destructive']
                    if 'tick_count' in parsed:
                        self.metrics.tick_count = parsed['tick_count']
                    if 'entropy_map' in parsed:
                        self.metrics.entropy_map = parsed['entropy_map']
                    
                    # Update standard reflex metrics
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
                elif self.metrics.firmware_type == "hologram":
                    # Hologram-specific status
                    if self.metrics.neighbors > 0 and self.metrics.confidence > 30:
                        self.metrics.status = "ok"
                    elif self.metrics.neighbors > 0:
                        self.metrics.status = "learning"
                    else:
                        self.metrics.status = "isolated"
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
