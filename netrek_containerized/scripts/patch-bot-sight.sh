#!/bin/bash
# Install the bot-sight fix into a running container and rebuild.
#
# The vanilla image builds the server from a github clone, not from this
# repo, so these changes live only in the container's writable layer and are
# lost whenever the container is recreated (docker compose down/up, rebuild).
# Re-run this after any recreate.
#
# Usage: ./patch-bot-sight.sh [container]   (default: vanilla-netrek-server)

set -e

C="${1:-vanilla-netrek-server}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$REPO/servers/netrek-server"
DST=/usr/local/src/netrek/netrek-server

echo "==> copying patched sources into $C"
docker cp "$SRC/ntserv/genspkt.c"    "$C:$DST/ntserv/genspkt.c"
docker cp "$SRC/robots/rmove.c"      "$C:$DST/robots/rmove.c"
docker cp "$REPO/netrek_containerized/scripts/peek.c" "$C:/tmp/peek.c"

echo "==> building"
docker exec "$C" bash -lc "
set -e
cd $DST

# keep a pristine control build the first time only
[ -f here/lib/ntserv.orig ]   || cp here/lib/ntserv here/lib/ntserv.orig
[ -f here/lib/robotII.orig ]  || cp here/lib/robotII here/lib/robotII.orig

make -C ntserv ntserv  >/dev/null
make -C robots robotII >/dev/null
gcc -I include -I . -o /tmp/peek /tmp/peek.c ntserv/libnetrek.a -lgdbm -lresolv -lm
"

echo "==> installing (bots and their ntserv processes must die first)"
docker exec "$C" bash -lc "
cd $DST
pkill -f 'newbieb[o]t' || true
pkill -x ntserv || true
for i in \$(seq 1 300); do cp ntserv/ntserv here/lib/ntserv 2>/dev/null && break; done
cp robots/robotII here/lib/robotII
"

echo "==> installed:"
docker exec "$C" bash -lc "ls -la $DST/here/lib/ntserv* $DST/here/lib/robotII*"
echo
echo "watch live state with:"
echo "  docker exec $C bash -c 'while :; do clear; /tmp/peek; sleep 1; done'"
