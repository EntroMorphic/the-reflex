#!/bin/bash
# Check if the Isaac ROS container build is still running

THOR_HOST="ztflynn@10.42.0.2"
THOR_PORT="11965"

echo "=== Build Status on Thor ==="
echo ""

# Check if build process is running
RUNNING=$(ssh -p $THOR_PORT $THOR_HOST 'ps aux | grep -v grep | grep "jetson-containers build" | wc -l')

if [ "$RUNNING" -gt 0 ]; then
    echo "Status: BUILDING"
    echo ""
    echo "Last 10 lines of log:"
    ssh -p $THOR_PORT $THOR_HOST 'tail -10 /tmp/isaac_ros_build.log'
else
    echo "Status: NOT RUNNING"
    echo ""
    echo "Checking for success/failure..."
    ssh -p $THOR_PORT $THOR_HOST 'grep -E "(Successfully|Failed)" /tmp/isaac_ros_build.log | tail -5'
fi

echo ""
echo "=== Docker Images ==="
ssh -p $THOR_PORT $THOR_HOST 'docker images | grep isaac-ros | head -10'
