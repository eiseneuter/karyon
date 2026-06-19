#!/bin/bash
# Run Karyon from source.
#
# Auto-detects the display server: do NOT force QT_QPA_PLATFORM, so it uses the
# native Wayland plugin on Wayland and xcb on X11 (both supported).
#
#   ./run.sh            normal run
#   ./run.sh --debug    verbose logging (also /tmp/karyon-debug.log)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

# Make sure no stale single-instance lock blocks a fresh start after a crash.
LOCK="${XDG_DATA_HOME:-$HOME/.local/share}/karyon/instance.lock"
if [[ -f "$LOCK" ]] && ! pgrep -f '^python3? -m karyon' >/dev/null 2>&1; then
    rm -f "$LOCK"
fi

exec python3 -m karyon "$@"
