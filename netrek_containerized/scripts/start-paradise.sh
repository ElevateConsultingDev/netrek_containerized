#!/bin/bash
# Start the Paradise server
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
docker compose -f "$REPO_DIR/docker-compose.yml" up paradise-server -d
echo ""
echo "Paradise server starting... check status with: docker compose ps"
echo "Connect any client to localhost:2792"
