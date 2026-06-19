#!/bin/bash
# Grant input access for Karyon: udev uaccess for /dev/input/event* and
# /dev/uinput.  Run with sudo (or it will re-exec itself with sudo).
set -e

if [[ $EUID -ne 0 ]]; then
    exec sudo "$0" "$@"
fi

RULES=/etc/udev/rules.d/70-karyon.rules
cat > "$RULES" <<'EOF'
KERNEL=="event*", SUBSYSTEM=="input", TAG+="uaccess"
KERNEL=="uinput", SUBSYSTEM=="misc", TAG+="uaccess", OPTIONS+="static_node=uinput"
EOF

modprobe uinput || true
echo uinput > /etc/modules-load.d/karyon-uinput.conf

udevadm control --reload-rules
udevadm trigger --subsystem-match=input
udevadm trigger --subsystem-match=misc --name-match=uinput

echo "Input access installed. If the middle mouse button is still not detected,"
echo "log out and back in once so the ACLs take effect."
