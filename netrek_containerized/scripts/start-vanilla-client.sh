#!/bin/bash
# Launch the VANILLA upstream X11 COW client that is built inside the server
# container, displaying it on the Mac through XQuartz. This is the original
# Linux client, NOT the Netrek COM (SDL2) build -- for that use
# start-com-client.sh.
#
# Prereq (one time): XQuartz -> Preferences -> Security ->
#   "Allow connections from network clients" = ON, then restart XQuartz.
#
# Usage: ./start-vanilla-client.sh [container-name]   (default: vanilla-netrek-server)
set -e
CONTAINER="${1:-vanilla-netrek-server}"

# Bring XQuartz up and authorise local connections.
open -a XQuartz 2>/dev/null || true
export DISPLAY=:0
xhost +localhost >/dev/null 2>&1 || xhost + >/dev/null 2>&1 || \
  echo "warning: xhost failed -- is XQuartz running? (open a fresh terminal after it starts)"

# Run the in-container X11 client, pointing its display back at the host XQuartz.
exec docker exec -e DISPLAY=host.docker.internal:0 -it "$CONTAINER" \
  sh -c 'cd /usr/local/src/netrek/netrek-client-cow && ./netrek-client-cow -h localhost -p 2592'
