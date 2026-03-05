#!/bin/bash
# Extract sprite and sound assets from the netrek-server Docker container.
# Run once after building the container.

set -e

CONTAINER="netrek-server"
SRC="/usr/local/src/netrek/netrek-client-cow"
DEST="$(cd "$(dirname "$0")" && pwd)/assets"

echo "Copying pixmaps..."
for team in Fed Rom Kli Ori Ind; do
    mkdir -p "$DEST/pixmaps/$team"
    docker cp "$CONTAINER:$SRC/pixmaps/$team/." "$DEST/pixmaps/$team/"
done

mkdir -p "$DEST/pixmaps/Misc"
docker cp "$CONTAINER:$SRC/pixmaps/Misc/." "$DEST/pixmaps/Misc/"

mkdir -p "$DEST/pixmaps/Planets/Map"
docker cp "$CONTAINER:$SRC/pixmaps/Planets/Map/." "$DEST/pixmaps/Planets/Map/"

echo "Copying sounds..."
mkdir -p "$DEST/sounds"
docker cp "$CONTAINER:$SRC/sounds/." "$DEST/sounds/"

echo "Copying lurk.py reference..."
docker cp "$CONTAINER:$SRC/lurk.py" "$DEST/lurk.py"

echo "Done. Assets in $DEST"
