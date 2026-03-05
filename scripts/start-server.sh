#!/bin/bash
# Start the vanilla Netrek server (headless, no GUI needed)
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
docker compose -f "$REPO_DIR/docker-compose.yml" up server -d
echo ""
echo "Server starting... check status with: docker compose ps"
echo "Connect any client to localhost:2692"
