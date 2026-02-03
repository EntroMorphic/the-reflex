#!/usr/bin/env python3
"""
rerun_etm_monitor.py - Real-time ETM Fabric Visualization

Receives structured events from ESP32-C6 via USB serial and visualizes
the autonomous hardware computation in Rerun.

Entity Hierarchy (each gets its own panel):
  /pcnt
    /channel_0, /channel_1, /channel_2, /channel_3  - Time-series counts
    /total                                           - Sum of all channels
  /patterns
    /current                                         - Active pattern ID (scalar)
    /timeline                                        - Pattern start/end events
    /duration                                        - Pattern execution time
  /thresholds
    /crossings                                       - Threshold crossing events
    /channel_0, /channel_1, ...                      - Per-channel threshold markers
  /state_machine
    /current                                         - Current state ID
    /transitions                                     - State transition log
  /dma
    /eof                                             - DMA EOF events
    /channel_0, /channel_1, /channel_2               - Per-channel DMA activity
  /etm
    /events                                          - Raw ETM event->task firings
  /cycles
    /count                                           - Cycle counter
    /duration                                        - Cycle duration
    /log                                             - Cycle completion events
  /system
    /uptime                                          - System uptime
    /heartbeat                                       - Heartbeat events
  /activity
    /matrix                                          - 4x4 heatmap
  /log
    /all                                             - Combined text log

Usage:
  python rerun_etm_monitor.py /dev/ttyACM0
  python rerun_etm_monitor.py --demo  # Demo mode with synthetic data
"""

import argparse
import struct
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
    TEXT_LOG = 0x20


# Pattern names
PATTERN_NAMES = {
    0: "A (sparse)",
    1: "B (medium)",
    2: "C (dense)",
}

# State names
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

# Threshold values for plotting reference lines
THRESHOLD_VALUES = {
    0: 32,
    1: 64,
    2: 128,
}


