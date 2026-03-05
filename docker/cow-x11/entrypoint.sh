#!/bin/bash
set -e

NETREK_DIR="/usr/local/src/netrek"
CLIENT_BIN="${NETREK_DIR}/netrek-client-cow/netrek-client-cow"
SERVER_HOST="${NETREK_SERVER:-server}"

if [ ! -x "$CLIENT_BIN" ]; then
    echo "ERROR: netrek-client-cow not found at ${CLIENT_BIN}" >&2
    exit 1
fi

cd "${NETREK_DIR}/netrek-client-cow"
echo "Connecting cow client to ${SERVER_HOST}..."
exec "$CLIENT_BIN" -h "$SERVER_HOST"
