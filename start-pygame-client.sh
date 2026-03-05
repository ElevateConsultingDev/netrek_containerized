#!/bin/bash
# Start the pygame client locally against the containerized server
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLIENT_DIR="$SCRIPT_DIR/netrek_client_pygame"
VENV_DIR="$CLIENT_DIR/.venv"
SERVER_PORT=2692

# Ensure the server is running
if ! docker compose -f "$SCRIPT_DIR/docker-compose.yml" ps --status running server 2>/dev/null | grep -q server; then
    echo "Starting netrek server..."
    docker compose -f "$SCRIPT_DIR/docker-compose.yml" up server -d
    echo "Waiting for server to be healthy..."
    docker compose -f "$SCRIPT_DIR/docker-compose.yml" wait --condition service_healthy server 2>/dev/null || sleep 5
fi

# Activate venv
if [ ! -d "$VENV_DIR" ]; then
    echo "Creating venv..."
    python3 -m venv "$VENV_DIR"
    "$VENV_DIR/bin/pip" install -r "$CLIENT_DIR/requirements.txt"
fi
source "$VENV_DIR/bin/activate"

# Resolve any --rc path to absolute before cd
ARGS=()
while [[ $# -gt 0 ]]; do
    if [[ "$1" == "--rc" && -n "$2" ]]; then
        ARGS+=("--rc" "$(cd "$(dirname "$2")" && pwd)/$(basename "$2")")
        shift 2
    else
        ARGS+=("$1")
        shift
    fi
done

# Run the client
cd "$CLIENT_DIR"
exec python3 -m netrek --server localhost --port "$SERVER_PORT" --no-udp "${ARGS[@]}"
