#!/usr/bin/env python3
"""
rerun_etm_monitor.py - Real-time ETM Fabric Visualization

Receives structured events from ESP32-C6 via USB serial and visualizes
the autonomous hardware computation in Rerun.

Visualization Layers:
  1. Time-series: PCNT counts, thresholds, pattern activity
  2. State machine: Current state, transitions
  3. Heatmap: 4x4 channel activity matrix
  4. Event log: All ETM events as they occur

Protocol (binary, little-endian):
  Header: 0xAA 0x55 (sync)
  Type:   1 byte (event type)
  Len:    1 byte (payload length)
  Payload: variable
  CRC:    1 byte (XOR of type + len + payload)

Event Types:
  0x01: PCNT_UPDATE   - 4x int16 counts
  0x02: THRESHOLD     - uint8 channel, uint8 threshold_id, int16 count
  0x03: PATTERN_START - uint8 pattern_id
  0x04: PATTERN_END   - uint8 pattern_id, uint32 duration_us
  0x05: DMA_EOF       - uint8 channel
  0x06: ETM_EVENT     - uint8 event_id, uint8 task_id
  0x07: STATE_CHANGE  - uint8 old_state, uint8 new_state
  0x08: CYCLE_COMPLETE- uint32 cycle_num, uint32 duration_us
  0x10: HEARTBEAT     - uint32 uptime_ms, uint32 cycles

Usage:
  python rerun_etm_monitor.py /dev/ttyACM0
  python rerun_etm_monitor.py --demo  # Demo mode with synthetic data
"""

import argparse
import struct
import sys
import time
from collections import deque
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional

import numpy as np
import rerun as rr
import serial

# Protocol constants
SYNC_BYTE_1 = 0xAA
SYNC_BYTE_2 = 0x55


class EventType(IntEnum):
    PCNT_UPDATE = 0x01
    THRESHOLD = 0x02
    PATTERN_START = 0x03
    PATTERN_END = 0x04
    DMA_EOF = 0x05
    ETM_EVENT = 0x06
    STATE_CHANGE = 0x07
    CYCLE_COMPLETE = 0x08
    HEARTBEAT = 0x10
    TEXT_LOG = 0x20  # For debug text messages


# Pattern names
PATTERN_NAMES = {
    0: "A (sparse)",
    1: "B (medium)",
    2: "C (dense)",
}

# State names for state machine view
STATE_NAMES = {
    0: "IDLE",
    1: "PATTERN_A",
    2: "PATTERN_B",
    3: "PATTERN_C",
    4: "WAITING",
}

# Threshold names
THRESHOLD_NAMES = {
    0: "A (32)",
    1: "B (64)",
    2: "C (128)",
}


@dataclass
class ETMEvent:
    """Parsed ETM event from serial"""

    event_type: EventType
    timestamp: float  # Host timestamp
    payload: bytes


