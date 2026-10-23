#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="karyon-appimage-builder"
USER_ID="$(id -u)"
GROUP_ID="$(id -g)"

echo "=== [1/2] Erstelle Docker Builder Image ($IMAGE_NAME) ==="
docker build -f "$ROOT_DIR/Dockerfile.appimage" -t "$IMAGE_NAME" "$ROOT_DIR"

echo "=== [2/2] Baue AppImage im Docker-Container ==="
mkdir -p "$ROOT_DIR/dist"

docker run --rm \
    -e APPIMAGE_EXTRACT_AND_RUN=1 \
    -v "$ROOT_DIR:/src:ro" \
    -v "$ROOT_DIR/dist":/dist \
    -e DIST_DIR=/dist \
    -w /src \
    "$IMAGE_NAME" \
    bash -c '
        set -euo pipefail
        /src/build_appimage.sh
        chown -R '"$USER_ID:$GROUP_ID"' /dist
    '

echo ""
echo "=========================================================="
echo " Fertig! Das AppImage wurde erfolgreich generiert:"
echo " -> $ROOT_DIR/dist/Karyon-x86_64.AppImage"
echo " -> $ROOT_DIR/dist/Karyon-x86_64.AppImage.zsync"
echo " (Lade für GitHub Releases BEIDE Dateien hoch, damit Gear Lever & Co. Auto-Updates durchführen können!)"
echo "=========================================================="
