#!/bin/bash
# Ship the current working tree to the EC2 server, rebuild the image there,
# and restart the container.
#
#   ./prod-deploy.sh            deploy the working tree as it stands
#   ./prod-deploy.sh --dry-run  show what would be sent, change nothing
#
# The instance has no git checkout: it gets a tarball of the paths the image
# build actually needs. Those are the repo root paths the Dockerfile COPYs
# from, so the layout on the server mirrors the monorepo rather than the old
# flat netrek_containerized/ it was first deployed with.
#
# Building on the instance rather than pushing an image keeps this dependent
# on nothing but ssh, at the cost of a few minutes of t3 CPU.
set -e

# Resolve through any symlink so this works from ~/bin.
SELF="$0"
while [ -L "$SELF" ]; do
  L="$(readlink "$SELF")"
  case "$L" in /*) SELF="$L" ;; *) SELF="$(dirname "$SELF")/$L" ;; esac
done
cd "$(dirname "$SELF")" && source prod-env.sh
REPO="$(cd ../.. && pwd)"
SSH_OPTS="-i $HOME/.ssh/netrek-prod.pem -o StrictHostKeyChecking=no
          -o UserKnownHostsFile=/dev/null -o ConnectTimeout=15"
REMOTE=ec2-user@$NETREK_EIP
DEST=netrek                       # ~/netrek on the instance

# Only what the image build reads: the compose files and docker/ and scripts/
# from netrek_containerized, plus the two source trees the Dockerfile COPYs.
PATHS=(
  netrek_containerized/docker
  netrek_containerized/scripts
  netrek_containerized/docker-compose.yml
  netrek_containerized/docker-compose.prod.yml
  servers/netrek-server
  clients/netrek-client-cow
)

if [ "${1:-}" = "--dry-run" ]; then
  echo "would send to $REMOTE:~/$DEST"
  ( cd "$REPO" && du -sh "${PATHS[@]}" )
  exit 0
fi

echo "==> packing $(cd "$REPO" && du -shc "${PATHS[@]}" | tail -1 | cut -f1)"
TAR=$(mktemp -t netrek-deploy).tgz
# NB: no bare '--exclude build'. It matches any path component called
# build, which silently drops docker/build, the vendored upstream build
# script the image needs, and the failure only shows up as a COPY error.
tar czf "$TAR" -C "$REPO" \
    --exclude .git --exclude node_modules --exclude '*.o' \
    --exclude 'clients/*/build' --exclude 'clients/*/build-asan' \
    --exclude here \
    "${PATHS[@]}"

echo "==> uploading $(du -h "$TAR" | cut -f1)"
scp $SSH_OPTS -q "$TAR" "$REMOTE:/tmp/netrek-deploy.tgz"
rm -f "$TAR"

echo "==> unpacking and rebuilding on the instance (a few minutes)"
ssh $SSH_OPTS "$REMOTE" "
  set -e
  mkdir -p ~/$DEST
  tar xzf /tmp/netrek-deploy.tgz -C ~/$DEST
  rm -f /tmp/netrek-deploy.tgz
  cd ~/$DEST/netrek_containerized
  sudo docker compose -f docker-compose.prod.yml build server
  sudo docker compose -f docker-compose.prod.yml up -d server
"

echo "==> deployed. checking"
sleep 8
./prod-status.sh
