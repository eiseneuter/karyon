#!/bin/bash
# STANDARD BUILD: build the AppImage inside ubuntu:20.04 (glibc 2.31) with all
# system libs bundled, so it runs self-contained on fresh Plasma 6 systems.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

docker run --rm \
    -e BUNDLE_SYSTEM_LIBS=1 \
    -v "$HERE":/src \
    -w /src \
    ubuntu:20.04 \
    bash -c '
        set -e
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        apt-get install -y --no-install-recommends \
            curl ca-certificates file desktop-file-utils build-essential clang \
            libxcb1 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
            libxcb-randr0 libxcb-render-util0 libxcb-render0 libxcb-shape0 \
            libxcb-shm0 libxcb-sync1 libxcb-util1 libxcb-xfixes0 libxcb-xinerama0 \
            libxcb-xkb1 libxkbcommon-x11-0 libxkbcommon0 \
            libwayland-client0 libwayland-cursor0 libwayland-egl1 \
            libfontconfig1 libfreetype6 libharfbuzz0b libglib2.0-0 \
            libdbus-1-3 libegl1 libgl1
        ./build-appimage.sh
        chown -R '"$(id -u)":"$(id -g)"' /src/build /src/dist
    '

echo "Done. Result: $HERE/dist/Karyon-x86_64.AppImage"
