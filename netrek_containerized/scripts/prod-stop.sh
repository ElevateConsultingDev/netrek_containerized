#!/bin/bash
# Stop the instance (stops billing for compute; EIP + disk remain).
# Resolve through any symlink so this works from ~/bin.
SELF="$0"
while [ -L "$SELF" ]; do
  L="$(readlink "$SELF")"
  case "$L" in /*) SELF="$L" ;; *) SELF="$(dirname "$SELF")/$L" ;; esac
done
cd "$(dirname "$SELF")" && source prod-env.sh
echo "stopping $NETREK_IID ..."
aws_ ec2 stop-instances --instance-ids "$NETREK_IID" >/dev/null
aws_ ec2 wait instance-stopped --instance-ids "$NETREK_IID"
echo "stopped."
