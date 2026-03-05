#!/bin/bash
# Start the COW client (requires XQuartz + running server)
open -a XQuartz 2>/dev/null
sleep 2
DISPLAY=:0 xhost +localhost 2>/dev/null
docker compose up client
