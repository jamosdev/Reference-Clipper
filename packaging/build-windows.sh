#!/usr/bin/env bash
# Builds, tests and packages the Windows build as dist/*.zip. Runs in Git Bash on
# a Windows runner, with Qt's llvm-mingw build and the toolchain it was made with.
set -euo pipefail
version="${1:?usage: packaging/build-windows.sh <version>}"
cd "$(dirname "$0")/.."
source packaging/versions.sh
root="$(cygpath -m "$PWD")"
deps="$root/.deps/windows"
mkdir -p "$deps"

[ -f "$deps/venv/Scripts/aqt.exe" ] || { python -m venv "$deps/venv" && "$deps/venv/Scripts/pip" install -q aqtinstall cmake ninja; }
export PATH="$(cygpath -u "$deps/venv/Scripts"):$PATH"

qt="$deps/qt/$QT_VERSION/llvm-mingw_64"
[ -d "$qt" ] || aqt install-qt windows desktop "$QT_VERSION" win64_llvm_mingw -O "$deps/qt" -m qtimageformats
toolchain="$(ls -d "$deps"/qt/Tools/llvm-mingw*_64 2>/dev/null | head -1 || true)"
if [ -z "$toolchain" ]; then
    aqt install-tool windows desktop "$LLVM_MINGW_TOOL" "$LLVM_MINGW_PACKAGE" -O "$deps/qt"
    toolchain="$(ls -d "$deps"/qt/Tools/llvm-mingw*_64 | head -1)"
fi

# LGPL FFmpeg ships in the zip; the GPL build only makes test fixtures (libx264).
fetch_ffmpeg() {
    local flavour="$1" dir="$deps/ffmpeg-$1"
    if [ ! -d "$dir" ]; then
        curl -fsSL -o "$deps/ffmpeg-$flavour.zip" \
            "$BTBN/ffmpeg-n$FFMPEG_BRANCH-latest-win64-$flavour-shared-$FFMPEG_BRANCH.zip"
        unzip -q "$deps/ffmpeg-$flavour.zip" -d "$deps/unpack-$flavour"
        mv "$deps/unpack-$flavour"/ffmpeg-* "$dir"
        rm -rf "$deps/unpack-$flavour" "$deps/ffmpeg-$flavour.zip"
    fi
    echo "$dir"
}
ffmpeg_lgpl="$(fetch_ffmpeg lgpl)"
ffmpeg_gpl="$(fetch_ffmpeg gpl)"

# Executables find DLLs on PATH: the compiler runtime, Qt, and the FFmpeg that ships.
export PATH="$(cygpath -u "$toolchain/bin"):$(cygpath -u "$qt/bin"):$(cygpath -u "$ffmpeg_lgpl/bin"):$PATH"

build="$root/build-windows"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_PREFIX_PATH="$qt" -DFFMPEG_ROOT="$ffmpeg_lgpl"
cmake --build "$build"
FFMPEG="$ffmpeg_gpl/bin/ffmpeg.exe" QT_QPA_PLATFORM=offscreen ctest --test-dir "$build" --output-on-failure

name="reference-clipper-$version-windows-x86_64"
stage="$root/dist/$name"
rm -rf "$stage"
mkdir -p "$stage/licenses"
cp "$build/reference-clipper.exe" "$stage/"
windeployqt --release --no-translations --compiler-runtime --dir "$stage" "$stage/reference-clipper.exe"
cp "$ffmpeg_lgpl"/bin/*.dll "$stage/"
cp packaging/THIRD-PARTY.md assets/fonts/OFL.txt "$stage/licenses/"
cp "$ffmpeg_lgpl/LICENSE.txt" "$stage/licenses/FFmpeg-LICENSE.txt"

rm -f "$root/dist/"*windows*.zip
(cd "$root/dist" && 7z a -tzip -bso0 "$name.zip" "$name")
rm -rf "$stage"
ls -l "$root/dist"
