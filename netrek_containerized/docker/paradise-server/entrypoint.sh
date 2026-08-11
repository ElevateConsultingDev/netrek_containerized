#!/bin/bash
set -e

NETREKDIR="${NETREKDIR:-/usr/local/netrek}"

if [ ! -f "${NETREKDIR}/bin/listen" ]; then
    echo "ERROR: paradise server 'listen' binary not found at ${NETREKDIR}/bin/listen" >&2
    exit 1
fi

export NETREKDIR

echo "Starting Paradise server (NETREKDIR=${NETREKDIR})..."
"${NETREKDIR}/bin/ntstart" "$NETREKDIR"

# ntstart daemonizes, keep container alive
LOGDIR="${NETREKDIR}/logs"
sleep 1
if [ -d "$LOGDIR" ] && ls "$LOGDIR"/*.log 1>/dev/null 2>&1; then
    exec tail -f "$LOGDIR"/*.log 2>/dev/null
else
    exec sleep infinity
fi
