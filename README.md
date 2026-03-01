# Netrek Containerized

Dockerized Netrek server and client infrastructure using Docker Compose. Builds and runs [Netrek](https://www.netrek.org/) vanilla and Paradise servers, the COW client, and a pygame development client.

## Quick Start

```bash
# Start the vanilla server (headless)
docker compose up server

# Start server + COW client (requires XQuartz on macOS)
docker compose up

# Start the pygame development client against the running server
docker compose up pygame-client
```

## Prerequisites

- Docker and Docker Compose
- XQuartz (macOS, for GUI clients): `brew install xquartz`
- Before running clients, allow X11 connections: `xhost +localhost`

## Architecture

```
docker-compose.yml          # Service definitions
Dockerfile                  # Base image (vanilla server + COW client)
server/entrypoint.sh        # Server startup script
client-dev/entrypoint.sh    # COW client startup script
client-dev/config/          # Client configuration (.xtrekrc)
paradise-server/Dockerfile  # Paradise server image
paradise-server/entrypoint.sh
pygame-client/Dockerfile    # Pygame client image
pygame-client/entrypoint.sh
submodules/                 # Upstream Netrek source (git submodules)
dev/                        # Development configs and legacy files
```

### Services

| Service | Description | Profile |
|---------|-------------|---------|
| `server` | Vanilla Netrek server (headless) | default |
| `client` | COW client (X11 GUI) | default |
| `pygame-client` | Pygame development client (X11 GUI) | default |
| `paradise-server` | NetrekII Paradise server | `paradise` |

## Usage

### Vanilla server only (headless hosting)

```bash
docker compose up server -d
```

The server listens on TCP port 2692 (mapped to container 2592) and UDP ports 2693-2729. No X11 or GUI required.

### Server + COW client

```bash
open -a XQuartz
xhost +localhost
docker compose up
```

### Paradise server

```bash
docker compose --profile paradise up paradise-server
```

### Pygame client

```bash
docker compose up pygame-client
```

The pygame client connects to the `server` service via Docker networking.

### Connect to a running container

```bash
docker exec -it vanilla-netrek-server /bin/bash
```

## Ports

| Port (host) | Port (container) | Protocol | Purpose |
|-------------|-----------------|----------|---------|
| 2692 | 2592 | TCP | Netrek game server |
| 2693-2729 | 2593-2629 | UDP | Per-player UDP channels |

## Sound (macOS)

To enable sound from the COW client:

```bash
pulseaudio --load="module-native-protocol-tcp auth-anonymous=1" --exit-idle-time=-1 --daemon
```

The container routes audio via PulseAudio to the macOS host on port 4713.

## But, why?

In addition to the enjoyment of learning more about Docker and XQuartz, and the satisfaction of overcoming the challenges of getting legacy code to work, I was eager to look under the hood, and make it easier to share with others.
