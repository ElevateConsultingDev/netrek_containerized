#!/bin/bash
# Start vanilla server + COW client together (requires XQuartz)
open -a XQuartz 2>/dev/null
sleep 1
xhost +localhost 2>/dev/null
docker compose up