class ETMMonitor:
    """Receives and visualizes ETM events via Rerun"""

    def __init__(self, port: str, baud: int = 115200):
        self.port = port
        self.baud = baud
        self.ser: Optional[serial.Serial] = None

        # State tracking
        self.pcnt_counts = [0, 0, 0, 0]
        self.current_pattern = None
        self.current_state = 0
        self.cycle_count = 0
        self.start_time = time.time()
        self.step = 0  # Sequence counter for Rerun timeline

        # History for charts
        self.pcnt_history = [deque(maxlen=1000) for _ in range(4)]
        self.threshold_events = deque(maxlen=100)

        # Activity matrix (4x4: input channels x output accumulators)
        self.activity_matrix = np.zeros((4, 4), dtype=np.float32)

    def connect(self) -> bool:
        """Connect to serial port"""
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.1)
            print(f"Connected to {self.port} at {self.baud} baud")
            return True
        except serial.SerialException as e:
            print(f"Failed to connect: {e}")
            return False

    def close(self):
        """Close serial connection"""
        if self.ser:
            self.ser.close()

    def read_event(self) -> Optional[ETMEvent]:
        """Read and parse one event from serial"""
        if not self.ser:
            return None

        # Look for sync bytes
        while True:
            b = self.ser.read(1)
            if not b:
                return None
            if b[0] == SYNC_BYTE_1:
                b2 = self.ser.read(1)
                if b2 and b2[0] == SYNC_BYTE_2:
                    break

        # Read type and length
        header = self.ser.read(2)
        if len(header) < 2:
            return None

        event_type = header[0]
        payload_len = header[1]

        # Read payload
        payload = self.ser.read(payload_len) if payload_len > 0 else b""
        if len(payload) < payload_len:
            return None

        # Read and verify CRC
        crc_byte = self.ser.read(1)
        if not crc_byte:
            return None

        expected_crc = event_type ^ payload_len
        for b in payload:
            expected_crc ^= b

        if crc_byte[0] != expected_crc:
            print(f"CRC mismatch: got {crc_byte[0]:02x}, expected {expected_crc:02x}")
            return None

        try:
            return ETMEvent(
                event_type=EventType(event_type),
                timestamp=time.time() - self.start_time,
                payload=payload,
            )
        except ValueError:
            # Unknown event type, skip
            return None

    def _set_time(self):
        """Set Rerun timeline"""
        self.step += 1
        rr.set_time("step", sequence=self.step)

    def process_event(self, event: ETMEvent):
        """Process event and log to Rerun"""
        self._set_time()

        if event.event_type == EventType.PCNT_UPDATE:
            # 4x int16 counts
            if len(event.payload) >= 8:
                counts = struct.unpack("<4h", event.payload[:8])
                self.pcnt_counts = list(counts)

                # Log time-series for each PCNT channel
                for i, count in enumerate(counts):
                    rr.log(f"etm/pcnt/channel_{i}", rr.Scalars([count]))
                    self.pcnt_history[i].append((event.timestamp, count))

                # Log total count
                total = sum(counts)
                rr.log("etm/pcnt/total", rr.Scalars([total]))

                # Update activity matrix (simplified: diagonal for now)
                for i in range(4):
                    self.activity_matrix[i, i] = min(1.0, counts[i] / 128.0)

                # Log heatmap as image (scale up for visibility)
                heatmap = np.repeat(
                    np.repeat(self.activity_matrix, 32, axis=0), 32, axis=1
                )
                rr.log(
                    "etm/activity_matrix", rr.Image((heatmap * 255).astype(np.uint8))
                )

        elif event.event_type == EventType.THRESHOLD:
            # uint8 channel, uint8 threshold_id, int16 count
            if len(event.payload) >= 4:
                channel, thresh_id, count = struct.unpack("<BBh", event.payload[:4])

                # Log threshold event
                thresh_name = THRESHOLD_NAMES.get(thresh_id, f"#{thresh_id}")
                rr.log(
                    "etm/events/threshold",
                    rr.TextLog(
                        f"THRESHOLD {thresh_name} crossed on CH{channel} at count={count}"
                    ),
                )

                self.threshold_events.append(
                    (event.timestamp, channel, thresh_id, count)
                )

        elif event.event_type == EventType.PATTERN_START:
            # uint8 pattern_id
            if len(event.payload) >= 1:
                pattern_id = event.payload[0]
                self.current_pattern = pattern_id
                pattern_name = PATTERN_NAMES.get(pattern_id, f"#{pattern_id}")

                rr.log("etm/pattern/current", rr.Scalars([pattern_id]))
                rr.log(
                    "etm/events/pattern",
                    rr.TextLog(f"PATTERN {pattern_name} started"),
                )

                # Update state machine
                new_state = pattern_id + 1  # 1=A, 2=B, 3=C
                if new_state != self.current_state:
                    self._log_state_transition(self.current_state, new_state)
                    self.current_state = new_state

        elif event.event_type == EventType.PATTERN_END:
            # uint8 pattern_id, uint32 duration_us
            if len(event.payload) >= 5:
                pattern_id, duration = struct.unpack("<BI", event.payload[:5])
                pattern_name = PATTERN_NAMES.get(pattern_id, f"#{pattern_id}")

                rr.log("etm/pattern/duration_us", rr.Scalars([duration]))
                rr.log(
                    "etm/events/pattern",
                    rr.TextLog(f"PATTERN {pattern_name} ended ({duration} us)"),
                )

                self.current_pattern = None

        elif event.event_type == EventType.DMA_EOF:
            # uint8 channel
            if len(event.payload) >= 1:
                channel = event.payload[0]
                rr.log("etm/events/dma", rr.TextLog(f"DMA EOF on channel {channel}"))

        elif event.event_type == EventType.ETM_EVENT:
            # uint8 event_id, uint8 task_id
            if len(event.payload) >= 2:
                event_id, task_id = event.payload[:2]
                rr.log(
                    "etm/events/etm",
                    rr.TextLog(f"ETM: Event {event_id} -> Task {task_id}"),
                )

        elif event.event_type == EventType.STATE_CHANGE:
            # uint8 old_state, uint8 new_state
            if len(event.payload) >= 2:
                old_state, new_state = event.payload[:2]
                self._log_state_transition(old_state, new_state)
                self.current_state = new_state

        elif event.event_type == EventType.CYCLE_COMPLETE:
            # uint32 cycle_num, uint32 duration_us
            if len(event.payload) >= 8:
                cycle_num, duration = struct.unpack("<II", event.payload[:8])
                self.cycle_count = cycle_num

                rr.log("etm/cycle/count", rr.Scalars([cycle_num]))
                rr.log("etm/cycle/duration_us", rr.Scalars([duration]))
                rr.log(
                    "etm/events/cycle",
                    rr.TextLog(f"Cycle {cycle_num} complete ({duration} us)"),
                )

        elif event.event_type == EventType.HEARTBEAT:
            # uint32 uptime_ms, uint32 cycles
            if len(event.payload) >= 8:
                uptime_ms, cycles = struct.unpack("<II", event.payload[:8])

                rr.log("etm/system/uptime_ms", rr.Scalars([uptime_ms]))
                rr.log("etm/system/total_cycles", rr.Scalars([cycles]))

        elif event.event_type == EventType.TEXT_LOG:
            # Variable length text
            try:
                text = event.payload.decode("utf-8", errors="replace")
                rr.log("etm/log", rr.TextLog(text))
            except:
                pass

    def _log_state_transition(self, old_state: int, new_state: int):
        """Log a state machine transition"""
        old_name = STATE_NAMES.get(old_state, f"S{old_state}")
        new_name = STATE_NAMES.get(new_state, f"S{new_state}")

        rr.log("etm/state/current", rr.Scalars([new_state]))
        rr.log("etm/events/state", rr.TextLog(f"STATE: {old_name} -> {new_name}"))

    def setup_rerun(self):
        """Initialize Rerun recording"""
        rr.init("ETM Fabric Monitor", spawn=True)

        # Log static threshold reference lines
        rr.set_time("step", sequence=0)
        rr.log("etm", rr.TextLog("ETM Fabric Visualization Root"))

    def run(self):
        """Main event loop"""
        self.setup_rerun()

        if not self.connect():
            print("Failed to connect to device")
            return

        print("Monitoring ETM events... (Ctrl+C to stop)")
        print("Rerun viewer should open automatically")

        try:
            while True:
                event = self.read_event()
                if event:
                    self.process_event(event)
                else:
                    # No event, small sleep to avoid busy loop
                    time.sleep(0.001)

        except KeyboardInterrupt:
            print("\nStopping monitor...")
        finally:
            self.close()


