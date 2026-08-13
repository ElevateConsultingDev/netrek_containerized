#!/bin/bash
# Launch the Netrek COM client against the PROD STURGEON server on EC2.
#
# Auto-enrolls your CURRENT public IP in the server firewall first, so a
# changing/remote IP never locks you out -- then connects. Pass a host to
# override (defaults to the DNS name, falls back to the Elastic IP).
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$REPO_DIR/scripts/prod-env.sh"
HOST="${1:-${NETREK_HOST:-$NETREK_EIP}}"

# --- auto-enroll this machine's current IP (non-fatal on failure) -----------
if [ -x "$REPO_DIR/scripts/prod-allow-ip.sh" ]; then
  echo "Enrolling your current IP in the server firewall..."
  if "$REPO_DIR/scripts/prod-allow-ip.sh" me; then
    :
  else
    echo "  (enroll failed -- continuing; you may already be allowed, or check AWS creds)"
  fi
fi

cd "$REPO_DIR/../clients/netrek-client-cow-sdl2"
echo "Connecting to $HOST:2592 ..."
exec ./build/netrek-sdl2 -h "$HOST" -p 2592
