#!/bin/bash
set -e

NETREK_DIR="/usr/local/src/netrek"
SERVER_BIN="${NETREK_DIR}/netrek-server/here/bin/netrekd"

if [ ! -x "$SERVER_BIN" ]; then
    echo "ERROR: netrekd not found at ${SERVER_BIN}" >&2
    exit 1
fi

echo "Starting netrekd server..."
"$SERVER_BIN"

# netrekd daemonizes (forks to background), so wait for it to appear
# then keep the container alive by following its log
sleep 1
LOGDIR="${NETREK_DIR}/netrek-server/here/var/log"
if [ -d "$LOGDIR" ]; then
    exec tail -f "$LOGDIR"/* 2>/dev/null
else
    exec sleep infinity
fi
