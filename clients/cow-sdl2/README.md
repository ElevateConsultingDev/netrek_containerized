# COW Netrek Client — SDL2 Backend

Native macOS build of the [COW Netrek client](https://github.com/netrek/netrek-client-cow) using SDL2 instead of X11. Compiles the entire 45K-line C client unchanged by replacing only the display backend (3 files, ~2700 lines).

## Quick Start

```bash
# Install dependencies
brew install sdl2 sdl2_ttf sdl2_image sdl2_mixer

# Build
cd cow_sdl2
make

# Run (connect to a server)
./netrek-sdl2 -h localhost -p 2692
```

## Prerequisites

- macOS (arm64 or x86_64)
- Homebrew
- SDL2 libraries: `brew install sdl2 sdl2_ttf sdl2_image sdl2_mixer`
- COW source in `../submodules/netrek-client-cow` (git submodule)

## How It Works

COW has a clean display abstraction layer called Wlib (~60 functions declared in `Wlib.h`). All game code — protocol, rendering logic, input handling, UI — calls only Wlib functions. The X11 backend (`x11window.c`, 4000+ lines) implements Wlib via Xlib. This project implements the same Wlib interface via SDL2.

### Architecture

```
COW game code (45K lines, unchanged)
         │
         ▼
    Wlib.h (~60 functions)
         │
         ▼
┌─────────────────────┐
│   sdl2window.c      │  Window management, events, text, drawing, compositor
│   sdl2sprite.c      │  PNG sprite loading via SDL2_image
│   sdl2camera.c      │  Screenshot support
│   sdl2window.h      │  Internal structures
│   sdl2_config.h     │  Build configuration (replaces autoconf config.h)
└─────────────────────┘
         │
         ▼
  SDL2 / SDL2_ttf / SDL2_image / SDL2_mixer
```

### Key Design Decisions

**Render-to-texture compositor.** COW creates ~20 "windows" (tactical, galactic, dashboard, etc.). X11 supports window hierarchies natively; SDL2 does not. Each Wlib window becomes an `SDL_Texture` with `SDL_TEXTUREACCESS_TARGET`. Drawing commands target individual textures. `W_Flush()` composites all mapped textures onto a single `SDL_Window` and calls `SDL_RenderPresent()`.

**TTF fonts at 6x10 cell size.** COW layout depends on `W_Textwidth=6` and `W_Textheight=10`. The backend loads Menlo at size 8 (yielding 5x10 glyph dimensions) and forces the cell constants to 6x10. Text rendering uses SDL2_ttf.

**XBM bitmap color modulation.** X11 bitmaps are 1-bit monochrome, colorized via `XCopyPlane`. The SDL2 backend stores them as RGBA textures (white pixels with alpha from bitmap data) and colorizes at draw time with `SDL_SetTextureColorMod()`.

**Pipe-based select() integration.** COW's event loop uses `select()` on both the network socket and a display file descriptor. SDL2 has no FD. The backend creates a `pipe()` at init and returns the read end from `W_Socket()`. When SDL events arrive, it writes a byte to wake `select()`.

**Sound via SDL2_mixer.** COW's `sound.c` already has an SDL_mixer code path. The SDL_mixer API is compatible between SDL 1.x and SDL 2.x, so sound works without modification.

## Files

| File | Lines | Purpose |
|------|-------|---------|
| `sdl2window.c` | ~2100 | Full Wlib implementation: windows, events, text, drawing, bitmaps, menus, scrolling |
| `sdl2sprite.c` | ~530 | PNG sprite loading (ships, planets, torpedoes, explosions) and rendering |
| `sdl2window.h` | ~130 | Internal structures: window, icon, S_Object, color table |
| `sdl2camera.c` | ~45 | Screenshot support |
| `sdl2_config.h` | ~180 | Hand-crafted config.h for macOS (replaces autoconf) |
| `Makefile` | ~100 | Build system |

### Files Replaced from COW

| COW original | SDL2 replacement | Why |
|-------------|-----------------|-----|
| `x11window.c` (4063 lines) | `sdl2window.c` | X11 Wlib → SDL2 Wlib |
| `x11sprite.c` (865 lines) | `sdl2sprite.c` | Imlib2 → SDL2_image |
| `camera.c` (65 lines) | `sdl2camera.c` | Imlib2 → SDL2 screenshot |

All other COW source files (~40 .c files) compile unchanged.

## Asset Setup

Pixmaps and sounds are loaded from local directories. Create symlinks to the existing assets:

```bash
# Ship/planet/weapon sprites (from COW submodule)
ln -sf ../submodules/netrek-client-cow/pixmaps pixmaps

# Sound effects (from pygame client assets)
ln -sf ../netrek_client_pygame/assets/sounds sounds
```

## Usage

```bash
# Connect to a local server
./netrek-sdl2 -h localhost -p 2692

# Connect to a remote server
./netrek-sdl2 -h server.example.com

# Use metaserver to find games
./netrek-sdl2 -m

# Specify a player name
./netrek-sdl2 -h localhost -p 2692 -n YourName
```

## Debug Features

- **SIGUSR1 screenshot:** Send `kill -USR1 <pid>` to capture a BMP screenshot of the current frame. The PID is printed at startup.
- **In-game screenshot:** The `%` key (camera snap) saves a timestamped BMP.

## What Works

- Login/MOTD screen with text rendering
- Dashboard with gauge bars (speed, fuel, shields, damage, etc.)
- Galactic map with planet names and ownership indicators
- Tactical view with ship sprites, torpedoes, phasers, explosions
- Ship rotation (sprite sheet frame selection)
- Cloaking (alpha fade)
- XBM bitmap rendering with color modulation (fallback when sprites missing)
- Menu windows with separator bars and click handling
- Scrolling windows (message review, MOTD)
- Keyboard input with shift, ctrl, and caps lock handling
- Mouse input with button and wheel support
- Clipboard paste into message input
- Sound effects via SDL2_mixer
- Fullscreen toggle
- Window tiling (team logos)

## Known Limitations

- Custom cursor shapes not implemented (uses system cursor)
- Background tile images for tactical/galactic not yet loaded
- TTS overlay text is left-aligned rather than centered
- Window resize does not dynamically reflow layout
