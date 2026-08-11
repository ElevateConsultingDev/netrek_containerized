#!/bin/bash
# Start the COW SDL2 client connecting to the Paradise server (port 2792)
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_DIR/../clients/netrek-client-cow-sdl2"
exec ./build/netrek-sdl2 -h localhost -p 2792 "$@"
