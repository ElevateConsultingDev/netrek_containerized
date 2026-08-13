#!/bin/bash
# Start the COW SDL2 client connecting to the PROD STURGEON server on EC2.
# Uses the Elastic IP directly (works before DNS is set); once
# sturgeon.elevateconsulting.dev resolves you can pass -h that instead.
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$REPO_DIR/scripts/prod-env.sh"
HOST="${1:-$NETREK_EIP}"   # default to the Elastic IP; override: start-prod-client.sh sturgeon.elevateconsulting.dev
cd "$REPO_DIR/../clients/netrek-client-cow-sdl2"
exec ./build/netrek-sdl2 -h "$HOST" -p 2592
