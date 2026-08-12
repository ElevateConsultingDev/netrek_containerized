# Makefile for COW Netrek client with SwiftUI/CoreGraphics backend (macOS/arm64)
#
# Compiles the COW C source from the submodule unchanged,
# replacing x11window.c/x11sprite.c/camera.c with our CoreGraphics versions.
# Links with Swift frontend for SwiftUI display.

CC = cc
SWIFTC = swiftc
SHELL = /bin/sh

# Build output directory
BUILD = build

# COW source directory
COW = ../netrek-client-cow

# Bridge source directory
BRIDGE = NetrekSwiftUI/Bridge

# Swift source directories
SWIFT_APP = NetrekSwiftUI/App
SWIFT_VIEWS = NetrekSwiftUI/Views
SWIFT_VM = NetrekSwiftUI/ViewModels

# Compiler flags
# -I$(BRIDGE) picks up our swiftui_config.h (as "config.h") and swiftuiwindow.h
# -I$(COW) picks up all COW headers (Wlib.h, defs.h, struct.h, etc.)
CFLAGS = -g -Wall -Wno-implicit-function-declaration -Wno-parentheses \
         -Wno-format -Wno-unused-variable -Wno-pointer-sign \
         -Wno-deprecated-declarations \
         -I$(BRIDGE) -I$(COW)
LDFLAGS = -g

# Frameworks needed by CoreGraphics + SwiftUI backend
FRAMEWORKS = -framework CoreGraphics -framework CoreText -framework ImageIO \
             -framework Cocoa -framework SwiftUI

# ============================================================
# COW source files (reused as-is)
# ============================================================

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
    input.c redraw.c

# ============================================================
# Our SwiftUI/CoreGraphics replacement files
# ============================================================
SWIFTUI_C_SRC = swiftuiwindow.c swiftuisprite.c sound_stubs.c

# ============================================================
# Swift source files
# ============================================================
SWIFT_SRC = \
    $(SWIFT_APP)/NetrekApp.swift \
    $(SWIFT_APP)/ContentView.swift \
    $(SWIFT_VIEWS)/PixelBufferView.swift \
    $(SWIFT_VM)/EngineController.swift

# ============================================================
# Object files (all in build/)
# ============================================================
COW_OBJ     = $(addprefix $(BUILD)/,$(COW_SRC:.c=.o))
SWIFTUI_C_OBJ = $(addprefix $(BUILD)/,$(SWIFTUI_C_SRC:.c=.o))

ALL_C_OBJ = $(COW_OBJ) $(SWIFTUI_C_OBJ)

# config.h symlink target
CONFIG_H = $(BRIDGE)/config.h

# App bundle
APP_NAME = NetrekSwiftUI
APP_BUNDLE = $(BUILD)/$(APP_NAME).app
TARGET = $(APP_BUNDLE)/Contents/MacOS/$(APP_NAME)

# ============================================================
# Targets
# ============================================================

all: $(CONFIG_H) null $(TARGET)
	@echo "=== Build complete: $(APP_BUNDLE) ==="
	@echo "Run with: open $(APP_BUNDLE)"

$(TARGET): $(ALL_C_OBJ) $(SWIFT_SRC) | $(BUILD) appbundle
	$(SWIFTC) -g \
	    -import-objc-header $(BRIDGE)/NetrekBridge.h \
	    -I$(BRIDGE) -I$(COW) \
	    $(FRAMEWORKS) \
	    $(ALL_C_OBJ) \
	    $(SWIFT_SRC) \
	    -lm -o $@

# Create build directory
$(BUILD):
	mkdir -p $(BUILD)

# Create app bundle structure
appbundle: | $(BUILD)
	mkdir -p $(APP_BUNDLE)/Contents/MacOS
	mkdir -p $(APP_BUNDLE)/Contents/Resources
	@echo '<?xml version="1.0" encoding="UTF-8"?>' > $(APP_BUNDLE)/Contents/Info.plist
	@echo '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' >> $(APP_BUNDLE)/Contents/Info.plist
	@echo '<plist version="1.0"><dict>' >> $(APP_BUNDLE)/Contents/Info.plist
	@echo '<key>CFBundleExecutable</key><string>$(APP_NAME)</string>' >> $(APP_BUNDLE)/Contents/Info.plist
	@echo '<key>CFBundleIdentifier</key><string>org.netrek.swiftui</string>' >> $(APP_BUNDLE)/Contents/Info.plist
	@echo '<key>CFBundleName</key><string>Netrek</string>' >> $(APP_BUNDLE)/Contents/Info.plist
	@echo '<key>CFBundlePackageType</key><string>APPL</string>' >> $(APP_BUNDLE)/Contents/Info.plist
	@echo '<key>CFBundleVersion</key><string>1.0</string>' >> $(APP_BUNDLE)/Contents/Info.plist
	@echo '<key>LSMinimumSystemVersion</key><string>13.0</string>' >> $(APP_BUNDLE)/Contents/Info.plist
	@echo '<key>NSHighResolutionCapable</key><true/>' >> $(APP_BUNDLE)/Contents/Info.plist
	@echo '<key>NSPrincipalClass</key><string>NSApplication</string>' >> $(APP_BUNDLE)/Contents/Info.plist
	@echo '</dict></plist>' >> $(APP_BUNDLE)/Contents/Info.plist

# Create config.h symlink to our swiftui_config.h
$(CONFIG_H):
	cd $(BRIDGE) && ln -sf swiftui_config.h config.h

# Create null file (some COW sources include it)
null:
	@echo "/* intentionally empty */" > $(BRIDGE)/null

# Compile COW sources from submodule
$(COW_OBJ): $(BUILD)/%.o: $(COW)/%.c $(CONFIG_H) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Our bridge C files
$(SWIFTUI_C_OBJ): $(BUILD)/%.o: $(BRIDGE)/%.c $(CONFIG_H) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Create pixmaps symlink for sprite loading
pixmaps:
	ln -sf ../netrek-client-cow/pixmaps pixmaps

clean:
	rm -rf $(BUILD) $(BRIDGE)/config.h $(BRIDGE)/null pixmaps

.PHONY: all clean null appbundle pixmaps
