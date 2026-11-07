#!/usr/bin/env bash
# Builds, tests and packages the Linux AppImage as dist/*.zip.
# Runs in an ubuntu:22.04 container: its glibc (2.35) is the oldest the AppImage
# supports, and Qt and FFmpeg come prebuilt for even older ones.
set -euo pipefail
version="${1:?usage: packaging/build-appimage.sh <version>}"
cd "$(dirname "$0")/.."
source packaging/versions.sh
root="$PWD"
deps="$root/.deps/linux"
mkdir -p "$deps"

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    build-essential python3-venv curl ca-certificates xz-utils zip file git \
    libpulse-dev libgl1-mesa-dev libegl1 libfontconfig1 libfreetype6 libdbus-1-3 \
    libxkbcommon0 libxkbcommon-x11-0 libx11-xcb1 libxcb-cursor0 libxcb-icccm4 libxcb-image0 \
    libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 \
    fonts-dejavu-core

[ -x "$deps/venv/bin/aqt" ] || { python3 -m venv "$deps/venv" && "$deps/venv/bin/pip" install -q aqtinstall cmake ninja; }
export PATH="$deps/venv/bin:$PATH"

qt="$deps/qt/$QT_VERSION/gcc_64"
[ -d "$qt" ] || aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O "$deps/qt" -m qtimageformats

# LGPL FFmpeg ships in the AppImage; the GPL build only makes test fixtures (libx264).
fetch_ffmpeg() {
    local flavour="$1" dir="$deps/ffmpeg-$1"
    if [ ! -d "$dir" ]; then
        mkdir -p "$dir"
        curl -fsSL "$BTBN/ffmpeg-n$FFMPEG_BRANCH-latest-linux64-$flavour-shared-$FFMPEG_BRANCH.tar.xz" \
            | tar -xJ -C "$dir" --strip-components=1
    fi
    echo "$dir"
}
ffmpeg_lgpl="$(fetch_ffmpeg lgpl)"
ffmpeg_gpl="$(fetch_ffmpeg gpl)"

for tool in linuxdeploy linuxdeploy-plugin-qt; do
    if [ ! -x "$deps/$tool" ]; then
        curl -fsSL -o "$deps/$tool" \
            "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-x86_64.AppImage"
        chmod +x "$deps/$tool"
    fi
done

build="$root/build-appimage"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qt" -DFFMPEG_ROOT="$ffmpeg_lgpl"
cmake --build "$build"
LD_LIBRARY_PATH="$ffmpeg_lgpl/lib" FFMPEG="$ffmpeg_gpl/bin/ffmpeg" QT_QPA_PLATFORM=offscreen \
    ctest --test-dir "$build" --output-on-failure

appdir="$build/AppDir"
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build" --prefix /usr
mkdir -p "$appdir/usr/share/licenses/reference-clipper"
cp packaging/THIRD-PARTY.md assets/fonts/OFL.txt "$appdir/usr/share/licenses/reference-clipper/"
cp "$ffmpeg_lgpl/LICENSE.txt" "$appdir/usr/share/licenses/reference-clipper/FFmpeg-LICENSE.txt"

export APPIMAGE_EXTRACT_AND_RUN=1  # no FUSE in containers
export QMAKE="$qt/bin/qmake"
export LD_LIBRARY_PATH="$ffmpeg_lgpl/lib:$qt/lib"
export LDAI_OUTPUT="reference-clipper-$version-x86_64.AppImage"
cd "$build"
"$deps/linuxdeploy" --appdir "$appdir" \
    --executable "$appdir/usr/bin/reference-clipper" \
    --desktop-file "$root/packaging/reference-clipper.desktop" \
    --icon-file "$root/assets/icons/reference-clipper.svg" \
    --plugin qt --output appimage

mkdir -p "$root/dist"
rm -f "$root/dist/"*linux*.zip
zip -j "$root/dist/reference-clipper-$version-linux-x86_64-appimage.zip" "$LDAI_OUTPUT"
ls -l "$root/dist"
