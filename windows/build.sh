#!/bin/sh
# Cross-compiles dist/Mosics.exe from macOS/Linux with mingw-w64
# (macOS: brew install mingw-w64 · Ubuntu: apt install g++-mingw-w64-x86-64).
set -eu
cd "$(dirname "$0")"
CXX=${CXX:-x86_64-w64-mingw32-g++}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
VERSION=$(tr -d ' \n' < ../VERSION)                           # e.g. 1.1.0 — shared with the Mac app
NUM=$(echo "$VERSION" | awk -F. '{printf "%d,%d,%d,0", $1, $2, $3}')
mkdir -p dist build

$WINDRES -I res -DMOSICS_VERSION="\\\"$VERSION\\\"" -DMOSICS_VERSION_NUM="$NUM" res/app.rc -O coff -o build/app.res
$CXX -std=c++17 -Os -s -static -municode -mwindows -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DMOSICS_VERSION="\"$VERSION\"" \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -Wno-missing-field-initializers \
    src/main.cpp build/app.res -o dist/Mosics.exe \
    -lcomctl32 -lcomdlg32 -lwinhttp -lmfuuid -lgdiplus -ldwmapi -lshlwapi \
    -lshell32 -lole32 -loleaut32 -luuid -lpropsys -lversion

echo "Built dist/Mosics.exe $VERSION ($(du -h dist/Mosics.exe | cut -f1))"