@dataclass
class ETMEvent:
    """Parsed ETM event from serial"""

    event_type: EventType
    timestamp: float
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
        self.pattern_start_time = 0
        self.start_time = time.time()
        self.step = 0

        # History
        self.pcnt_history = [deque(maxlen=1000) for _ in range(4)]
        self.threshold_events = deque(maxlen=100)

        # Activity matrix
        self.activity_matrix = np.zeros((4, 4), dtype=np.float32)

    def connect(self) -> bool:
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.1)
            print(f"Connected to {self.port} at {self.baud} baud")
            return True
        except serial.SerialException as e:
            print(f"Failed to connect: {e}")
            return False

    def close(self):
        if self.ser:
            self.ser.close()

    def read_event(self) -> Optional[ETMEvent]:
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
            return None

        try:
            return ETMEvent(
                event_type=EventType(event_type),
                timestamp=time.time() - self.start_time,
                payload=payload,
            )
        except ValueError:
            return None

    def _set_time(self):
        self.step += 1
        rr.set_time("step", sequence=self.step)

    def process_event(self, event: ETMEvent):
        """Process event and log to appropriate Rerun entities"""
        self._set_time()

        if event.event_type == EventType.PCNT_UPDATE:
            self._handle_pcnt_update(event)
        elif event.event_type == EventType.THRESHOLD:
            self._handle_threshold(event)
        elif event.event_type == EventType.PATTERN_START:
            self._handle_pattern_start(event)
        elif event.event_type == EventType.PATTERN_END:
            self._handle_pattern_end(event)
        elif event.event_type == EventType.DMA_EOF:
            self._handle_dma_eof(event)
        elif event.event_type == EventType.ETM_EVENT:
            self._handle_etm_event(event)
        elif event.event_type == EventType.STATE_CHANGE:
            self._handle_state_change(event)
        elif event.event_type == EventType.CYCLE_COMPLETE:
            self._handle_cycle_complete(event)
        elif event.event_type == EventType.HEARTBEAT:
            self._handle_heartbeat(event)
        elif event.event_type == EventType.TEXT_LOG:
            self._handle_text_log(event)

    def _handle_pcnt_update(self, event: ETMEvent):
        """PCNT counts - dedicated panel per channel"""
        if len(event.payload) >= 8:
            counts = struct.unpack("<4h", event.payload[:8])
            self.pcnt_counts = list(counts)

            # Log each channel to its own entity
            for i, count in enumerate(counts):
                rr.log(f"pcnt/channel_{i}", rr.Scalars([count]))
                self.pcnt_history[i].append((event.timestamp, count))

            # Log total
            total = sum(counts)
            rr.log("pcnt/total", rr.Scalars([total]))

            # Calculate pulse rates (delta from last reading)
            rates = []
            for i in range(4):
                if len(self.pcnt_history[i]) >= 2:
                    # Rate = change since last sample
                    prev = (
                        self.pcnt_history[i][-2][1]
                        if len(self.pcnt_history[i]) >= 2
                        else 0
                    )
                    rate = max(0, counts[i] - prev)
                else:
                    rate = counts[i]
                rates.append(rate)

            # Create 4-channel bar chart visualization
            # Layout: 4 vertical bars, each showing pulse rate (0-32 scale)
            bar_width = 30
            bar_gap = 10
            chart_height = 128
            chart_width = 4 * bar_width + 3 * bar_gap

            chart = np.zeros((chart_height, chart_width, 3), dtype=np.uint8)

            # Channel colors: Blue, Green, Yellow, Red
            colors = [
                (66, 133, 244),  # CH0 - Blue
                (52, 168, 83),  # CH1 - Green
                (251, 188, 4),  # CH2 - Yellow
                (234, 67, 53),  # CH3 - Red
            ]

            max_rate = 32  # Expected max pulses per update

            for i in range(4):
                x_start = i * (bar_width + bar_gap)
                x_end = x_start + bar_width

                # Normalize rate to bar height
                bar_height = min(
                    chart_height, int((rates[i] / max_rate) * chart_height)
                )

                if bar_height > 0:
                    # Draw bar from bottom up
                    y_start = chart_height - bar_height
                    chart[y_start:chart_height, x_start:x_end] = colors[i]

                # Draw channel label area at bottom (dim background)
                chart[chart_height - 12 : chart_height, x_start:x_end] = (40, 40, 40)

            rr.log("activity/pulse_rates", rr.Image(chart))

            # Also log individual rates as scalars for time-series
            for i, rate in enumerate(rates):
                rr.log(f"activity/rate_ch{i}", rr.Scalars([rate]))

    def _handle_threshold(self, event: ETMEvent):
        """Threshold crossings - dedicated threshold panel"""
        if len(event.payload) >= 4:
            channel, thresh_id, count = struct.unpack("<BBh", event.payload[:4])
            thresh_name = THRESHOLD_NAMES.get(thresh_id, f"#{thresh_id}")
            thresh_val = THRESHOLD_VALUES.get(thresh_id, 0)

            # Log to threshold panel
            rr.log(
                "thresholds/crossings",
                rr.TextLog(f"CH{channel}: {thresh_name} crossed at {count}"),
            )

            # Log threshold marker value for this channel
            rr.log(f"thresholds/channel_{channel}", rr.Scalars([thresh_val]))

            # Also log to combined log
            rr.log(
                "log/all",
                rr.TextLog(f"[THRESHOLD] CH{channel} crossed {thresh_name} at {count}"),
            )

            self.threshold_events.append((event.timestamp, channel, thresh_id, count))

    def _handle_pattern_start(self, event: ETMEvent):
        """Pattern start - dedicated patterns panel"""
        if len(event.payload) >= 1:
            pattern_id = event.payload[0]
            self.current_pattern = pattern_id
            self.pattern_start_time = event.timestamp
            pattern_name = PATTERN_NAMES.get(pattern_id, f"#{pattern_id}")

            # Log to patterns panel
            rr.log("patterns/current", rr.Scalars([pattern_id]))
            rr.log("patterns/timeline", rr.TextLog(f"START: {pattern_name}"))

            # Log to combined log
            rr.log("log/all", rr.TextLog(f"[PATTERN] {pattern_name} started"))

            # Trigger state change
            new_state = pattern_id + 1
            if new_state != self.current_state:
                self._log_state_transition(self.current_state, new_state)
                self.current_state = new_state

    def _handle_pattern_end(self, event: ETMEvent):
        """Pattern end - log duration"""
        if len(event.payload) >= 5:
            pattern_id, duration = struct.unpack("<BI", event.payload[:5])
            pattern_name = PATTERN_NAMES.get(pattern_id, f"#{pattern_id}")

            # Log to patterns panel
            rr.log("patterns/duration", rr.Scalars([duration]))
            rr.log(
                "patterns/timeline", rr.TextLog(f"END: {pattern_name} ({duration} us)")
            )

            # Log to combined log
            rr.log(
                "log/all", rr.TextLog(f"[PATTERN] {pattern_name} ended ({duration} us)")
            )

            self.current_pattern = None

    def _handle_dma_eof(self, event: ETMEvent):
        """DMA EOF - dedicated DMA panel"""
        if len(event.payload) >= 1:
            channel = event.payload[0]

            # Log to DMA panel
            rr.log("dma/eof", rr.TextLog(f"EOF on channel {channel}"))
            rr.log(f"dma/channel_{channel}", rr.Scalars([1]))  # Pulse indicator

            # Log to combined log
            rr.log("log/all", rr.TextLog(f"[DMA] EOF on channel {channel}"))

    def _handle_etm_event(self, event: ETMEvent):
        """ETM event->task - dedicated ETM panel"""
        if len(event.payload) >= 2:
            event_id, task_id = event.payload[:2]

            # Log to ETM panel
            rr.log("etm/events", rr.TextLog(f"Event {event_id} -> Task {task_id}"))

            # Log to combined log
            rr.log("log/all", rr.TextLog(f"[ETM] Event {event_id} -> Task {task_id}"))

    def _handle_state_change(self, event: ETMEvent):
        """State change - handled via _log_state_transition"""
        if len(event.payload) >= 2:
            old_state, new_state = event.payload[:2]
            self._log_state_transition(old_state, new_state)
            self.current_state = new_state

    def _log_state_transition(self, old_state: int, new_state: int):
        """Log state machine transition to dedicated panel"""
        old_name = STATE_NAMES.get(old_state, f"S{old_state}")
        new_name = STATE_NAMES.get(new_state, f"S{new_state}")

        # Log to state machine panel
        rr.log("state_machine/current", rr.Scalars([new_state]))
        rr.log("state_machine/transitions", rr.TextLog(f"{old_name} -> {new_name}"))

        # Log to combined log
        rr.log("log/all", rr.TextLog(f"[STATE] {old_name} -> {new_name}"))

    def _handle_cycle_complete(self, event: ETMEvent):
        """Cycle complete - dedicated cycles panel"""
        if len(event.payload) >= 8:
            cycle_num, duration = struct.unpack("<II", event.payload[:8])
            self.cycle_count = cycle_num

            # Log to cycles panel
            rr.log("cycles/count", rr.Scalars([cycle_num]))
            rr.log("cycles/duration", rr.Scalars([duration]))
            rr.log("cycles/log", rr.TextLog(f"Cycle {cycle_num} ({duration} us)"))

            # Log to combined log
            rr.log(
                "log/all", rr.TextLog(f"[CYCLE] #{cycle_num} complete ({duration} us)")
            )

    def _handle_heartbeat(self, event: ETMEvent):
        """Heartbeat - dedicated system panel"""
        if len(event.payload) >= 8:
            uptime_ms, cycles = struct.unpack("<II", event.payload[:8])

            # Log to system panel
            rr.log("system/uptime", rr.Scalars([uptime_ms]))
            rr.log(
                "system/heartbeat",
                rr.TextLog(f"Uptime: {uptime_ms}ms, Cycles: {cycles}"),
            )

    def _handle_text_log(self, event: ETMEvent):
        """Text log"""
        try:
            text = event.payload.decode("utf-8", errors="replace")
            rr.log("log/all", rr.TextLog(f"[LOG] {text}"))
        except:
            pass

    def setup_rerun(self):
        """Initialize Rerun with entity hierarchy"""
        rr.init("ETM Fabric Monitor", spawn=True)
        rr.set_time("step", sequence=0)

        # Log static descriptions for each panel
        rr.log("pcnt", rr.TextLog("Pulse Counter Channels"), static=True)
        rr.log("patterns", rr.TextLog("DMA Pattern Execution"), static=True)
        rr.log("thresholds", rr.TextLog("PCNT Threshold Crossings"), static=True)
        rr.log("state_machine", rr.TextLog("Compute State Machine"), static=True)
        rr.log("dma", rr.TextLog("DMA Transfer Events"), static=True)
        rr.log("etm", rr.TextLog("ETM Event->Task Firings"), static=True)
        rr.log("cycles", rr.TextLog("Compute Cycles"), static=True)
        rr.log("system", rr.TextLog("System Status"), static=True)
        rr.log("activity", rr.TextLog("Channel Pulse Rates (bar chart)"), static=True)
        rr.log("log", rr.TextLog("Combined Event Log"), static=True)

    def run(self):
        """Main event loop"""
        self.setup_rerun()

        if not self.connect():
            print("Failed to connect to device")
            return

        print("Monitoring ETM events... (Ctrl+C to stop)")
        print("Rerun viewer should open automatically")
        print("\nEntity panels available:")
        print("  /pcnt          - PCNT channel counts (time-series)")
        print("  /patterns      - Pattern execution timeline")
        print("  /thresholds    - Threshold crossing events")
        print("  /state_machine - State transitions")
        print("  /dma           - DMA EOF events")
        print("  /etm           - ETM event->task firings")
        print("  /cycles        - Compute cycle stats")
        print("  /system        - Heartbeat and uptime")
        print("  /activity      - 4x4 channel heatmap")
        print("  /log           - Combined text log")

        try:
            while True:
                event = self.read_event()
                if event:
                    self.process_event(event)
                else:
                    time.sleep(0.001)
        except KeyboardInterrupt:
            print("\nStopping monitor...")
        finally:
            self.close()


