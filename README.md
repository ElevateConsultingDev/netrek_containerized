# Netrek Containerized

Dockerized Netrek server and client infrastructure using Docker Compose. Builds and runs [Netrek](https://www.netrek.org/) vanilla and Paradise servers, the COW client, and a pygame development client.

Designed for macOS hosts using XQuartz for X11 forwarding.

## Quick Start

```bash
# Start the vanilla server + COW client
./scripts/start-everything.sh

# Or start just the server (headless)
./scripts/start-server.sh

# Stop everything
./scripts/stop.sh
```

## Prerequisites

- Docker and Docker Compose
- XQuartz (for GUI clients): `brew install xquartz`

## Scripts

| Script | What it does |
|--------|-------------|
| `scripts/start-server.sh` | Start vanilla server headless |
| `scripts/start-cow-client.sh` | Start COW client (opens XQuartz, needs running server) |
| `scripts/start-pygame-client.sh` | Start pygame client (needs running server) |
| `scripts/start-everything.sh` | Start server + COW client together |
| `scripts/start-paradise.sh` | Start Paradise server (alt game mode, same port) |
| `scripts/connect.sh` | Shell into the running server container |
| `scripts/stop.sh` | Stop all containers |

## Docker Compose (direct usage)

```bash
docker compose up server -d              # Headless vanilla server
docker compose up                         # Server + COW client
docker compose --profile paradise up paradise-server -d  # Paradise server
docker compose down                       # Stop
```

## Architecture

```
docker-compose.yml              # Service definitions
docker/Dockerfile               # Base image (vanilla server + COW client)
docker/server/entrypoint.sh     # Server startup script
docker/cow-x11/entrypoint.sh    # COW client startup script
docker/cow-x11/config/.xtrekrc  # COW client configuration
docker/paradise-server/         # Paradise server image + entrypoint
docker/dev/                     # Development configs (bashrc, vimrc, etc.)
clients/cow-sdl2/               # Native macOS SDL2 client (C)
clients/pygame/                 # Pygame client source code
scripts/                        # All startup/stop scripts
submodules/                     # Upstream Netrek source (git submodules)
.github/workflows/docker.yml   # CI: build all images on push
```

### Services

| Service | Description | Profile |
|---------|-------------|---------|
| `server` | Vanilla Netrek server (headless) | default |
| `client` | COW client (X11 GUI) | default |
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
