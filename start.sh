#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Karyon C++ Test Start ==="

# Build project if cmake is available on host
if command -v cmake >/dev/null 2>&1; then
    cmake -B build -S .
    cmake --build build -j"$(nproc)"
fi

# Determine executable to run:
# If dist/Karyon-x86_64.AppImage exists, use it (fully bundled and guaranteed to match build)
if [ -f "$SCRIPT_DIR/dist/Karyon-x86_64.AppImage" ]; then
    APP_BIN="$SCRIPT_DIR/dist/Karyon-x86_64.AppImage"
elif [ -f "$SCRIPT_DIR/dist/Karyon.AppImage" ]; then
    APP_BIN="$SCRIPT_DIR/dist/Karyon.AppImage"
elif [ -f "$SCRIPT_DIR/build/bin/karyon" ]; then
    APP_BIN="$SCRIPT_DIR/build/bin/karyon"
else
    echo "Kein Binary oder AppImage gefunden. Starte Docker-Build..."
    "$SCRIPT_DIR/build_appimage_docker.sh"
    APP_BIN="$SCRIPT_DIR/dist/Karyon-x86_64.AppImage"
fi

# Terminate existing Karyon instances for exclusive mouse access
if pgrep -f 'python.*karyon' >/dev/null 2>&1; then
    echo "Stopping existing Python-Karyon instance for exclusive mouse access..."
    pkill -f 'python.*karyon' || true
    sleep 0.3
fi

if pgrep -f 'bin/karyon' >/dev/null 2>&1; then
    echo "Stopping previous Karyon C++ instance..."
    pkill -f 'bin/karyon' || true
    sleep 0.2
fi

if pgrep -f 'Karyon.*AppImage' >/dev/null 2>&1; then
    echo "Stopping previous Karyon AppImage instance..."
    pkill -f 'Karyon.*AppImage' || true
    sleep 0.2
fi

# QT_QPA_PLATFORM=xcb for KWin/XWayland (synchronous cursor positioning & override-redirect without animations)
export QT_QPA_PLATFORM=xcb

# English locale enforcement
export LC_ALL=en_US.UTF-8
export LANG=en_US.UTF-8
export LANGUAGE=en_US:en

echo "Starting Karyon C++ ($APP_BIN)..."
echo "Hold right mouse button for 120 ms."
echo "Press Ctrl+C to stop."

exec "$APP_BIN" "$@"
