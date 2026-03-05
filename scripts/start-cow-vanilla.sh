#!/bin/bash
#
# Start COW client connecting to the vanilla Netrek server (host port 2692).
# Requires: vanilla-netrek-server running, XQuartz open.

xhost +localhost 2>/dev/null

docker run --rm -d \
    -e DISPLAY=host.docker.internal:0 \
    --name cow-vanilla \
    vanilla-netrek-image \
    bash -c "cd /usr/local/src/netrek/netrek-client-cow && ./netrek-client-cow -h host.docker.internal -p 2692"
