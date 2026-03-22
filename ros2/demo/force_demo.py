#!/usr/bin/env python3
"""
Minimal Reflex Demo - No ROS2 Required

Demonstrates 10kHz force control using shared memory channels.
Visualizes with Rerun.

Run on Thor:
  1. Start this script in the container
  2. Start reflex_force_control on host (isolated cores)
  3. Open Rerun viewer on workstation
"""

import mmap
import struct
import time
import math
import threading
import numpy as np

# Try to import rerun, fall back to console output
try:
    import rerun as rr
    HAVE_RERUN = True
except ImportError:
    HAVE_RERUN = False
    print("Rerun not available, using console output")

# Channel structure: sequence(8) + timestamp(8) + value(8) + padding(40) = 64 bytes
CHANNEL_SIZE = 64
CHANNEL_FORMAT = '<QQQ'  # 3x uint64_t little-endian

class SharedChannel:
    """Python wrapper for Reflex shared memory channel"""
    
    def __init__(self, name: str, create: bool = False):
        import posix_ipc
        
        self.name = name
        shm_name = f"/{name}"
        
        if create:
            try:
                posix_ipc.unlink_shared_memory(shm_name)
            except:
                pass
            self.shm = posix_ipc.SharedMemory(shm_name, posix_ipc.O_CREAT | posix_ipc.O_RDWR, size=CHANNEL_SIZE)
        else:
            # Wait for channel to exist
            for _ in range(100):
                try:
                    self.shm = posix_ipc.SharedMemory(shm_name, posix_ipc.O_RDWR)
                    break
                except:
                    time.sleep(0.1)
            else:
                raise RuntimeError(f"Channel {name} not found")
        
        self.mm = mmap.mmap(self.shm.fd, CHANNEL_SIZE)
        self.shm.close_fd()
        
        if create:
            self.mm.seek(0)
            self.mm.write(struct.pack(CHANNEL_FORMAT, 0, 0, 0))
    
    def signal(self, value: int):
        """Write value and increment sequence"""
        self.mm.seek(0)
        seq, _, _ = struct.unpack(CHANNEL_FORMAT, self.mm.read(24))
        
        timestamp = int(time.monotonic() * 1e9)
        self.mm.seek(0)
        self.mm.write(struct.pack(CHANNEL_FORMAT, seq + 1, timestamp, value))
    
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


def encode_force(force_newtons: float) -> int:
    return int(force_newtons * 1_000_000)

def decode_force(value: int) -> float:
    return value / 1_000_000

def decode_position(value: int) -> float:
    return value / 1_000_000


class ForceSimulator:
    """Simulates force sensor with realistic grasp profile"""
    
    def __init__(self, channel: SharedChannel):
        self.channel = channel
        self.running = False
        self.mode = 'approach'
        self.contact_time = None
        self.t = 0.0
    
    def start(self):
        self.running = True
        self.thread = threading.Thread(target=self._run)
        self.thread.start()
    
    def stop(self):
        self.running = False
        self.thread.join()
    
    def _run(self):
        """Publish force at 1kHz"""
        while self.running:
            force = self._get_force()
            self.channel.signal(encode_force(force))
            time.sleep(0.001)  # 1kHz
    
    def _get_force(self) -> float:
        if self.mode == 'approach':
            # No force during approach
            self.t += 0.001
            if self.t > 2.0:
                self.mode = 'contact'
                self.contact_time = time.time()
            return 0.0
        
        elif self.mode == 'contact':
            # Force ramps up on contact
            dt = time.time() - self.contact_time
            force = min(10.0, dt * 15.0)  # 15 N/s ramp
            
            if force > 6.0:
                self.mode = 'grasp'
            return force
        
        else:  # grasp
            # Steady state with noise
            return 2.0 + 0.2 * math.sin(time.time() * 20)
    
    def reset(self):
        self.mode = 'approach'
        self.t = 0.0
        self.contact_time = None


def main():
    print("╔═══════════════════════════════════════════════════════════════╗")
    print("║       REFLEX DEMO: Force Control Visualization                ║")
    print("╚═══════════════════════════════════════════════════════════════╝")
    print()
    
    # Initialize Rerun
    if HAVE_RERUN:
        rr.init("reflex_force_demo", spawn=True)
        print("Rerun initialized - viewer should open")
    
    # Create channels
    print("Creating shared memory channels...")
    force_channel = SharedChannel("reflex_force", create=True)
    command_channel = SharedChannel("reflex_command", create=True)
    telemetry_channel = SharedChannel("reflex_telemetry", create=True)
    print("  Channels created")
    
    # Start force simulator
    print("Starting force simulator (1kHz)...")
    simulator = ForceSimulator(force_channel)
    simulator.start()
    print("  Simulator running")
    
    print()
    print("Waiting for reflex_force_control to connect...")
    print("Run on host: sudo taskset -c 0-2 ./reflex_force_control")
    print()
    print("Press Ctrl+C to stop")
    print()
    
    # Monitoring loop
    force_history = []
    command_history = []
    anomaly_history = []
    last_seq = 0
    sample_count = 0
    start_time = time.time()
    
    try:
        while True:
            # Read channels
            seq = telemetry_channel.sequence
            
            if seq != last_seq:
                last_seq = seq
                
                force = decode_force(force_channel.value)
                command = decode_position(command_channel.value)
                anomaly = telemetry_channel.value
                
                force_history.append(force)
                command_history.append(command)
                anomaly_history.append(anomaly)
                
                # Keep last 1000 samples
                if len(force_history) > 1000:
                    force_history.pop(0)
                    command_history.pop(0)
                    anomaly_history.pop(0)
                
                # Log to Rerun
                if HAVE_RERUN:
                    rr.log("force/current", rr.Scalar(force))
                    rr.log("command/current", rr.Scalar(command))
                    rr.log("threshold", rr.Scalar(5.0))  # Force threshold
                    rr.log("target", rr.Scalar(2.0))      # Target force
                    
                    if anomaly:
                        rr.log("events/anomaly", rr.TextLog("FORCE THRESHOLD EXCEEDED", level="WARN"))
                
                sample_count += 1
                
                # Periodic console output
                if sample_count % 1000 == 0:
                    elapsed = time.time() - start_time
                    rate = sample_count / elapsed
                    anomaly_count = sum(anomaly_history)
                    print(f"  Samples: {sample_count}, Rate: {rate:.0f} Hz, Force: {force:.2f}N, "
                          f"Cmd: {command:.3f}, Anomalies: {anomaly_count}")
            
            time.sleep(0.0001)  # 10kHz poll
            
    except KeyboardInterrupt:
        print("\nStopping...")
    
    finally:
        simulator.stop()
        print("Demo stopped")


if __name__ == "__main__":
    main()
