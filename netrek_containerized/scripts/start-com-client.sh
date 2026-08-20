#!/bin/bash
# Launch the Netrek COM client (our SDL2/Mac build) against the local vanilla
# server on port 2692. For the upstream X11 client, see start-vanilla-client.sh.
# Resolve through any symlink so this works when run from ~/bin.
SELF="$0"
while [ -L "$SELF" ]; do
  L="$(readlink "$SELF")"
  case "$L" in /*) SELF="$L" ;; *) SELF="$(dirname "$SELF")/$L" ;; esac
done
REPO_DIR="$(cd "$(dirname "$SELF")/.." && pwd)"
cd "$REPO_DIR/../clients/netrek-client-cow-sdl2"
exec ./build/netrek-sdl2 -h localhost -p 2692 "$@"
