# Makefile for COW Netrek client with SDL2 backend (macOS/arm64)
#
# Compiles the COW C source from the submodule unchanged,
# replacing x11window.c/x11sprite.c/camera.c with our SDL2 versions.
# All build artifacts (.o, binary) go into build/.

CC = cc
SHELL = /bin/sh

# Build output directory
BUILD = build

# COW source directory
COW = ../netrek-client-cow

# SDL2 flags
SDL2_CFLAGS = $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image SDL2_mixer)
SDL2_LIBS   = $(shell pkg-config --libs   sdl2 SDL2_ttf SDL2_image SDL2_mixer)

# Compiler flags
# -I. picks up our sdl2_config.h (as "config.h") and sdl2window.h
# -I$(COW) picks up all COW headers (Wlib.h, defs.h, struct.h, etc.)
CFLAGS = -g -Wall -Wno-implicit-function-declaration -Wno-parentheses \
         -Wno-format -Wno-unused-variable -Wno-pointer-sign \
         -I. -I$(COW) $(SDL2_CFLAGS)
LDFLAGS = -g
LIBS = $(SDL2_LIBS) -lm

# Our config.h must override COW's. We achieve this by setting -I. first
# and creating a symlink config.h -> sdl2_config.h
CONFIG_H = config.h

# ============================================================
# COW source files (reused as-is)
# ============================================================

# These are compiled from the submodule directory
COW_SRC = \
    beeplite.c censor.c check.c colors.c cowmain.c \
    dashboard.c dashboard3.c data.c death.c defaults.c defwin.c \
    distress.c dmessage.c docwin.c enter.c feature.c findslot.c \
    getname.c getship.c helpwin.c inform.c interface.c \
    lagmeter.c local.c macrowin.c map.c myf.c netstat.c netstatopt.c \
    newwin.c option.c parsemeta.c ping.c pingstats.c planetlist.c \
    playback.c playerlist.c ranklist.c reserved.c rotate.c senddist.c \
    short.c sintab.c smessage.c socket.c spopt.c stats.c \
    string_util.c tools.c udpopt.c util.c war.c warning.c \
    input.c redraw.c main.c

# Sound is handled separately (patched at build time)
COW_SDL_SRC =

# ============================================================
# Our SDL2 replacement files
# ============================================================
SDL2_SRC = sdl2window.c sdl2sprite.c sdl2camera.c

# ============================================================
# Object files (all in build/)
# ============================================================
COW_OBJ     = $(addprefix $(BUILD)/,$(COW_SRC:.c=.o))
COW_SDL_OBJ = $(addprefix $(BUILD)/,$(COW_SDL_SRC:.c=.o))
SDL2_OBJ    = $(addprefix $(BUILD)/,$(SDL2_SRC:.c=.o))

ALL_OBJ = $(COW_OBJ) $(COW_SDL_OBJ) $(BUILD)/sound.o $(SDL2_OBJ)

TARGET = $(BUILD)/netrek-sdl2

# ============================================================
# Targets
# ============================================================

all: $(CONFIG_H) null $(TARGET)

$(TARGET): $(ALL_OBJ)
	$(CC) $(LDFLAGS) -o $@ $(ALL_OBJ) $(LIBS)
	@echo "=== Build complete: $(TARGET) ==="

# Create build directory
$(BUILD):
	mkdir -p $(BUILD)

# Create config.h symlink to our sdl2_config.h
$(CONFIG_H):
	ln -sf sdl2_config.h config.h

# Create null file (some COW sources include it)
null:
	@echo "/* intentionally empty */" > null

# Compile COW sources from submodule
$(COW_OBJ): $(BUILD)/%.o: $(COW)/%.c $(CONFIG_H) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Sound needs SDL2 mixer headers
# Patch: stop intro sound when entering ship
$(BUILD)/sound.o: $(COW)/sound.c $(CONFIG_H) | $(BUILD)
	sed 's/void Play_Sound(int type) {/void Play_Sound(int type) { \
	if (type == 15) Mix_HaltChannel(-1);/' $< > $(BUILD)/sound_patched.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL2_CFLAGS) -c $(BUILD)/sound_patched.c -o $@

$(COW_SDL_OBJ): $(BUILD)/%.o: $(COW)/%.c $(CONFIG_H) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL2_CFLAGS) -c $< -o $@

# Our SDL2 files
$(SDL2_OBJ): $(BUILD)/%.o: %.c $(CONFIG_H) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD) config.h null

.PHONY: all clean null
