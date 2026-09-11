#!/bin/bash
# Restart the game server container, leaving the instance alone. Anyone in
# game is dropped and can reconnect straight away: that container is the
# server. Use prod-stop.sh/prod-start.sh for the machine itself.
# Resolve through any symlink so this works from ~/bin.
SELF="$0"
while [ -L "$SELF" ]; do
  L="$(readlink "$SELF")"
  case "$L" in /*) SELF="$L" ;; *) SELF="$(dirname "$SELF")/$L" ;; esac
done
cd "$(dirname "$SELF")" && source prod-env.sh

state=$(aws_ ec2 describe-instances --instance-ids "$NETREK_IID" \
  --query "Reservations[0].Instances[0].State.Name" --output text)
if [ "$state" != running ]; then
  echo "instance is $state, so there is no server to restart."
  echo "start the machine first: netrek start_prod"
  exit 1
fi

echo "restarting netrekd on $NETREK_HOST (instance stays up) ..."
ssh -i ~/.ssh/netrek-prod.pem -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o ConnectTimeout=15 ec2-user@$NETREK_EIP \
    'cd ~/netrek/netrek_containerized && sudo docker compose -f docker-compose.prod.yml restart server'
./prod-status.sh
