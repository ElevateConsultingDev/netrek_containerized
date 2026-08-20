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
docker cp "$SRC/robots/newbie.c"     "$C:$DST/robots/newbie.c"
docker cp "$SRC/robotd/util.c"       "$C:$DST/robotd/util.c"
docker cp "$SRC/robotd/dmessage.c"   "$C:$DST/robotd/dmessage.c"
docker cp "$SRC/robotd/socket.c"     "$C:$DST/robotd/socket.c"
docker cp "$SRC/robotd/redraw.c"     "$C:$DST/robotd/redraw.c"
docker cp "$SRC/robotd/robot.c"      "$C:$DST/robotd/robot.c"
docker cp "$SRC/robotd/decide.c"     "$C:$DST/robotd/decide.c"
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
make -C robots newbie  >/dev/null
make -C robotd        >/dev/null
gcc -I include -I . -o /tmp/peek /tmp/peek.c ntserv/libnetrek.a -lgdbm -lresolv -lm
"

echo "==> installing (every process holding a binary must die first)"
docker exec "$C" bash -lc "
cd $DST

# a running binary is 'Text file busy', so stop everything, then retry each
# copy until the kernel lets go. The daemon restarts the newbie manager on a
# one-minute fuse, which then respawns the bots with the new arguments.
pkill -x newbie || true          # manager first: it respawns bots instantly
pkill -f 'newbieb[o]t' || true
pkill -x ntserv || true

install_retry() {   # install_retry <src> <dst>
  # processes die asynchronously after pkill, so wait between attempts
  for i in \$(seq 1 100); do
    cp \"\$1\" \"\$2\" 2>/dev/null && return 0
    sleep 0.1
  done
  echo \"FAILED to install \$2\" >&2
  return 1
}

install_retry ntserv/ntserv   here/lib/ntserv
install_retry robots/robotII  here/lib/robotII
install_retry robots/newbie   here/lib/newbie
install_retry robotd/robot    here/lib/og/robot
rm -f /tmp/robot*.log
"

echo "==> installed:"
docker exec "$C" bash -lc "ls -la $DST/here/lib/ntserv* $DST/here/lib/robotII*"
echo
echo "bots respawn within a minute; their decision traces land in /tmp:"
echo "  docker exec $C bash -lc 'ls -t /tmp/robot*.out.log'"
echo
echo "watch live state with:"
echo "  docker exec $C bash -c 'while :; do clear; /tmp/peek; sleep 1; done'"
