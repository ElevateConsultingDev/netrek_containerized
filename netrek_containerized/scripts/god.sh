#!/bin/bash
# Live game admin: planets and players, without knowing container paths.
#
# The server tools operate on the running game's shared memory, so changes
# take effect immediately. Nothing here is persisted: a game reset or a
# container recreate puts it all back.
#
#   ./god.sh planets                     list planets with armies and owner
#   ./god.sh planet Earth                show one planet
#   ./god.sh planet Earth +10            add 10 armies
#   ./god.sh planet Earth -5             remove 5 armies (floors at 0)
#   ./god.sh planet Earth 30             set armies to exactly 30
#   ./god.sh planet Earth owner Klingon  hand the planet to a team
#
#   ./god.sh who                         list occupied slots
#   ./god.sh player F0                   show a player's kills, rank, stats
#   ./god.sh player F0 kills 5           set kills (fractions allowed)
#   ./god.sh player F0 rank 8            set rank index
#   ./god.sh player F0 stat tkills 200   set a stat; DI is derived from these
#   ./god.sh upgrades F0                 print sturgeon upgrades held
#   ./god.sh di F0 25                    rough DI bump (see note below)
#
# DI is not a stored value. The client shows ratings * (ticks/36000), where
# the ratings come from kills, bombing and planets. 'di' just sets those to
# plausible numbers; it is a nudge in the right direction, not a dial. Check
# the result with 'player' and adjust the individual stats if you need a
# specific figure.
#
# Player is a slot number, or the id as shown in botmon (F0, Ra).
set -e

C="${CONTAINER:-vanilla-netrek-server}"
H=/usr/local/src/netrek/netrek-server/here
run() { docker exec "$C" bash -lc "cd $H && $*"; }

# botmon-style id (F0, Ra) -> slot number
slot_of() {
  case "$1" in
    [0-9]|[0-9][0-9]) echo "$1" ;;
    [FfRrKkOo][0-9a-zA-Z])
      local c="${1:1}"
      case "$c" in
        [0-9]) echo "$c" ;;
        *) printf '%d\n' "$(( $(printf '%d' "'${c}") - 87 ))" ;;   # a=10
      esac ;;
    *) echo "bad player '$1' (use a slot number or an id like F0)" >&2; exit 1 ;;
  esac
}

armies_of() {   # current army count for a planet
  run "./lib/tools/setplanet '$1' get" | sed -n "s/.* armies \([0-9]*\) .*/\1/p"
}

cmd="${1:-}"; shift || true
case "$cmd" in
  planets)
    run "./lib/tools/setplanet dump" ;;

  planet)
    p="${1:?planet name or number}"; shift || true
    if [ $# -eq 0 ]; then run "./lib/tools/setplanet '$p' get"; exit 0; fi
    case "$1" in
      # bare number sets, +n adds, -n removes. No '=' form: zsh expands a
      # leading '=' as a command path before the script ever sees it.
      +*|-*|[0-9]*)
        n="${1#[-+]}"; cur=$(armies_of "$p")
        case "$1" in
          +*) new=$(( cur + n )) ;;
          -*) new=$(( cur - n )); [ "$new" -lt 0 ] && new=0 ;;
          *)  new="$n" ;;
        esac
        run "./lib/tools/setplanet '$p' verbose armies $new"
        echo "$p: $cur -> $new armies" ;;
      *)
        run "./lib/tools/setplanet '$p' verbose $*" ;;
    esac ;;

  who)
    run "/tmp/peek" 2>/dev/null | grep -vE "^tourn=" || true ;;

  player)
    s=$(slot_of "${1:?player slot or id}"); shift || true
    if [ $# -eq 0 ]; then run "./lib/tools/setship $s show-player"; exit 0; fi
    run "./lib/tools/setship $s $*"
    run "./lib/tools/setship $s show-player" ;;

  upgrades)
    s=$(slot_of "${1:?player slot or id}")
    run "./lib/tools/setship $s show-upgrades" ;;

  di)
    # DI = ratings * (tticks/36000); ratings rise with tournament kills, so
    # this is an approximation, not an exact dial. Verify with 'player'.
    s=$(slot_of "${1:?player slot or id}"); want="${2:?target DI}"
    run "./lib/tools/setship $s stat ticks 360000 stat kills $(( want * 4 )) stat armsbomb $(( want * 10 )) stat planets $(( want * 3 ))"
    run "./lib/tools/setship $s show-player" ;;

  ""|-h|--help|help)
    sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//' ;;

  *) echo "unknown command '$cmd' (try: ./god.sh help)" >&2; exit 1 ;;
esac
