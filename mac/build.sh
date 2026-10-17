#!/bin/zsh
# Builds Mosics.app into ./dist. Usage: ./build.sh [--install]  (--install copies it to /Applications)
set -euo pipefail
cd "$(dirname "$0")"

# Universal (Apple Silicon + Intel) when possible; never fall back to a stale universal binary.
if swift build -c release --arch arm64 --arch x86_64 >/dev/null 2>&1; then
  BIN=.build/apple/Products/Release/Mosics
else
  swift build -c release
  BIN=$(swift build -c release --show-bin-path)/Mosics
fi

APP=dist/Mosics.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/Mosics"
strip -x "$APP/Contents/MacOS/Mosics" 2>/dev/null || true

if [[ ! -f .build/AppIcon.icns || scripts/make-icon.swift -nt .build/AppIcon.icns ]]; then
  rm -rf .build/AppIcon.iconset
  swift scripts/make-icon.swift .build/AppIcon.iconset
  iconutil -c icns .build/AppIcon.iconset -o .build/AppIcon.icns
fi
cp .build/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"

cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Mosics</string>
  <key>CFBundleDisplayName</key><string>Mosics</string>
  <key>CFBundleIdentifier</key><string>local.mosics.app</string>
  <key>CFBundleExecutable</key><string>Mosics</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.music</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSPrincipalClass</key><string>NSApplication</string>
</dict>
</plist>
PLIST

codesign --force --sign - "$APP" >/dev/null
echo "Built $APP ($(du -sh "$APP" | cut -f1))"

if [[ "${1:-}" == "--install" ]]; then
  rm -rf /Applications/Mosics.app
  cp -R "$APP" /Applications/
  echo "Installed to /Applications/Mosics.app"
fi
