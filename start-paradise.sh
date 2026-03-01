#!/bin/bash
# Start the Paradise server (don't run alongside vanilla - same port)
docker compose --profile paradise up paradise-server -d
echo ""
echo "Paradise server starting... check status with: docker compose ps"
echo "Connect any client to localhost:2692"
