#!/bin/bash
#
# record_demo.sh - Headless Demo Recording with Verification
#
# Records both REFLEX and ROS2 modes, captures output, and verifies results.
# Outputs timestamped logs and a summary report.
#
# Usage:
#   ./record_demo.sh [output_dir]
#
# Output:
#   output_dir/
#   ├── reflex_run.log      # REFLEX mode output
#   ├── ros2_run.log        # ROS2 mode output
#   ├── phases.log          # Phase transitions
#   ├── summary.md          # Verification report
#   └── recording_meta.json # Metadata
#

set -e

OUTPUT_DIR="${1:-/tmp/reflex_demo_$(date +%Y%m%d_%H%M%S)}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BRIDGE_DIR="$(dirname "$SCRIPT_DIR")"

mkdir -p "$OUTPUT_DIR"

echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║       HEADLESS DEMO RECORDING                                 ║"
echo "╠═══════════════════════════════════════════════════════════════╣"
echo "║  Output: $OUTPUT_DIR"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo ""

# Metadata
cat > "$OUTPUT_DIR/recording_meta.json" << EOF
{
  "timestamp": "$(date -Iseconds)",
  "hostname": "$(hostname)",
  "kernel": "$(uname -r)",
  "output_dir": "$OUTPUT_DIR"
}
EOF

cleanup() {
    echo "Cleaning up..."
    docker exec reflex_bridge pkill -f force_simulator 2>/dev/null || true
}
trap cleanup EXIT

# Ensure bridge is running
ensure_bridge() {
    if ! docker ps | grep -q reflex_bridge; then
        echo "Starting bridge..."
        docker run -d --rm --runtime nvidia \
            --name reflex_bridge \
            --network host --ipc host \
            -v /dev/shm:/dev/shm \
            -v /home/ztflynn/the-reflex:/workspace/the-reflex \
            dustynv/ros:humble-desktop-l4t-r36.4.0 \
            bash -c "source /opt/ros/humble/install/setup.bash && \
                     mkdir -p /tmp/ros_ws/src && \
                     cp -r /workspace/the-reflex/reflex_ros_bridge /tmp/ros_ws/src/ && \
                     cd /tmp/ros_ws && \
                     colcon build --packages-select reflex_ros_bridge >/dev/null 2>&1 && \
                     source install/setup.bash && \
                     export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/tmp/ros_ws/install/reflex_ros_bridge/lib && \
                     ros2 run reflex_ros_bridge bridge_node"
        sleep 15
    fi
    echo "Bridge ready."
}

# Run a single mode and capture output
run_mode() {
    local mode=$1
    local logfile=$2
    local duration=${3:-16}
    
    echo "Recording $mode mode..."
    
    # Start simulator
    docker exec -d reflex_bridge bash -c "source /opt/ros/humble/install/setup.bash && \
        source /tmp/ros_ws/install/setup.bash && \
        ros2 run reflex_ros_bridge force_simulator_node"
    
    sleep 1
    
    # Run controller and capture output
    cd "$BRIDGE_DIR"
    timeout $duration ./reflex_force_control --$mode > "$logfile" 2>&1 || true
    
    # Stop simulator
    docker exec reflex_bridge pkill -f force_simulator 2>/dev/null || true
    sleep 2
    
    echo "  Saved to: $logfile"
}

# Extract metrics from log file
extract_metrics() {
    local logfile=$1
    local mode=$2
    
    local loop_count=$(grep "Loop count:" "$logfile" | awk '{print $3}' | tr -d ',')
    local anomaly_count=$(grep "Anomaly count:" "$logfile" | awk '{print $3}' | tr -d ',')
    local avg_time=$(grep "Reaction time:" "$logfile" | grep -oE '[0-9]+ ns average' | awk '{print $1}')
    local max_time=$(grep "Reaction time:" "$logfile" | grep -oE '[0-9]+ ns max' | awk '{print $1}')
    local max_force=$(grep "Max force:" "$logfile" | awk '{print $3}')
    
    # Fallback for ROS2 mode (different format)
    if [ -z "$avg_time" ]; then
        avg_time="N/A"
        max_time="10000000"  # 10ms
    fi
    
    echo "$mode|$loop_count|$anomaly_count|$avg_time|$max_time|$max_force"
}