def run_demo_mode():
    """Run with synthetic data for testing without hardware"""
    print("Running in DEMO mode with synthetic data...")

    rr.init("ETM Fabric Monitor (Demo)", spawn=True)

    step = 0
    cycle = 0
    pcnt_counts = [0, 0, 0, 0]

    try:
        while True:
            # Simulate pattern execution
            pattern = cycle % 3
            pattern_name = PATTERN_NAMES.get(pattern, f"#{pattern}")

            # Log pattern start
            step += 1
            rr.set_time("step", sequence=step)
            rr.log("etm/pattern/current", rr.Scalars([pattern]))
            rr.log("etm/events/pattern", rr.TextLog(f"PATTERN {pattern_name} started"))
            rr.log("etm/state/current", rr.Scalars([pattern + 1]))

            # Simulate pulse accumulation
            pulses_per_pattern = [8, 32, 128][pattern]
            steps = 20

            for sim_step in range(steps):
                step += 1
                rr.set_time("step", sequence=step)

                # Update counts
                for i in range(4):
                    if pattern == 0:  # Sparse - only channel 0
                        if i == 0:
                            pcnt_counts[i] += pulses_per_pattern // steps
                    elif pattern == 1:  # Medium - channels 0,1
                        if i < 2:
                            pcnt_counts[i] += pulses_per_pattern // steps // 2
                    else:  # Dense - all channels
                        pcnt_counts[i] += pulses_per_pattern // steps // 4

                # Log counts
                for i, count in enumerate(pcnt_counts):
                    rr.log(f"etm/pcnt/channel_{i}", rr.Scalars([count]))
                rr.log("etm/pcnt/total", rr.Scalars([sum(pcnt_counts)]))

                # Check thresholds
                thresholds = [(32, "A"), (64, "B"), (128, "C")]
                for i, count in enumerate(pcnt_counts):
                    for thresh_val, thresh_name in thresholds:
                        if count >= thresh_val and count < thresh_val + 2:
                            rr.log(
                                "etm/events/threshold",
                                rr.TextLog(f"THRESHOLD {thresh_name} crossed on CH{i}"),
                            )

                # Update activity matrix
                matrix = np.zeros((4, 4), dtype=np.float32)
                for i in range(4):
                    matrix[i, i] = min(1.0, pcnt_counts[i] / 128.0)
                heatmap = np.repeat(np.repeat(matrix, 32, axis=0), 32, axis=1)
                rr.log(
                    "etm/activity_matrix", rr.Image((heatmap * 255).astype(np.uint8))
                )

                time.sleep(0.02)

            # Log pattern end and DMA EOF
            step += 1
            rr.set_time("step", sequence=step)
            rr.log("etm/events/pattern", rr.TextLog(f"PATTERN {pattern_name} ended"))
            rr.log("etm/events/dma", rr.TextLog("DMA EOF - PCNT reset"))

            # Reset PCNT (simulating ETM reset)
            pcnt_counts = [0, 0, 0, 0]

            # Log cycle complete
            cycle += 1
            rr.log("etm/cycle/count", rr.Scalars([cycle]))
            rr.log("etm/events/cycle", rr.TextLog(f"Cycle {cycle} complete"))

            time.sleep(0.1)

    except KeyboardInterrupt:
        print("\nDemo stopped.")


def main():
    parser = argparse.ArgumentParser(description="ETM Fabric Monitor with Rerun")
    parser.add_argument(
        "port", nargs="?", default=None, help="Serial port (e.g., /dev/ttyACM0)"
    )
    parser.add_argument(
        "--baud", type=int, default=115200, help="Baud rate (default: 115200)"
    )
    parser.add_argument(
        "--demo", action="store_true", help="Run demo mode with synthetic data"
    )

    args = parser.parse_args()

    if args.demo or args.port is None:
        run_demo_mode()
    else:
        monitor = ETMMonitor(args.port, args.baud)
        monitor.run()


if __name__ == "__main__":
    main()
