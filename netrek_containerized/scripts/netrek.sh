#!/bin/bash
# netrek - one entry point for the local server, the client, prod, and admin.
#
# LOCAL
#   netrek start                 start the local server
#   netrek stop                  stop it
#   netrek play                  join the local game
#   netrek bots                  live bot monitor
#   netrek botlog [-d|bot]       follow bot decisions
#
# PROD (the EC2 server)
#   netrek start_prod            start the instance
#   netrek stop_prod             stop it (it bills while running)
#   netrek status_prod           instance and container state
#   netrek play_prod             join prod, enrolling your IP first
#   netrek deploy_prod           ship this working tree to prod and rebuild
#
# ADMIN (live game, local by default)
#   netrek admin <command>       everything the admin tool does, e.g.
#   netrek admin planet Earth +10
#   netrek admin player F0 kills 5
#   netrek admin upgrades
#   netrek admin help            the full admin reference
#
#   netrek check                 verify the toolchain after a move or re-clone
#
# Run with no arguments for this list.
set -e

# Resolve through any symlink: this is normally run as ~/bin/netrek.
SELF="$0"
while [ -L "$SELF" ]; do
  L="$(readlink "$SELF")"
  case "$L" in /*) SELF="$L" ;; *) SELF="$(dirname "$SELF")/$L" ;; esac
done
HERE="$(cd "$(dirname "$SELF")" && pwd)"

usage() {
  sed -n '2,/^set -e/p' "$SELF" | sed '/^set -e/d' | sed 's/^# \{0,1\}//'
}

cmd="${1:-}"; shift || true
case "$cmd" in
  start)        exec "$HERE/start-server.sh"      "$@" ;;
  stop)         exec "$HERE/stop.sh"              "$@" ;;
  play)         exec "$HERE/start-com-client.sh"  "$@" ;;

  start_prod)   exec "$HERE/prod-start.sh"        "$@" ;;
  stop_prod)    exec "$HERE/prod-stop.sh"         "$@" ;;
  status_prod)  exec "$HERE/prod-status.sh"       "$@" ;;
  play_prod)    exec "$HERE/start-prod-client.sh" "$@" ;;
  deploy_prod)  exec "$HERE/prod-deploy.sh"       "$@" ;;

  # the admin tool, and its two monitors promoted to the top level
  admin)        NETREK_CMD="netrek admin" exec "$HERE/god.sh" "$@" ;;
  check)        exec "$HERE/selfcheck.sh"          "$@" ;;
  bots)         exec "$HERE/god.sh" bots          "$@" ;;
  botlog)       exec "$HERE/god.sh" botlog        "$@" ;;

  ""|-h|--help|help) usage ;;
  *)
    echo "unknown command '$cmd'" >&2
    echo >&2
    usage >&2
    exit 1 ;;
esac
