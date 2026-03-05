#!/bin/bash
# Start the Paradise server (don't run alongside vanilla - same port)
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
docker compose -f "$REPO_DIR/docker-compose.yml" --profile paradise up paradise-server -d
echo ""
echo "Paradise server starting... check status with: docker compose ps"
echo "Connect any client to localhost:2692"
