#!/bin/bash
#
# run_demo.sh - Valentine's Day Demo Launcher
#
# Usage:
#   ./run_demo.sh reflex   # 10kHz Reflex mode (default)
#   ./run_demo.sh ros2     # 100Hz ROS2 baseline mode
#   ./run_demo.sh compare  # Run both modes back-to-back
#

set -e

MODE=${1:-reflex}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BRIDGE_DIR="$(dirname "$SCRIPT_DIR")"
THOR_REFLEX_DIR="/home/ztflynn/the-reflex/reflex_ros_bridge"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

banner() {
    echo -e "${BLUE}╔═══════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║       THE REFLEX: VALENTINE'S DAY DEMO                        ║${NC}"
    echo -e "${BLUE}║       Mode: ${YELLOW}$1${BLUE}                                              ║${NC}"
    echo -e "${BLUE}╚═══════════════════════════════════════════════════════════════╝${NC}"
    echo
}

cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    docker exec reflex_bridge pkill -f force_simulator 2>/dev/null || true
    docker exec entromorphic-dev pkill -f telemetry_dashboard 2>/dev/null || true
    pkill -f reflex_force_control 2>/dev/null || true
}

trap cleanup EXIT

start_bridge() {
    echo -e "${GREEN}Starting ROS2 bridge...${NC}"
    
    # Check if already running
    if docker ps | grep -q reflex_bridge; then
        echo "  Bridge already running"
        return
    fi
    
    # Clean old shared memory
    docker run --rm -v /dev/shm:/dev/shm alpine sh -c "rm /dev/shm/reflex_* 2>/dev/null" 2>/dev/null || true
    
    # Start bridge container
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
    
    echo "  Waiting for bridge to initialize..."
    sleep 15
    
    # Verify
    if docker ps | grep -q reflex_bridge; then
        echo -e "  ${GREEN}Bridge running${NC}"
    else
        echo -e "  ${RED}Bridge failed to start${NC}"
        docker logs reflex_bridge 2>&1 | tail -10
        exit 1
    fi
}

start_simulator() {
    echo -e "${GREEN}Starting force simulator...${NC}"
    
    docker exec -d reflex_bridge bash -c "source /opt/ros/humble/install/setup.bash && \
        source /tmp/ros_ws/install/setup.bash && \
        ros2 run reflex_ros_bridge force_simulator_node"
    
    sleep 1
    echo -e "  ${GREEN}Simulator running${NC}"
}

start_telemetry() {
    echo -e "${GREEN}Starting telemetry dashboard...${NC}"
    
    # Start in entromorphic-dev container (has Rerun)
    docker exec -d entromorphic-dev bash -c "cd /workspace && \
        python3 /workspace/the-reflex/reflex_ros_bridge/scripts/telemetry_dashboard.py --spawn" 2>/dev/null || {
        echo -e "  ${YELLOW}Telemetry dashboard not available (Rerun not configured)${NC}"
        return
    }
    
    echo -e "  ${GREEN}Dashboard running (check Rerun viewer)${NC}"
}

run_controller() {
    local mode=$1
    local duration=${2:-16}  # Default 16 seconds (one full cycle + buffer)
    
    echo -e "${GREEN}Running controller in ${YELLOW}$mode${GREEN} mode...${NC}"
    
    cd "$THOR_REFLEX_DIR"
    
    if [ "$mode" == "reflex" ]; then
        timeout $duration ./reflex_force_control --reflex 2>&1
    else
        timeout $duration ./reflex_force_control --ros2 2>&1
    fi
}

# Main
case "$MODE" in
    reflex)
        banner "REFLEX (10kHz)"
        start_bridge
        start_simulator
        # start_telemetry  # Optional
        echo
        run_controller reflex 16
        ;;
    
    ros2)
        banner "ROS2 BASELINE (100Hz)"
        start_bridge
        start_simulator
        # start_telemetry  # Optional
        echo
        run_controller ros2 16
        ;;
    
    compare)
        banner "A/B COMPARISON"
        start_bridge
        
        echo -e "${YELLOW}═══════════════════════════════════════════════════════════════${NC}"
        echo -e "${YELLOW}TEST A: REFLEX MODE (10kHz)${NC}"
        echo -e "${YELLOW}═══════════════════════════════════════════════════════════════${NC}"
        start_simulator
        run_controller reflex 16
        
        # Stop simulator, wait for reset
        docker exec reflex_bridge pkill -f force_simulator 2>/dev/null || true
        sleep 3
        
        echo
        echo -e "${YELLOW}═══════════════════════════════════════════════════════════════${NC}"
        echo -e "${YELLOW}TEST B: ROS2 MODE (100Hz) - BASELINE${NC}"
        echo -e "${YELLOW}═══════════════════════════════════════════════════════════════${NC}"
        start_simulator
        run_controller ros2 16
        
        echo
        echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
        echo -e "${GREEN}COMPARISON COMPLETE${NC}"
        echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
        echo
        echo "Key difference: REFLEX samples at 100μs, ROS2 at 10ms"
        echo "This means REFLEX can react 100x faster to force threshold breaches."
        ;;
    
    *)
        echo "Usage: $0 [reflex|ros2|compare]"
        echo
        echo "Modes:"
        echo "  reflex   10kHz Reflex mode (default)"
        echo "  ros2     100Hz ROS2 baseline mode"
        echo "  compare  Run both modes back-to-back"
        exit 1
        ;;
esac

echo
echo -e "${GREEN}Demo complete.${NC}"
