#!/bin/bash
# Resolve through any symlink so this works from ~/bin.
SELF="$0"
while [ -L "$SELF" ]; do
  L="$(readlink "$SELF")"
  case "$L" in /*) SELF="$L" ;; *) SELF="$(dirname "$SELF")/$L" ;; esac
done
cd "$(dirname "$SELF")" && source prod-env.sh
state=$(aws_ ec2 describe-instances --instance-ids "$NETREK_IID" \
  --query "Reservations[0].Instances[0].State.Name" --output text)
echo "instance $NETREK_IID: $state"
echo "elastic IP: $NETREK_EIP  (host: $NETREK_HOST)"
if [ "$state" = running ]; then
  ssh -i ~/.ssh/netrek-prod.pem -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
      -o ConnectTimeout=8 ec2-user@$NETREK_EIP \
      'sudo docker ps --format "  container: {{.Names}} {{.Status}}"' 2>/dev/null || echo "  (ssh not ready)"
fi
