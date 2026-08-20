#!/bin/bash
# One sample of everything botmon needs, from inside the container:
# peek's player table, then each live bot's recent decisions.
/tmp/peek
echo "=== DECISIONS"
for f in /tmp/robot*.out.log; do
    [ -e "$f" ] || continue
    p=$(echo "$f" | tr -dc 0-9)
    [ -r /proc/$p/cmdline ] || continue          # bot died, orphan log
    n=$(tr "\0" " " < /proc/$p/cmdline | sed -n "s/.*-n \([A-Za-z]*\).*/\1/p")
    [ -n "$n" ] || continue
    echo "@@ $n"
    grep DECIDE "$f" | tail -150
done
