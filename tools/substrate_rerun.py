#!/usr/bin/env python3 -u
"""
substrate_rerun.py - Substrate Discovery Visualization in Rerun

Receives text-based streaming data from ESP32-C6 substrate discovery
and visualizes memory regions in 3D.

Protocol (see reflex_substrate_stream.h):
    ##SUBSTRATE##:INIT
    ##SUBSTRATE##:PHASE:<phase_name>
    ##SUBSTRATE##:PROBE:<addr>,<type>,<read_cycles>,<write_cycles>
    ##SUBSTRATE##:REGION:<start>,<end>,<type>,<avg_cycles>
    ##SUBSTRATE##:MAP_START
    ##SUBSTRATE##:MAP_END:<total_probes>,<faults>,<regions>

Visualization:
    - Memory addresses map to 3D positions
    - Colors: RAM=green, REGISTER=blue, ROM=gray, FAULT=red
    - Point size indicates latency (larger = slower)

Usage:
    python substrate_rerun.py /dev/ttyACM0
    python substrate_rerun.py /dev/ttyACM0 substrate.rrd
"""

import sys
import datetime
import re
import numpy as np

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)

try:
    import rerun as rr
except ImportError:
    print("ERROR: rerun-sdk not installed. Run: pip install rerun-sdk")
    sys.exit(1)

# Stream marker
MARKER = "##SUBSTRATE##"

# Memory type enum (matches reflex_substrate.h)
MEM_TYPES = {
    0: "UNKNOWN",
    1: "RAM",
    2: "ROM",
    3: "REGISTER",
    4: "FAULT",
    5: "SELF",
    6: "RESERVED",
}

# Colors for memory types (RGBA)
TYPE_COLORS = {
    "UNKNOWN":  [128, 128, 128, 255],  # Gray
    "RAM":      [0, 255, 0, 255],      # Green
    "ROM":      [160, 160, 160, 255],  # Light gray
    "REGISTER": [0, 128, 255, 255],   # Blue
    "FAULT":    [255, 0, 0, 255],      # Red
    "SELF":     [255, 255, 0, 255],    # Yellow
    "RESERVED": [128, 0, 128, 255],    # Purple
}


def addr_to_3d(addr):
    """
    Map a 32-bit address to 3D coordinates.

    Memory layout visualization:
    - X axis: bits 0-11 (4KB page offset) -> maps to 0-4 meters
    - Y axis: bits 12-19 (256 pages)      -> maps to 0-4 meters
    - Z axis: top 4 bits (region ID)      -> maps to -2 to 2 meters

    Region mapping:
    - 0x4xxxxxxx -> Z=0 (SRAM)
    - 0x5xxxxxxx -> Z=1 (LP SRAM)
    - 0x6xxxxxxx -> Z=2 (Peripherals)
    """
    # Extract components
    page_offset = (addr >> 0) & 0xFFF      # 12 bits
    page_num = (addr >> 12) & 0xFF         # 8 bits
    region = (addr >> 28) & 0xF            # Top 4 bits

    # Normalize to visualization space
    x = (page_offset / 4096.0) * 4.0
    y = (page_num / 256.0) * 4.0

    # Z by memory region
    z_map = {
        0x4: 0.0,  # SRAM
        0x5: 1.0,  # LP SRAM
        0x6: 2.0,  # Peripherals
    }
    z = z_map.get(region, -1.0)

    return [x, y, z]


def cycles_to_size(cycles, min_cycles=50, max_cycles=200):
    """Map cycle count to point size (0.02 to 0.1 meters)."""
    if cycles <= 0:
        return 0.02

    # Clamp and normalize
    normalized = (cycles - min_cycles) / (max_cycles - min_cycles)
    normalized = max(0.0, min(1.0, normalized))

    # Map to size range
    return 0.02 + normalized * 0.08


