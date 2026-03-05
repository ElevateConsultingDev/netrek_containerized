#!/bin/bash
# Start the COW client (requires XQuartz + running server)
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
open -a XQuartz 2>/dev/null
sleep 2
DISPLAY=:0 xhost +localhost 2>/dev/null
docker compose -f "$REPO_DIR/docker-compose.yml" up client