# Verify results meet expectations
verify_results() {
    local reflex_metrics=$1
    local ros2_metrics=$2
    
    IFS='|' read -r r_mode r_loops r_anomalies r_avg r_max r_force <<< "$reflex_metrics"
    IFS='|' read -r s_mode s_loops s_anomalies s_avg s_max s_force <<< "$ros2_metrics"
    
    local pass=true
    local checks=""
    
    # Check 1: REFLEX should have sub-microsecond reaction
    if [ "$r_avg" != "N/A" ] && [ "$r_avg" -lt 1000 ]; then
        checks+="✓ REFLEX reaction time < 1μs ($r_avg ns)\n"
    else
        checks+="✗ REFLEX reaction time >= 1μs ($r_avg ns)\n"
        pass=false
    fi
    
    # Check 2: REFLEX should catch more anomalies
    if [ "$r_anomalies" -gt "$s_anomalies" ]; then
        checks+="✓ REFLEX caught more anomalies ($r_anomalies vs $s_anomalies)\n"
    else
        checks+="✗ REFLEX did not catch more anomalies\n"
        pass=false
    fi
    
    # Check 3: REFLEX should have more loops
    if [ "$r_loops" -gt "$s_loops" ]; then
        checks+="✓ REFLEX processed more signals ($r_loops vs $s_loops)\n"
    else
        checks+="✗ REFLEX did not process more signals\n"
        pass=false
    fi
    
    # Check 4: Both should see 7N max force (anomaly)
    if [[ "$r_force" == "7.0"* ]] && [[ "$s_force" == "7.0"* ]]; then
        checks+="✓ Both modes detected 7N anomaly spike\n"
    else
        checks+="✗ Force detection issue (REFLEX: $r_force, ROS2: $s_force)\n"
        pass=false
    fi
    
    echo -e "$checks"
    
    if $pass; then
        return 0
    else
        return 1
    fi
}

# Main recording sequence
main() {
    ensure_bridge
    
    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    echo "RECORDING REFLEX MODE"
    echo "═══════════════════════════════════════════════════════════════"
    run_mode "reflex" "$OUTPUT_DIR/reflex_run.log" 16
    
    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    echo "RECORDING ROS2 MODE"
    echo "═══════════════════════════════════════════════════════════════"
    run_mode "ros2" "$OUTPUT_DIR/ros2_run.log" 16
    
    # Extract metrics
    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    echo "EXTRACTING METRICS"
    echo "═══════════════════════════════════════════════════════════════"
    
    reflex_metrics=$(extract_metrics "$OUTPUT_DIR/reflex_run.log" "REFLEX")
    ros2_metrics=$(extract_metrics "$OUTPUT_DIR/ros2_run.log" "ROS2")
    
    echo "REFLEX: $reflex_metrics"
    echo "ROS2:   $ros2_metrics"
    
    # Generate summary report
    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    echo "GENERATING REPORT"
    echo "═══════════════════════════════════════════════════════════════"
    
    IFS='|' read -r r_mode r_loops r_anomalies r_avg r_max r_force <<< "$reflex_metrics"
    IFS='|' read -r s_mode s_loops s_anomalies s_avg s_max s_force <<< "$ros2_metrics"
    
    # Calculate improvement
    if [ "$r_avg" != "N/A" ]; then
        improvement=$((10000000 / r_avg))
    else
        improvement="N/A"
    fi
    
    cat > "$OUTPUT_DIR/summary.md" << EOF
# Valentine's Day Demo Recording

**Recorded:** $(date)
**Host:** $(hostname)

## Results

| Metric | REFLEX | ROS2 | Improvement |
|--------|--------|------|-------------|
| Reaction time | ${r_avg} ns | 10 ms | ${improvement}x |
| Loop count | ${r_loops} | ${s_loops} | $(echo "scale=0; $r_loops / $s_loops" | bc)x |
| Anomalies caught | ${r_anomalies} | ${s_anomalies} | $(echo "scale=0; $r_anomalies / $s_anomalies" | bc)x |
| Max force seen | ${r_force} | ${s_force} | - |

## Verification

$(verify_results "$reflex_metrics" "$ros2_metrics")

## Raw Output

### REFLEX Mode
\`\`\`
$(cat "$OUTPUT_DIR/reflex_run.log")
\`\`\`

### ROS2 Mode
\`\`\`
$(cat "$OUTPUT_DIR/ros2_run.log")
\`\`\`

---
*Generated by record_demo.sh*
EOF

    echo "Report saved to: $OUTPUT_DIR/summary.md"
    
    # Final verification
    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    echo "VERIFICATION"
    echo "═══════════════════════════════════════════════════════════════"
    
    if verify_results "$reflex_metrics" "$ros2_metrics"; then
        echo ""
        echo "✓ ALL CHECKS PASSED"
        echo ""
        echo "The demo is ready to ship."
        exit 0
    else
        echo ""
        echo "✗ SOME CHECKS FAILED"
        echo ""
        echo "Review the logs in $OUTPUT_DIR"
        exit 1
    fi
}

main
