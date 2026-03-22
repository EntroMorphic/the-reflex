#!/bin/bash
# Monitor the Isaac ROS container build on Thor

THOR_HOST="ztflynn@10.42.0.2"
THOR_PORT="11965"
LOG_FILE="/tmp/isaac_ros_build.log"

echo "Monitoring Isaac ROS build on Thor..."
echo "Press Ctrl+C to stop"
echo ""

ssh -p $THOR_PORT $THOR_HOST "tail -f $LOG_FILE"
