#!/bin/bash
# Start the COW SDL2 client connecting to the vanilla server (port 2692)
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_DIR/clients/cow-sdl2"
exec ./build/netrek-sdl2 -h localhost -p 2692 "$@"
