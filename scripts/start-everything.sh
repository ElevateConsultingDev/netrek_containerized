#!/bin/bash
# Start vanilla server + COW client together (requires XQuartz)
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
open -a XQuartz 2>/dev/null
sleep 1
xhost +localhost 2>/dev/null
docker compose -f "$REPO_DIR/docker-compose.yml" up
