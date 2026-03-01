#!/bin/bash
set -e

SERVER_HOST="${NETREK_SERVER:-server}"
SERVER_PORT="${NETREK_PORT:-2592}"

cd /app
echo "Starting pygame client -> ${SERVER_HOST}:${SERVER_PORT}..."
exec python3 -m netrek --server "$SERVER_HOST" --port "$SERVER_PORT"
