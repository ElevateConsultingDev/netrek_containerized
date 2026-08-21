#!/bin/bash
# Open the server to an IP. Usage:
#   ./prod-allow-ip.sh me [name]     -> detects your current public IP
#   ./prod-allow-ip.sh <ip> <name>   -> a specific IP (e.g. Ron's)
# Replaces any prior rule carrying the same <name> description, so a changing
# IP never leaves stale holes. "me" also opens SSH (22); others get game only.
# Resolve through any symlink so this works from ~/bin.
SELF="$0"
while [ -L "$SELF" ]; do
  L="$(readlink "$SELF")"
  case "$L" in /*) SELF="$L" ;; *) SELF="$(dirname "$SELF")/$L" ;; esac
done
cd "$(dirname "$SELF")" && source prod-env.sh
arg="$1"; name="${2:-me}"
if [ "$arg" = me ]; then ip=$(curl -s https://checkip.amazonaws.com); name="${2:-dave}"; ssh_too=1; else ip="$arg"; ssh_too=0; fi
[ -z "$ip" ] && { echo "no IP"; exit 1; }
cidr="$ip/32"
echo "allowing $name = $cidr"

# revoke any existing rules tagged with this name (prune stale IPs)
for pproto_ports in "tcp:22" "tcp:2592" "udp:2593:2629"; do :; done
python3 - "$NETREK_SG" "$name" "$AWS_PROFILE_NETREK" "$AWS_REGION_NETREK" <<'PY'
import json,subprocess,sys
sg,name,prof,region=sys.argv[1:5]
d=json.loads(subprocess.check_output(["aws","--profile",prof,"--region",region,"ec2","describe-security-groups","--group-ids",sg,"--query","SecurityGroups[0].IpPermissions","--output","json"]))
for p in d:
    keep=[r for r in p.get("IpRanges",[]) if r.get("Description")==name]
    if not keep: continue
    perm={"IpProtocol":p["IpProtocol"],"IpRanges":keep}
    if "FromPort" in p: perm["FromPort"]=p["FromPort"]; perm["ToPort"]=p["ToPort"]
    subprocess.run(["aws","--profile",prof,"--region",region,"ec2","revoke-security-group-ingress","--group-id",sg,"--ip-permissions",json.dumps([perm])],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
PY

rules=("IpProtocol=tcp,FromPort=2592,ToPort=2592,IpRanges=[{CidrIp=$cidr,Description=$name}]"
       "IpProtocol=udp,FromPort=2593,ToPort=2629,IpRanges=[{CidrIp=$cidr,Description=$name}]")
[ "$ssh_too" = 1 ] && rules+=("IpProtocol=tcp,FromPort=22,ToPort=22,IpRanges=[{CidrIp=$cidr,Description=$name}]")
aws_ ec2 authorize-security-group-ingress --group-id "$NETREK_SG" --ip-permissions "${rules[@]}" >/dev/null \
  && echo "done. $name can reach $NETREK_HOST:2592" || echo "authorize failed (maybe already set)"
