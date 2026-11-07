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
# By hand rather than windeployqt, which does not recognise Qt's llvm-mingw
# build (win32-clang-g++): it fails on the compiler runtime and then cannot find
# the platform plugin. The import check below proves the set is complete.
for module in Core Gui Widgets Svg; do cp "$qt/bin/Qt6$module.dll" "$stage/"; done
for plugin in platforms/qwindows styles/qmodernwindowsstyle imageformats/qsvg imageformats/qico iconengines/qsvgicon; do
    mkdir -p "$stage/$(dirname "$plugin")"
    cp "$qt/plugins/$plugin.dll" "$stage/$plugin.dll"
done
cp "$ffmpeg_lgpl"/bin/*.dll "$stage/"
# The compiler runtime, the x86_64 target's copies.
for dll in libc++.dll libunwind.dll libwinpthread-1.dll; do
    for candidate in "$toolchain/x86_64-w64-mingw32/bin/$dll" "$toolchain/bin/$dll"; do
        if [ -f "$candidate" ]; then
            cp "$candidate" "$stage/"
            break
        fi
    done
done

# Nothing on the build machine's PATH exists on a user's PC, so every DLL any
# shipped binary imports must be in the zip or be part of Windows.
system="$(cygpath -u "$SYSTEMROOT/System32")"
known="$( { ls "$stage"; ls "$system"; } | tr '[:upper:]' '[:lower:]' | sort -u)"
# Captured first: piping into grep -q under pipefail fails at random on SIGPIPE.
exe_headers="$(llvm-objdump -p "$stage/reference-clipper.exe" || true)"
if ! grep -q 'DLL Name:' <<<"$exe_headers"; then
    echo "the import check cannot read imports; refusing to vouch for the zip"
    exit 1
fi
missing=0
while IFS= read -r binary; do
    while IFS= read -r dll; do
        lower="$(tr '[:upper:]' '[:lower:]' <<<"$dll")"
        case "$lower" in api-ms-win-*|ext-ms-win-*) continue ;; esac
        if ! grep -qxF "$lower" <<<"$known"; then
            echo "missing from the zip: $dll (needed by ${binary#$stage/})"
            missing=1
        fi
    done < <(llvm-objdump -p "$binary" | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p' | tr -d '\r')
done < <(find "$stage" -iname '*.exe' -o -iname '*.dll')
[ "$missing" = 0 ] || exit 1
echo "import check: every DLL that $(find "$stage" -iname '*.exe' -o -iname '*.dll' | wc -l) shipped binaries import is present"
cp packaging/THIRD-PARTY.md assets/fonts/OFL.txt "$stage/licenses/"
cp "$ffmpeg_lgpl/LICENSE.txt" "$stage/licenses/FFmpeg-LICENSE.txt"

rm -f "$root/dist/"*windows*.zip
(cd "$root/dist" && 7z a -tzip -bso0 "$name.zip" "$name")
rm -rf "$stage"
ls -l "$root/dist"
