#!/usr/bin/env python3
"""
rerun_receiver.py - Receive Reflex event stream and visualize in Rerun

Reads binary packets from C6 via serial, logs to Rerun for real-time visualization.

Usage:
    python rerun_receiver.py /dev/ttyACM0
"""

import sys
import struct
import serial
import numpy as np

try:
    import rerun as rr
except ImportError:
    print("ERROR: rerun-sdk not installed. Run: pip install rerun-sdk")
    sys.exit(1)

# Packet format (48 bytes)
# See reflex_stream.h for structure
PACKET_SIZE = 48
PACKET_FORMAT = '<BBH' + '8B8B8B' + 'BBBB' + '4h' + '8B'
MAGIC = 0x52  # 'R' for Reflex

def parse_packet(data):
    """Parse binary packet into dict."""
    if len(data) != PACKET_SIZE:
        return None

    values = struct.unpack(PACKET_FORMAT, data)

    magic = values[0]
    if magic != MAGIC:
        return None

    return {
        'magic': values[0],
        'version': values[1],
        'tick': values[2],
        'slow_scores': list(values[3:11]),
        'med_scores': list(values[11:19]),
        'fast_scores': list(values[19:27]),
        'chosen_output': values[27],
        'chosen_state': values[28],
        'agreement': values[29] / 255.0,
        'disagreement': values[30] / 255.0,
        'adc_deltas': list(values[31:35]),
        'output_counts': list(values[35:43]),
    }

def log_packet(pkt):
    """Log packet data to Rerun."""
    tick = pkt['tick']

    # Layer scores as bar charts
    rr.log("layers/slow", rr.BarChart(pkt['slow_scores']))
    rr.log("layers/medium", rr.BarChart(pkt['med_scores']))
    rr.log("layers/fast", rr.BarChart(pkt['fast_scores']))

    # Chosen output as scalar
    rr.log("decision/output", rr.Scalars(pkt['chosen_output']))
    rr.log("decision/state", rr.Scalars(pkt['chosen_state']))

    # Agreement/disagreement
    rr.log("consensus/agreement", rr.Scalars(pkt['agreement']))
    rr.log("consensus/disagreement", rr.Scalars(pkt['disagreement']))

    # ADC deltas as bar chart
    rr.log("observation/adc_deltas", rr.BarChart(pkt['adc_deltas']))

    # Output counts as bar chart (exploration distribution)
    rr.log("exploration/counts", rr.BarChart(pkt['output_counts']))

    # Layer scores as heatmap (3 layers × 8 outputs)
    scores_matrix = np.array([
        pkt['slow_scores'],
        pkt['med_scores'],
        pkt['fast_scores']
    ], dtype=np.float32)
    rr.log("layers/heatmap", rr.Image(scores_matrix / 255.0))

    # Log significant ADC discoveries
    for i, delta in enumerate(pkt['adc_deltas']):
        if abs(delta) > 1000:
            rr.log(f"discovery/adc{i}",
                   rr.TextLog(f"GPIO{pkt['chosen_output']} → ADC{i}: {delta}"))

def main():
    import datetime

    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <serial_port> [output.rrd]")
        print(f"Example: {sys.argv[0]} /dev/ttyACM0")
        print(f"Example: {sys.argv[0]} /dev/ttyACM0 session.rrd")
        sys.exit(1)

    port = sys.argv[1]

    # Generate timestamped RRD filename if not provided
    if len(sys.argv) >= 3:
        rrd_file = sys.argv[2]
    else:
        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        rrd_file = f"reflex_{timestamp}.rrd"

    # Initialize Rerun with save
    rr.init("reflex_explorer", spawn=True)
    rr.save(rrd_file)
    print(f"Recording to: {rrd_file}")
    rr.log("info", rr.TextLog("Reflex Layered Exploration Viewer"))

    print(f"Opening {port}...")

    try:
        ser = serial.Serial(port, 115200, timeout=1)
    except serial.SerialException as e:
        print(f"ERROR: Could not open {port}: {e}")
        sys.exit(1)

    print("Listening for packets...")
    print("Press Ctrl+C to stop")

    buffer = bytearray()
    packet_count = 0

    try:
        while True:
            # Read available data
            data = ser.read(ser.in_waiting or 1)
            if not data:
                continue

            buffer.extend(data)

            # Look for magic byte and extract packets
            while len(buffer) >= PACKET_SIZE:
                # Find magic byte
                try:
                    idx = buffer.index(MAGIC)
                    if idx > 0:
                        # Discard bytes before magic
                        buffer = buffer[idx:]
                except ValueError:
                    # No magic found, discard all but last byte
                    buffer = buffer[-1:]
                    break

                if len(buffer) < PACKET_SIZE:
                    break

                # Try to parse packet
                pkt_data = bytes(buffer[:PACKET_SIZE])
                pkt = parse_packet(pkt_data)

                if pkt:
                    rr.set_time("tick", sequence=pkt['tick'])
                    log_packet(pkt)
                    packet_count += 1

                    if packet_count % 10 == 0:
                        print(f"Tick {pkt['tick']}: chose GPIO{pkt['chosen_output']}, "
                              f"agreement={pkt['agreement']:.0%}")

                    buffer = buffer[PACKET_SIZE:]
                else:
                    # Bad packet, skip one byte
                    buffer = buffer[1:]

    except KeyboardInterrupt:
        print(f"\nReceived {packet_count} packets")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
