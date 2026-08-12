# Makefile for Paradise Netrek client with SDL2 backend (macOS/arm64)
#
# Compiles the Paradise C source from the submodule unchanged,
# replacing x11window.c with our SDL2 version.
# All build artifacts go into build/.

CC = cc
SHELL = /bin/sh

# Build output directory
BUILD = build

# Paradise source directory
PARADISE = ../../reference_repos/netrek-client-paradise

# SDL2 flags
SDL2_CFLAGS = $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image SDL2_mixer 2>/dev/null)
SDL2_LIBS   = $(shell pkg-config --libs   sdl2 SDL2_ttf SDL2_image SDL2_mixer 2>/dev/null)

# Compiler flags
# -I. picks up our Wlib.h, sdl2_config.h (as "config.h"), and sdl2window.h
# -I$(PARADISE)/include picks up Paradise headers (defs.h, struct.h, data.h, etc.)
# Our Wlib.h and config.h override the submodule's versions via include order.
CFLAGS = -g -Wall -Wno-implicit-function-declaration -Wno-parentheses \
         -Wno-format -Wno-unused-variable -Wno-pointer-sign \
         -Wno-incompatible-pointer-types -Wno-int-conversion \
         -DSHIP_BITMAP_PATH=\"images\" \
         -I. -I$(PARADISE)/include $(SDL2_CFLAGS)
LDFLAGS = -g
LIBS = $(SDL2_LIBS) -lz -lm

# Our config.h must override Paradise's
CONFIG_H = config.h

# ============================================================
# Paradise source files (reused as-is, excluding x11window.c)
# ============================================================

PARADISE_SRC = \
    beeplite.c \
    blk_parsemotd.c \
    colors.c \
    credits.c \
    dashboard.c \
    data.c \
    death.c \
    defaults.c \
    defwin.c \
    detonate.c \
    distress.c \
    dmessage.c \
    enter.c \
    feature.c \
    findslot.c \
    gameconf.c \
    getname.c \
    getship.c \
    helpwin.c \
    hockey.c \
    images.c \
    inform.c \
    input.c \
    interface.c \
    keymap.c \
    lib.c \
    local.c \
    macros.c \
    macrowin.c \
    main.c \
    map.c \
    motdwin.c \
    newstats.c \
    newwin.c \
    option.c \
    packets.c \
    parsemeta.c \
    ping.c \
    pingstats.c \
    planetlist.c \
    planets.c \
    playerlist.c \
    ranklist.c \
    ratings.c \
    recorder.c \
    redraw.c \
    rotate.c \
    senddist.c \
    shortcomm.c \
    sintab.c \
    smessage.c \
    socket.c \
    spopt.c \
    stats.c \
    tools.c \
    udpopt.c \
    util.c \
    varydamage.c \
    war.c \
    warning.c \
    wide_plist.c

# ============================================================
# Our SDL2 replacement file
# ============================================================
SDL2_SRC = sdl2window.c sdl2sound.c

# ============================================================
# Object files (all in build/)
# ============================================================
PARADISE_OBJ = $(addprefix $(BUILD)/,$(PARADISE_SRC:.c=.o))
SDL2_OBJ     = $(addprefix $(BUILD)/,$(SDL2_SRC:.c=.o))

ALL_OBJ = $(PARADISE_OBJ) $(SDL2_OBJ)

TARGET = $(BUILD)/paradise-sdl2

# ============================================================
# Targets
# ============================================================

all: $(CONFIG_H) $(TARGET)

$(TARGET): $(ALL_OBJ)
	$(CC) $(LDFLAGS) -o $@ $(ALL_OBJ) $(LIBS)
	@echo "=== Build complete: $(TARGET) ==="

# Create build directory
$(BUILD):
	mkdir -p $(BUILD)

# Create config.h symlink to our sdl2_config.h
$(CONFIG_H):
	ln -sf sdl2_config.h config.h

# Compile Paradise sources from submodule
$(PARADISE_OBJ): $(BUILD)/%.o: $(PARADISE)/src/%.c $(CONFIG_H) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Our SDL2 files
$(SDL2_OBJ): $(BUILD)/%.o: %.c $(CONFIG_H) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD) config.h

.PHONY: all clean
