# Makefile for NetrekXP client with SDL2 backend (macOS/arm64)
#
# Compiles the NetrekXP C source from the submodule mostly unchanged,
# replacing mswindow.c/winmain.c/console.c/richedit.c with SDL2/POSIX versions.
# All build artifacts go into build/.

CC = cc
SHELL = /bin/sh

# Build output directory
BUILD = build

# NetrekXP source directories
XP = ../netrek-client-netrekxp
XP_SRC = $(XP)/src
XP_INC = $(XP)/include

# SDL2 flags
SDL2_CFLAGS = $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image SDL2_mixer)
SDL2_LIBS   = $(shell pkg-config --libs   sdl2 SDL2_ttf SDL2_image SDL2_mixer)

# Compiler flags
# -I. picks up our config.h first (overrides win32/config.h)
# -Icompat picks up our compat headers (windows.h, winsock.h, etc.)
# -I$(XP_INC) picks up NetrekXP headers (Wlib.h, defs.h, struct.h, etc.)
# -I$(XP_SRC) picks up source-level includes
CFLAGS = -g -fgnu89-inline -Wno-implicit-function-declaration -Wno-parentheses \
         -Wno-format -Wno-unused-variable -Wno-pointer-sign \
         -Wno-incompatible-pointer-types -Wno-int-conversion \
         -Wno-return-type -Wno-deprecated-declarations \
         -Wno-macro-redefined -Wno-unused-value \
         -Wno-implicit-int -Wno-dangling-else \
         -I. -Icompat -I$(XP_INC) -I$(XP_SRC) $(SDL2_CFLAGS)
LDFLAGS = -g
LIBS = $(SDL2_LIBS) -lm -lpthread -lz

# ============================================================
# NetrekXP source files (reused as-is from submodule)
# ============================================================

# Files we DON'T compile from submodule (replaced by our versions):
# mswindow.c  -> sdl2window.c
# winmain.c   -> main_posix.c
# console.c   -> console_posix.c
# richedit.c  -> (empty, not needed)
# cursors/curslib.c -> (not needed, cursor stubs in sdl2window.c)

XP_COMMON_SRC = \
    beeplite.c check.c colors.c cowmain.c \
    dashboard.c dashboard3.c data.c death.c defaults.c defwin.c \
    distress.c dmessage.c docwin.c enter.c feature.c findslot.c \
    getname.c getship.c helpwin.c hintwin.c inform.c interface.c \
    input.c local.c macrowin.c main.c map.c \
    newwin.c option.c parsemeta.c ping.c pingstats.c planetlist.c \
    playback.c playerlist.c ranklist.c redraw.c reserved.c rotate.c \
    senddist.c short.c sintab.c smessage.c socket.c sound.c \
    spopt.c stats.c string_util.c tools.c udpopt.c util.c \
    war.c warning.c

# ============================================================
# Our replacement/SDL2 files
# ============================================================
SDL2_SRC = sdl2window.c main_posix.c console_posix.c

# ============================================================
# Object files
# ============================================================
XP_OBJ  = $(addprefix $(BUILD)/,$(XP_COMMON_SRC:.c=.o))
SDL2_OBJ = $(addprefix $(BUILD)/,$(SDL2_SRC:.c=.o))

ALL_OBJ = $(XP_OBJ) $(SDL2_OBJ)

TARGET = $(BUILD)/netrekxp-sdl2

# ============================================================
# Targets
# ============================================================

all: $(TARGET)

$(TARGET): $(ALL_OBJ)
	$(CC) $(LDFLAGS) -o $@ $(ALL_OBJ) $(LIBS)
	@echo "=== Build complete: $(TARGET) ==="

$(BUILD):
	mkdir -p $(BUILD)

# Compile NetrekXP sources from submodule
$(XP_OBJ): $(BUILD)/%.o: $(XP_SRC)/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# local.c has a static/extern conflict for sound_flags
# Preprocess with sed to change the static decl to non-conflicting name
$(BUILD)/local.o: $(XP_SRC)/local.c | $(BUILD)
	sed 's/static unsigned int sound_flags/static unsigned int local_sound_flags/;s/sound_flags \& PFSHIELD/local_sound_flags \& PFSHIELD/g;s/sound_flags = me/local_sound_flags = me/g;s/^inline void/void/' $< > $(BUILD)/local_patched.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(BUILD)/local_patched.c -o $@

# sound.c: fix Windows backslash paths from netrekrc (e.g. ".\sounds" -> "./sounds")
$(BUILD)/sound.o: $(XP_SRC)/sound.c | $(BUILD)
	sed '/sounddir = ".\/sounds";/{n;s|}|}\n        { char *_sp; for (_sp=sounddir; *_sp; _sp++) if (*_sp==0x5c) *_sp=0x2f; }|;}' $< > $(BUILD)/sound_patched.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(BUILD)/sound_patched.c -o $@

# input.c non-threaded path has missing return type for input()
$(BUILD)/input.o: $(XP_SRC)/input.c | $(BUILD)
	sed 's/^input ()/void input()/' $< > $(BUILD)/input_patched.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(BUILD)/input_patched.c -o $@

# Our SDL2/POSIX replacement files
$(SDL2_OBJ): $(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