def run_demo_mode():
    """Run with synthetic data for testing without hardware"""
    print("Running in DEMO mode with synthetic data...")

    rr.init("ETM Fabric Monitor (Demo)", spawn=True)

    # Log static panel descriptions
    rr.set_time("step", sequence=0)
    rr.log("pcnt", rr.TextLog("Pulse Counter Channels"), static=True)
    rr.log("patterns", rr.TextLog("DMA Pattern Execution"), static=True)
    rr.log("thresholds", rr.TextLog("PCNT Threshold Crossings"), static=True)
    rr.log("state_machine", rr.TextLog("Compute State Machine"), static=True)
    rr.log("dma", rr.TextLog("DMA Transfer Events"), static=True)
    rr.log("cycles", rr.TextLog("Compute Cycles"), static=True)
    rr.log("activity", rr.TextLog("Channel Activity Matrix"), static=True)
    rr.log("log", rr.TextLog("Combined Event Log"), static=True)

    step = 0
    cycle = 0
    pcnt_counts = [0, 0, 0, 0]
    current_state = 0

    try:
        while True:
            # Simulate pattern execution
            pattern = cycle % 3
            pattern_name = PATTERN_NAMES.get(pattern, f"#{pattern}")

            # Pattern start
            step += 1
            rr.set_time("step", sequence=step)

            new_state = pattern + 1
            old_state_name = STATE_NAMES.get(current_state, f"S{current_state}")
            new_state_name = STATE_NAMES.get(new_state, f"S{new_state}")

            rr.log("patterns/current", rr.Scalars([pattern]))
            rr.log("patterns/timeline", rr.TextLog(f"START: {pattern_name}"))
            rr.log("state_machine/current", rr.Scalars([new_state]))
            rr.log(
                "state_machine/transitions",
                rr.TextLog(f"{old_state_name} -> {new_state_name}"),
            )
            rr.log("log/all", rr.TextLog(f"[PATTERN] {pattern_name} started"))
            rr.log(
                "log/all", rr.TextLog(f"[STATE] {old_state_name} -> {new_state_name}")
            )

            current_state = new_state

            # Simulate pulse accumulation
            pulses_per_pattern = [8, 32, 128][pattern]
            steps = 20
            pattern_start = time.time()

            for sim_step in range(steps):
                step += 1
                rr.set_time("step", sequence=step)

                # Update counts based on pattern
                for i in range(4):
                    if pattern == 0:  # Sparse - only channel 0
                        if i == 0:
                            pcnt_counts[i] += pulses_per_pattern // steps
                    elif pattern == 1:  # Medium - channels 0,1
                        if i < 2:
                            pcnt_counts[i] += pulses_per_pattern // steps // 2
                    else:  # Dense - all channels
                        pcnt_counts[i] += pulses_per_pattern // steps // 4

                # Log PCNT to dedicated channels
                for i, count in enumerate(pcnt_counts):
                    rr.log(f"pcnt/channel_{i}", rr.Scalars([count]))
                rr.log("pcnt/total", rr.Scalars([sum(pcnt_counts)]))

                # Check thresholds
                thresholds = [(32, 0, "A"), (64, 1, "B"), (128, 2, "C")]
                for i, count in enumerate(pcnt_counts):
                    for thresh_val, thresh_id, thresh_name in thresholds:
                        if count >= thresh_val and count < thresh_val + 2:
                            rr.log(
                                "thresholds/crossings",
                                rr.TextLog(
                                    f"CH{i}: {thresh_name} ({thresh_val}) crossed at {count}"
                                ),
                            )
                            rr.log(f"thresholds/channel_{i}", rr.Scalars([thresh_val]))
                            rr.log(
                                "log/all",
                                rr.TextLog(
                                    f"[THRESHOLD] CH{i} crossed {thresh_name} at {count}"
                                ),
                            )

                # Calculate pulse rates for bar chart
                rates = []
                for i in range(4):
                    if pattern == 0:  # Sparse - only channel 0
                        rate = pulses_per_pattern // steps if i == 0 else 0
                    elif pattern == 1:  # Medium - channels 0,1
                        rate = pulses_per_pattern // steps // 2 if i < 2 else 0
                    else:  # Dense - all channels
                        rate = pulses_per_pattern // steps // 4
                    rates.append(rate)

                # Create 4-channel bar chart visualization
                bar_width = 30
                bar_gap = 10
                chart_height = 128
                chart_width = 4 * bar_width + 3 * bar_gap

                chart = np.zeros((chart_height, chart_width, 3), dtype=np.uint8)

                colors = [
                    (66, 133, 244),  # CH0 - Blue
                    (52, 168, 83),  # CH1 - Green
                    (251, 188, 4),  # CH2 - Yellow
                    (234, 67, 53),  # CH3 - Red
                ]

                max_rate = 32

                for i in range(4):
                    x_start = i * (bar_width + bar_gap)
                    x_end = x_start + bar_width

                    bar_height = min(
                        chart_height, int((rates[i] / max_rate) * chart_height)
                    )

                    if bar_height > 0:
                        y_start = chart_height - bar_height
                        chart[y_start:chart_height, x_start:x_end] = colors[i]

                    chart[chart_height - 12 : chart_height, x_start:x_end] = (
                        40,
                        40,
                        40,
                    )

                rr.log("activity/pulse_rates", rr.Image(chart))

                for i, rate in enumerate(rates):
                    rr.log(f"activity/rate_ch{i}", rr.Scalars([rate]))

                time.sleep(0.02)

            # Pattern end
            step += 1
            rr.set_time("step", sequence=step)
            duration = int((time.time() - pattern_start) * 1000000)

            rr.log("patterns/duration", rr.Scalars([duration]))
            rr.log(
                "patterns/timeline", rr.TextLog(f"END: {pattern_name} ({duration} us)")
            )
            rr.log(
                "log/all", rr.TextLog(f"[PATTERN] {pattern_name} ended ({duration} us)")
            )

            # DMA EOF
            rr.log("dma/eof", rr.TextLog(f"EOF on channel 0"))
            rr.log("dma/channel_0", rr.Scalars([1]))
            rr.log("log/all", rr.TextLog(f"[DMA] EOF on channel 0 - PCNT reset"))

            # Reset PCNT
            pcnt_counts = [0, 0, 0, 0]

            # Cycle complete
            cycle += 1
            rr.log("cycles/count", rr.Scalars([cycle]))
            rr.log("cycles/duration", rr.Scalars([duration]))
            rr.log("cycles/log", rr.TextLog(f"Cycle {cycle} ({duration} us)"))
            rr.log("log/all", rr.TextLog(f"[CYCLE] #{cycle} complete ({duration} us)"))

            # Return to idle
            step += 1
            rr.set_time("step", sequence=step)
            rr.log("state_machine/current", rr.Scalars([0]))
            rr.log("state_machine/transitions", rr.TextLog(f"{new_state_name} -> IDLE"))

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
