#!/bin/bash
# Stop all running containers
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
docker compose -f "$REPO_DIR/docker-compose.yml" --profile paradise down
