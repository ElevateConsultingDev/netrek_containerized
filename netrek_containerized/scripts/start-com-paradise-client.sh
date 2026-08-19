#!/bin/bash
# Launch the Netrek COM client (our SDL2/Mac build) against the local Paradise
# server on port 2792.
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_DIR/../clients/netrek-client-cow-sdl2"
exec ./build/netrek-sdl2 -h localhost -p 2792 "$@"