class SubstrateVisualizer:
    def __init__(self):
        self.probes = []  # List of (addr, type, read_cycles, write_cycles)
        self.regions = []  # List of (start, end, type, avg_cycles)
        self.total_probes = 0
        self.total_faults = 0
        self.phase = "INIT"

    def handle_line(self, line):
        """Parse and handle a single line from serial."""
        line = line.strip()

        # Skip non-substrate lines
        if MARKER not in line:
            # But still print them for debugging
            if line and not line.startswith("I (") and not line.startswith("W ("):
                print(f"[serial] {line}")
            return

        # Extract message after marker
        try:
            _, msg = line.split(f"{MARKER}:", 1)
        except ValueError:
            return

        # Parse message type
        if msg.startswith("INIT"):
            self._handle_init()
        elif msg.startswith("PHASE:"):
            self._handle_phase(msg[6:])
        elif msg.startswith("PROBE:"):
            self._handle_probe(msg[6:])
        elif msg.startswith("REGION:"):
            self._handle_region(msg[7:])
        elif msg.startswith("MAP_START"):
            self._handle_map_start()
        elif msg.startswith("MAP_END:"):
            self._handle_map_end(msg[8:])

    def _handle_init(self):
        print(">>> Substrate stream initialized")
        rr.log("substrate/status", rr.TextLog("Stream initialized"))

    def _handle_phase(self, phase_name):
        self.phase = phase_name
        print(f">>> Phase: {phase_name}")
        rr.log("substrate/phase", rr.TextLog(f"Phase: {phase_name}"))

    def _handle_probe(self, data):
        """Parse: addr,type,read_cycles,write_cycles"""
        try:
            parts = data.split(",")
            addr = int(parts[0], 16)
            mem_type = int(parts[1])
            read_cycles = int(parts[2])
            write_cycles = int(parts[3])
        except (ValueError, IndexError) as e:
            print(f"[warn] Bad probe data: {data}")
            return

        type_name = MEM_TYPES.get(mem_type, "UNKNOWN")
        self.probes.append((addr, type_name, read_cycles, write_cycles))

        # Log to Rerun
        pos = addr_to_3d(addr)
        color = TYPE_COLORS.get(type_name, TYPE_COLORS["UNKNOWN"])
        size = cycles_to_size(read_cycles)

        # Set time based on probe count
        rr.set_time("probe", sequence=len(self.probes))

        # Log as 3D point
        rr.log(
            f"substrate/probes/{type_name.lower()}",
            rr.Points3D(
                positions=[pos],
                colors=[color],
                radii=[size],
            )
        )

        # Progress update
        if len(self.probes) % 500 == 0:
            print(f"  {len(self.probes)} probes...", flush=True)

    def _handle_region(self, data):
        """Parse: start,end,type,avg_cycles"""
        try:
            parts = data.split(",")
            start = int(parts[0], 16)
            end = int(parts[1], 16)
            mem_type = int(parts[2])
            avg_cycles = int(parts[3])
        except (ValueError, IndexError) as e:
            print(f"[warn] Bad region data: {data}")
            return

        type_name = MEM_TYPES.get(mem_type, "UNKNOWN")
        self.regions.append((start, end, type_name, avg_cycles))

        size_kb = (end - start) // 1024
        print(f"  Region: 0x{start:08x}-0x{end:08x} ({size_kb}KB) {type_name} @ {avg_cycles} cycles")

        # Visualize region as a box
        start_pos = addr_to_3d(start)
        end_pos = addr_to_3d(end)

        # Box defined by two corners
        center = [
            (start_pos[0] + end_pos[0]) / 2,
            (start_pos[1] + end_pos[1]) / 2,
            start_pos[2],  # Keep Z from region
        ]
        half_size = [
            abs(end_pos[0] - start_pos[0]) / 2 + 0.1,
            abs(end_pos[1] - start_pos[1]) / 2 + 0.1,
            0.1,  # Thin box
        ]

        color = TYPE_COLORS.get(type_name, TYPE_COLORS["UNKNOWN"])

        rr.log(
            f"substrate/regions/{type_name.lower()}/{len(self.regions)}",
            rr.Boxes3D(
                centers=[center],
                half_sizes=[half_size],
                colors=[color[:3]],  # RGB only
            )
        )

    def _handle_map_start(self):
        print(">>> Map collection started")
        self.probes.clear()
        self.regions.clear()
        rr.log("substrate/status", rr.TextLog("Mapping started"))

    def _handle_map_end(self, data):
        """Parse: total_probes,faults,regions"""
        try:
            parts = data.split(",")
            self.total_probes = int(parts[0])
            self.total_faults = int(parts[1])
            num_regions = int(parts[2])
        except (ValueError, IndexError) as e:
            print(f"[warn] Bad map_end data: {data}")
            return

        print(f">>> Map complete: {self.total_probes} probes, {self.total_faults} faults, {num_regions} regions")
        rr.log("substrate/status", rr.TextLog(
            f"Complete: {self.total_probes} probes, {self.total_faults} faults, {num_regions} regions"
        ))

        # Log summary
        self._log_summary()

    def _log_summary(self):
        """Log final summary visualization."""
        # Count by type
        type_counts = {}
        for _, type_name, _, _ in self.probes:
            type_counts[type_name] = type_counts.get(type_name, 0) + 1

        print("\nMemory Type Summary:")
        for type_name, count in sorted(type_counts.items()):
            print(f"  {type_name}: {count} probes")

        # Log as bar chart
        labels = list(type_counts.keys())
        values = [float(type_counts[l]) for l in labels]

        rr.log("substrate/summary/type_counts",
               rr.BarChart(values))


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <serial_port> [output.rrd] [--headless]")
        print(f"Example: {sys.argv[0]} /dev/ttyACM0")
        print(f"Example: {sys.argv[0]} /dev/ttyACM0 substrate.rrd")
        print(f"Example: {sys.argv[0]} /dev/ttyACM0 substrate.rrd --headless")
        sys.exit(1)

    port = sys.argv[1]
    headless = "--headless" in sys.argv

    # Generate timestamped RRD filename if not provided
    args = [a for a in sys.argv[2:] if not a.startswith("--")]
    if args:
        rrd_file = args[0]
    else:
        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        rrd_file = f"substrate_{timestamp}.rrd"

    # Initialize Rerun
    rr.init("substrate_discovery")

    if headless:
        # Headless mode - just save to file
        rr.save(rrd_file)
        print(f"Recording to: {rrd_file} (headless mode)", flush=True)
    else:
        # Try to spawn viewer
        try:
            rr.spawn()
            import time
            time.sleep(1)  # Give viewer time to start
            # Set up both file and viewer sinks
            file_sink = rr.FileSink(rrd_file)
            grpc_sink = rr.GrpcSink("rerun+http://127.0.0.1:9876/proxy")
            rr.set_sinks(file_sink, grpc_sink)
            print(f"Recording to: {rrd_file} + viewer", flush=True)
        except RuntimeError:
            # Fall back to headless
            print("Viewer not available, using headless mode", flush=True)
            rr.save(rrd_file)
            print(f"Recording to: {rrd_file}", flush=True)

    # Log initial info
    rr.log("substrate/info", rr.TextLog("Substrate Discovery Viewer"))
    rr.log("substrate/info", rr.TextLog("X: page offset, Y: page number, Z: memory region"))

    # Open serial
    print(f"Opening {port}...")
    try:
        ser = serial.Serial(port, 115200, timeout=0.1)
        ser.reset_input_buffer()
    except serial.SerialException as e:
        print(f"ERROR: Could not open {port}: {e}")
        sys.exit(1)

    print("Listening for substrate stream...", flush=True)
    print("Press Ctrl+C to stop\n")

    visualizer = SubstrateVisualizer()
    line_buffer = ""

    try:
        while True:
            # Read available data
            data = ser.read(ser.in_waiting or 1)
            if not data:
                continue

            # Decode and buffer
            try:
                text = data.decode('utf-8', errors='replace')
            except UnicodeDecodeError:
                continue

            line_buffer += text

            # Process complete lines
            while '\n' in line_buffer:
                line, line_buffer = line_buffer.split('\n', 1)
                visualizer.handle_line(line)

    except KeyboardInterrupt:
        print(f"\n\nReceived {len(visualizer.probes)} probes")
        print(f"Saved to: {rrd_file}")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
