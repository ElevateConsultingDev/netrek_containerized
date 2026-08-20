#!/bin/bash
# Read the og bots' decision traces. The logs live inside the container, at
# /tmp/robot<pid>.out.log, so they are not tailable from the host directly.
#
#   ./bot-log.sh                 list live bots and their logs
#   ./bot-log.sh Razer           follow one bot's full trace
#   ./bot-log.sh Razer -d        follow one bot, decisions only
#   ./bot-log.sh -d              follow every bot's decisions, merged
#   ./bot-log.sh -p              parked check: last decision per bot, oldest first

C="${CONTAINER:-vanilla-netrek-server}"

# name for a log file, or "gone" if that bot has died and left its log behind
names='for f in /tmp/robot*.out.log; do
         p=$(echo "$f" | tr -dc 0-9)
         n=gone
         [ -r /proc/$p/cmdline ] &&
           n=$(tr "\0" " " < /proc/$p/cmdline | sed -n "s/.*-n \([A-Za-z]*\).*/\1/p")
         echo "${n:-gone} $f"
       done'

case "$1" in
  "")
    docker exec "$C" bash -lc "$names | sort" ;;

  -d|--decide)
    docker exec "$C" bash -lc \
      'tail -n 20 -f /tmp/robot*.out.log | grep --line-buffered DECIDE' ;;

  -p|--parked)
    # last decision each live bot made, oldest first: the top of this list is
    # whoever has been doing the same thing longest
    docker exec "$C" bash -lc "
      $names | while read n f; do
        [ \"\$n\" = gone ] && continue
        last=\$(grep DECIDE \"\$f\" | tail -1)
        [ -n \"\$last\" ] && printf '%-14s %s\n' \"\$n\" \"\$last\"
      done | sort -k2" ;;

  *)
    bot="$1"
    filter="cat"
    [ "$2" = "-d" ] && filter="grep --line-buffered DECIDE"
    docker exec "$C" bash -lc "
      f=\$($names | awk -v b=$bot '\$1 == b {print \$2; exit}')
      [ -z \"\$f\" ] && { echo \"no live bot named $bot\"; exit 1; }
      echo \"== \$f\"
      tail -n 40 -f \"\$f\" | $filter" ;;
esac
