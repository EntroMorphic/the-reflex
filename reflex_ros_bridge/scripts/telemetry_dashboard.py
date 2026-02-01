#!/usr/bin/env python3
"""
telemetry_dashboard.py - Real-time Reflex Telemetry Visualization

Reads shared memory channels and visualizes force control in real-time using Rerun.

Usage:
    python3 telemetry_dashboard.py [--addr RERUN_ADDR]

Connect Rerun viewer:
    rerun --connect  (local)
    rerun --addr IP:PORT  (remote)
"""

import argparse
import mmap
import os
import struct
import time
from collections import deque

try:
    import rerun as rr
    HAVE_RERUN = True
except ImportError:
    HAVE_RERUN = False
    print("Warning: rerun-sdk not installed. Using console output.")

# Channel format: sequence(8) + timestamp(8) + value(8) = 24 bytes
CHANNEL_FORMAT = '<QQQ'

# Configuration
FORCE_THRESHOLD = 5.0  # Newtons
TARGET_FORCE = 2.0     # Newtons
HISTORY_SIZE = 2000    # Samples to keep


class SharedChannelReader:
    """Read-only access to Reflex shared memory channel"""
    
    def __init__(self, name: str):
        self.name = name
        self.path = f"/dev/shm/{name}"
        self.fd = None
        self.mm = None
        self._connect()
    
    def _connect(self):
        """Connect to shared memory, retry if not exists"""
        for _ in range(50):
            if os.path.exists(self.path):
                self.fd = os.open(self.path, os.O_RDONLY)
                self.mm = mmap.mmap(self.fd, 64, access=mmap.ACCESS_READ)
                return
            time.sleep(0.1)
        raise RuntimeError(f"Channel {self.name} not found at {self.path}")
    
    def read(self) -> tuple:
        """Read (sequence, timestamp, value)"""
        self.mm.seek(0)
        return struct.unpack(CHANNEL_FORMAT, self.mm.read(24))
    
    @property
    def sequence(self) -> int:
        self.mm.seek(0)
        return struct.unpack('<Q', self.mm.read(8))[0]
    
    @property  
    def value(self) -> int:
        self.mm.seek(16)
        return struct.unpack('<Q', self.mm.read(8))[0]
    
    def close(self):
        if self.mm:
            self.mm.close()
        if self.fd:
            os.close(self.fd)


def decode_force(value: int) -> float:
    """Convert μN to N"""
    return value / 1_000_000.0


def decode_position(value: int) -> float:
    """Convert μm to normalized position"""
    return value / 1_000_000.0


def main():
    parser = argparse.ArgumentParser(description='Reflex Telemetry Dashboard')
    parser.add_argument('--addr', default=None, help='Rerun server address (IP:PORT)')
    parser.add_argument('--spawn', action='store_true', help='Spawn Rerun viewer')
    args = parser.parse_args()
    
    print("╔═══════════════════════════════════════════════════════════════╗")
    print("║       REFLEX TELEMETRY DASHBOARD                              ║")
    print("╚═══════════════════════════════════════════════════════════════╝")
    print()
    
    # Initialize Rerun
    if HAVE_RERUN:
        rr.init("reflex_telemetry", spawn=args.spawn)
        if args.addr:
            rr.connect(args.addr)
        print(f"Rerun initialized (spawn={args.spawn}, addr={args.addr})")
        
        # Set up time series
        rr.log("force", rr.SeriesLine(color=[255, 100, 100], name="Force (N)"))
        rr.log("threshold", rr.SeriesLine(color=[255, 0, 0], name="Threshold"))
        rr.log("target", rr.SeriesLine(color=[0, 255, 0], name="Target"))
        rr.log("position", rr.SeriesLine(color=[100, 100, 255], name="Position"))
        rr.log("anomaly", rr.SeriesLine(color=[255, 255, 0], name="Anomaly"))
    
    # Connect to channels
    print("Connecting to shared memory channels...")
    try:
        force_ch = SharedChannelReader("reflex_force")
        command_ch = SharedChannelReader("reflex_command")
        telemetry_ch = SharedChannelReader("reflex_telemetry")
        print("  All channels connected")
    except Exception as e:
        print(f"  Failed: {e}")
        print("  Make sure the bridge is running first")
        return 1
    
    # History buffers
    force_history = deque(maxlen=HISTORY_SIZE)
    position_history = deque(maxlen=HISTORY_SIZE)
    
    # State
    last_seq = 0
    sample_count = 0
    anomaly_count = 0
    start_time = time.time()
    
    print()
    print("Streaming telemetry (Ctrl+C to stop)...")
    print()
    
    try:
        while True:
            # Check for new data
            seq = telemetry_ch.sequence
            
            if seq != last_seq:
                last_seq = seq
                
                # Read values
                force = decode_force(force_ch.value)
                position = decode_position(command_ch.value)
                anomaly = telemetry_ch.value
                
                # Update history
                force_history.append(force)
                position_history.append(position)
                
                if anomaly:
                    anomaly_count += 1
                
                sample_count += 1
                
                # Log to Rerun
                if HAVE_RERUN:
                    rr.set_time_sequence("sample", sample_count)
                    rr.log("force", rr.Scalar(force))
                    rr.log("threshold", rr.Scalar(FORCE_THRESHOLD))
                    rr.log("target", rr.Scalar(TARGET_FORCE))
                    rr.log("position", rr.Scalar(position))
                    rr.log("anomaly", rr.Scalar(float(anomaly)))
                    
                    # Log text events
                    if anomaly and sample_count % 100 == 0:
                        rr.log("events", rr.TextLog(
                            f"ANOMALY: Force {force:.2f}N exceeds threshold",
                            level=rr.TextLogLevel.WARN
                        ))
                
                # Console output every 500 samples
                if sample_count % 500 == 0:
                    elapsed = time.time() - start_time
                    rate = sample_count / elapsed if elapsed > 0 else 0
                    status = "ANOMALY!" if anomaly else "OK"
                    print(f"  [{status:8s}] Force: {force:5.2f}N  Position: {position:.3f}  "
                          f"Rate: {rate:.0f} Hz  Anomalies: {anomaly_count}")
            
            # Poll rate
            time.sleep(0.0001)  # 10kHz max poll
            
    except KeyboardInterrupt:
        print()
        print("Stopped.")
    
    finally:
        force_ch.close()
        command_ch.close()
        telemetry_ch.close()
    
    # Final stats
    elapsed = time.time() - start_time
    print()
    print(f"Session: {sample_count} samples in {elapsed:.1f}s ({sample_count/elapsed:.0f} Hz)")
    print(f"Anomalies: {anomaly_count}")
    
    return 0


if __name__ == "__main__":
    exit(main())
