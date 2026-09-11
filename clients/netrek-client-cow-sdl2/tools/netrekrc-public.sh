#!/bin/sh
# Turn a personal .netrekrc into one that is safe to publish: the three
# credential lines become placeholders, everything else (keymap, buttonmap,
# macros, distress calls) is kept as is.
#
#   tools/netrekrc-public.sh ~/.netrekrc > docs/netrekrc.txt
#
# Refuses to emit anything if a credential survives, so a published file can
# never carry a real password.
set -e
SRC="${1:?usage: netrekrc-public.sh <path to .netrekrc>}"
OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT

cat > "$OUT" <<'HEADER'
# .netrekrc for STURGEON — netrek.elevateconsulting.dev
#
# Save this file as ~/.netrekrc. Fill in the three lines below: name is the
# pilot name you want, password is yours to choose the first time you log in
# with it, login is any short handle. Press & in game to reread the file
# without restarting.
#
# Printable command reference, matching the keys set here:
#   https://netrek.elevateconsulting.dev/netrek-com-cheatsheet.pdf
HEADER

sed -E 's/^([[:space:]]*name:).*/\1 yourname/I; 
        s/^([[:space:]]*password:).*/\1 yourpassword/I;
        s/^([[:space:]]*login:).*/\1 yourlogin/I' "$SRC" >> "$OUT"

# A personal rc often names the server on the command line instead; a player
# downloading this one needs it in the file.
grep -qiE "^[[:space:]]*server:" "$OUT" || printf '\nserver: sturgeon.elevateconsulting.dev\n' >> "$OUT"

# Nothing leaves this script carrying a real credential.
for opt in name password login; do
  bad=$(grep -iE "^[[:space:]]*$opt:" "$OUT" | grep -viE "your(name|password|login)" || true)
  [ -z "$bad" ] || { echo "FAIL: $opt line was not sanitized: $bad" >&2; exit 1; }
done

cat "$OUT"
