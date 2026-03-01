#!/bin/bash
# Start the vanilla Netrek server (headless, no GUI needed)
docker compose up server -d
echo ""
echo "Server starting... check status with: docker compose ps"
echo "Connect any client to localhost:2692"
