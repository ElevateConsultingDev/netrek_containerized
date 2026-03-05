# Netrek Containerized

Dockerized Netrek server infrastructure using Docker Compose. Runs vanilla and Paradise servers simultaneously on separate ports, with native macOS clients.

## Quick Start

```bash
# Start both servers
docker compose up -d

# Start just the vanilla server
./scripts/start-server.sh

# Connect a client
./scripts/start-vanilla-client.sh    # Vanilla (port 2692)
./scripts/start-paradise-client.sh   # Paradise (port 2792)

# Stop everything
./scripts/stop.sh
```

## Prerequisites

- Docker and Docker Compose
- COW SDL2 client built (`clients/cow-sdl2/build/netrek-sdl2`)

## Ports

| Server | Host Port | Container Port | Protocol | Purpose |
|--------|-----------|---------------|----------|---------|
| Vanilla | 2692 | 2592 | TCP | Game server |
| Vanilla | 2693-2729 | 2593-2629 | UDP | Per-player UDP channels |
| Paradise | 2792 | 2592 | TCP | Game server |
| Paradise | 2791 | 2591 | TCP | Secondary |
| Paradise | 2793-2829 | 2593-2629 | UDP | Per-player UDP channels |

## Scripts

| Script | What it does |
|--------|-------------|
| `scripts/start-server.sh` | Start vanilla server |
| `scripts/start-paradise.sh` | Start Paradise server |
| `scripts/start-vanilla-client.sh` | COW SDL2 client → vanilla (port 2692) |
| `scripts/start-paradise-client.sh` | COW SDL2 client → Paradise (port 2792) |
| `scripts/start-pygame-client.sh` | Pygame client → vanilla (port 2692) |
| `scripts/connect.sh` | Shell into vanilla server container |
| `scripts/connect-paradise.sh` | Shell into Paradise server container |
| `scripts/stop.sh` | Stop all containers |

## Docker Compose (direct usage)

```bash
docker compose up -d                          # Both servers
docker compose up server -d                   # Vanilla only
docker compose up paradise-server -d          # Paradise only
docker compose down                           # Stop all
```

## Architecture

```
docker-compose.yml              # Service definitions
docker/Dockerfile               # Vanilla server image
docker/server/entrypoint.sh     # Vanilla server startup script
docker/paradise-server/         # Paradise server image + entrypoint
docker/dev/                     # Development configs (bashrc, vimrc, etc.)
clients/cow-sdl2/               # Native macOS SDL2 client (C)
clients/pygame/                 # Pygame client source code
scripts/                        # All startup/stop scripts
submodules/                     # Upstream Netrek source (git submodules)
.github/workflows/docker.yml   # CI: build all images on push
```

### Services

| Service | Description | Host Port |
|---------|-------------|-----------|
| `server` | Vanilla Netrek server | 2692 |
| `paradise-server` | NetrekII Paradise server | 2792 |

## But, why?

In addition to the enjoyment of learning more about Docker and XQuartz, and the satisfaction of overcoming the challenges of getting legacy code to work, I was eager to look under the hood, and make it easier to share with others.
