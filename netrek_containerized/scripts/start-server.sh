#!/bin/bash
# Start the vanilla Netrek server (headless, no GUI needed)
# Resolve through any symlink so this works when run from ~/bin.
SELF="$0"
while [ -L "$SELF" ]; do
  L="$(readlink "$SELF")"
  case "$L" in /*) SELF="$L" ;; *) SELF="$(dirname "$SELF")/$L" ;; esac
done
REPO_DIR="$(cd "$(dirname "$SELF")/.." && pwd)"
docker compose -f "$REPO_DIR/docker-compose.yml" up server -d
echo ""
echo "Server starting... check status with: docker compose ps"
echo "Connect any client to localhost:2692"
