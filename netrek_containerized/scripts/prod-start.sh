#!/bin/bash
# Start the ephemeral server and make sure the Elastic IP is attached.
cd "$(dirname "$0")" && source prod-env.sh
echo "starting $NETREK_IID ..."
aws_ ec2 start-instances --instance-ids "$NETREK_IID" >/dev/null
aws_ ec2 wait instance-running --instance-ids "$NETREK_IID"
# Re-associate EIP (harmless if already attached; needed if it drifted)
aws_ ec2 associate-address --instance-id "$NETREK_IID" --allocation-id "$NETREK_ALLOC" >/dev/null 2>&1
echo "running. server at $NETREK_HOST ($NETREK_EIP):2592"
echo "note: container has restart=unless-stopped, so netrekd comes back on boot."
