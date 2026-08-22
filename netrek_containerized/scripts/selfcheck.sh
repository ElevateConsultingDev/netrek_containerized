#!/bin/bash
# Verify the toolchain still works after the repo is moved or re-cloned.
# Read-only: it starts nothing and changes nothing.
#
#   netrek check          (or run this script directly)
#
# Everything inside the repo resolves relative to the scripts directory, so
# the usual casualty of a move is the ~/bin/netrek symlink, which holds an
# absolute path. This says so plainly rather than failing later in a way that
# looks like something else.
SELF="$0"
while [ -L "$SELF" ]; do
  L="$(readlink "$SELF")"
  case "$L" in /*) SELF="$L" ;; *) SELF="$(dirname "$SELF")/$L" ;; esac
done
HERE="$(cd "$(dirname "$SELF")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

ok=0; bad=0
say() { printf "  %-6s %s\n" "$1" "$2"; [ "$1" = "FAIL" ] && bad=$((bad+1)) || ok=$((ok+1)); }

echo "repo:    $REPO"
echo "scripts: $HERE"
echo

echo "paths the scripts depend on"
for p in netrek_containerized/docker-compose.yml \
         netrek_containerized/docker/Dockerfile \
         netrek_containerized/docker/build \
         servers/netrek-server/ntserv \
         clients/netrek-client-cow/data.c \
         clients/netrek-client-cow-sdl2/Makefile; do
  [ -e "$REPO/$p" ] && say ok "$p" || say FAIL "$p MISSING"
done

echo
echo "scripts present and executable"
for s in netrek.sh god.sh start-server.sh stop.sh start-com-client.sh \
         prod-start.sh prod-stop.sh prod-status.sh prod-deploy.sh \
         start-prod-client.sh bot-log.sh botstate.sh; do
  [ -x "$HERE/$s" ] && say ok "$s" || say FAIL "$s missing or not executable"
done

echo
echo "client build"
if [ -x "$REPO/clients/netrek-client-cow-sdl2/build/netrek-sdl2" ]; then
  say ok "netrek-sdl2 binary present"
else
  say FAIL "netrek-sdl2 not built (run: make -C clients/netrek-client-cow-sdl2)"
fi
for l in config.h pixmaps sounds; do
  t="$REPO/clients/netrek-client-cow-sdl2/$l"
  if [ -e "$t" ]; then say ok "$l resolves"; else say FAIL "$l broken symlink"; fi
done

echo
echo "PATH entry"
w="$(command -v netrek 2>/dev/null || true)"
if [ -z "$w" ]; then
  say FAIL "'netrek' not on PATH"
elif [ "$(cd "$(dirname "$(readlink "$w" || echo "$w")")" && pwd)" = "$HERE" ]; then
  say ok "netrek -> this checkout"
else
  say FAIL "netrek points elsewhere: $(readlink "$w")"
  echo "         fix: ln -sf $HERE/netrek.sh ~/bin/netrek"
fi

echo
echo "$ok ok, $bad failed"
[ "$bad" -eq 0 ]
