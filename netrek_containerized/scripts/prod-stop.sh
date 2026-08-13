#!/bin/bash
# Stop the instance (stops billing for compute; EIP + disk remain).
cd "$(dirname "$0")" && source prod-env.sh
echo "stopping $NETREK_IID ..."
aws_ ec2 stop-instances --instance-ids "$NETREK_IID" >/dev/null
aws_ ec2 wait instance-stopped --instance-ids "$NETREK_IID"
echo "stopped."
