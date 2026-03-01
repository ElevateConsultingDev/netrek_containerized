# Netrek Containerized

Dockerized Netrek server and client infrastructure using Docker Compose. Builds and runs [Netrek](https://www.netrek.org/) vanilla and Paradise servers, the COW client, and a pygame development client.

Designed for macOS hosts using XQuartz for X11 forwarding.

## Quick Start

```bash
# Start the vanilla server + COW client
./start-everything.sh

# Or start just the server (headless)
./start-server.sh

# Stop everything
./stop.sh
```

## Prerequisites

- Docker and Docker Compose
- XQuartz (for GUI clients): `brew install xquartz`

## Scripts

| Script | What it does |
|--------|-------------|
| `./start-server.sh` | Start vanilla server headless |
| `./start-cow-client.sh` | Start COW client (opens XQuartz, needs running server) |
| `./start-pygame-client.sh` | Start pygame client (opens XQuartz, needs running server) |
| `./start-everything.sh` | Start server + COW client together |
| `./start-paradise.sh` | Start Paradise server (alt game mode, same port) |
| `./connect.sh` | Shell into the running server container |
| `./stop.sh` | Stop all containers |

## Docker Compose (direct usage)

```bash
docker compose up server -d              # Headless vanilla server
docker compose up                         # Server + COW client
docker compose up pygame-client           # Pygame client (needs running server)
docker compose --profile paradise up paradise-server -d  # Paradise server
docker compose down                       # Stop
```

## Architecture

```
docker-compose.yml              # Service definitions
Dockerfile                      # Base image (vanilla server + COW client)
server/entrypoint.sh            # Server startup script
client-dev/entrypoint.sh        # COW client startup script
client-dev/config/.xtrekrc      # COW client configuration
paradise-server/Dockerfile      # Paradise server image
paradise-server/entrypoint.sh   # Paradise startup script
pygame-client/Dockerfile        # Pygame client image
pygame-client/entrypoint.sh     # Pygame startup script
netrek_client_pygame/            # Pygame client source code
submodules/                     # Upstream Netrek source (git submodules)
dev/                            # Development configs and legacy files
.github/workflows/docker.yml   # CI: build all images on push
```

### Services

| Service | Description | Profile |
|---------|-------------|---------|
| `server` | Vanilla Netrek server (headless) | default |
| `client` | COW client (X11 GUI) | default |
| `pygame-client` | Pygame development client (X11 GUI) | default |
| `paradise-server` | NetrekII Paradise server | `paradise` |

## Ports

| Port (host) | Port (container) | Protocol | Purpose |
|-------------|-----------------|----------|---------|
| 2692 | 2592 | TCP | Netrek game server |
| 2693-2729 | 2593-2629 | UDP | Per-player UDP channels |

Any netrek client on your Mac or network can connect to `localhost:2692`.

## Sound (macOS)

To enable sound from the COW client, start PulseAudio before launching:

```bash
pulseaudio --load="module-native-protocol-tcp auth-anonymous=1" --exit-idle-time=-1 --daemon
```

The container routes audio via PulseAudio to the macOS host on port 4713.

## But, why?

In addition to the enjoyment of learning more about Docker and XQuartz, and the satisfaction of overcoming the challenges of getting legacy code to work, I was eager to look under the hood, and make it easier to share with others.
