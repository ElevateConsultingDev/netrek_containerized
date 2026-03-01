#!/bin/bash
# Start the pygame development client (requires XQuartz + running server)
open -a XQuartz 2>/dev/null
sleep 1
xhost +localhost 2>/dev/null
docker compose up pygame-client
