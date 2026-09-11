#!/bin/sh
# Build "Netrek COM.app" — a self-contained, double-clickable macOS bundle,
# plus the zip we attach to a GitHub release.
#
#   tools/make-app.sh [version]
#
# COW locates pixmaps/ and sounds/ relative to the working directory, so the
# bundle's executable is a small launcher that cd's into Resources first.
# dylibbundler vendors the Homebrew SDL2 stack into Contents/Frameworks so the
# player needs no Homebrew.
set -e

HERE="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${1:-0.9}"
APP="$HERE/build/Netrek COM.app"
ZIP="$HERE/build/netrek-com-$VERSION-macos-arm64.zip"

make -C "$HERE"

rm -rf "$APP" "$ZIP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"

cp "$HERE/build/netrek-sdl2" "$APP/Contents/MacOS/netrek-sdl2"
# pixmaps and sounds are symlinks into sibling repos; -L copies the real files.
cp -RL "$HERE/pixmaps" "$HERE/pixmaps-hr" "$HERE/sounds" "$APP/Contents/Resources/"

# The assets the game silently runs without: a missing ship sprite only shows up
# as blank ships mid-game, so fail the build here instead.
for asset in pixmaps/Fed/CA.png pixmaps-hr/Fed/CA.png sounds/nt_explosion.wav; do
  [ -f "$APP/Contents/Resources/$asset" ] || { echo "FAIL: missing $asset in bundle" >&2; exit 1; }
done

cat > "$APP/Contents/MacOS/netrek-com" <<'LAUNCHER'
#!/bin/sh
BIN="$(cd "$(dirname "$0")" && pwd)/netrek-sdl2"
cd "$(dirname "$BIN")/../Resources"
# A server named on the command line beats ~/.netrekrc, so only supply our
# default when the player has not set one of their own.
if grep -qE '^[[:space:]]*server:' "$HOME/.netrekrc" 2>/dev/null; then
  exec "$BIN" "$@"
fi
exec "$BIN" -h sturgeon.elevateconsulting.dev "$@"
LAUNCHER
chmod +x "$APP/Contents/MacOS/netrek-com"

# Icon, if the source png is present.
if [ -f "$HERE/tools/netrek-com.png" ]; then
  ICONSET="$(mktemp -d)/netrek-com.iconset"
  mkdir -p "$ICONSET"
  for sz in 16 32 64 128 256 512; do
    sips -z $sz $sz "$HERE/tools/netrek-com.png" --out "$ICONSET/icon_${sz}x${sz}.png" >/dev/null
    sips -z $((sz*2)) $((sz*2)) "$HERE/tools/netrek-com.png" --out "$ICONSET/icon_${sz}x${sz}@2x.png" >/dev/null
  done
  iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/netrek-com.icns"
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Netrek COM</string>
  <key>CFBundleDisplayName</key><string>Netrek COM</string>
  <key>CFBundleExecutable</key><string>netrek-com</string>
  <key>CFBundleIdentifier</key><string>dev.elevateconsulting.netrek-com</string>
  <key>CFBundleIconFile</key><string>netrek-com</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

dylibbundler -of -b -x "$APP/Contents/MacOS/netrek-sdl2" \
             -d "$APP/Contents/Frameworks" -p @executable_path/../Frameworks >/dev/null

# Ad-hoc signature: enough to launch, not enough to skip Gatekeeper's
# right-click-Open on first run. Real notarization needs a Developer ID.
codesign --force --deep --sign - "$APP"

# The one thing that silently breaks on a machine without Homebrew.
leaks=$(find "$APP" -type f \( -name '*.dylib' -o -name 'netrek-sdl2' \) -exec otool -L {} + \
        | grep -E '^\s+(/opt/homebrew|/usr/local)' || true)
if [ -n "$leaks" ]; then
  echo "FAIL: bundle still references Homebrew paths:" >&2
  echo "$leaks" >&2
  exit 1
fi

ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"
echo "built $APP"
echo "zipped $ZIP"
