#!/bin/bash
# Stop all running containers
# Resolve through any symlink so this works when run from ~/bin.
SELF="$0"
while [ -L "$SELF" ]; do
  L="$(readlink "$SELF")"
  case "$L" in /*) SELF="$L" ;; *) SELF="$(dirname "$SELF")/$L" ;; esac
done
REPO_DIR="$(cd "$(dirname "$SELF")/.." && pwd)"
docker compose -f "$REPO_DIR/docker-compose.yml" down
