#!/bin/sh
# Cross-compiles dist/Mosics.exe from macOS/Linux with mingw-w64 (brew install mingw-w64).
# On Windows, build.bat does the same with MSYS2/MinGW.
set -eu
cd "$(dirname "$0")"
CXX=${CXX:-x86_64-w64-mingw32-g++}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
mkdir -p dist build

$WINDRES -I res res/app.rc -O coff -o build/app.res
$CXX -std=c++17 -Os -s -static -municode -mwindows -DUNICODE -D_UNICODE \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -Wno-missing-field-initializers \
    src/main.cpp build/app.res -o dist/Mosics.exe \
    -lcomctl32 -lwinhttp -lmfplay -lmfplat -lmfuuid -lgdiplus -ldwmapi -luxtheme -lshlwapi \
    -lshell32 -lole32 -loleaut32 -luuid -lpropsys

echo "Built dist/Mosics.exe ($(du -h dist/Mosics.exe | cut -f1))"
