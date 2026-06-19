#!/bin/bash
# Remove the input-access setup again (for re-testing the first-run dialog).
set -e

if [[ $EUID -ne 0 ]]; then
    exec sudo "$0" "$@"
fi

rm -f /etc/udev/rules.d/70-karyon.rules
rm -f /etc/modules-load.d/karyon-uinput.conf

udevadm control --reload-rules
udevadm trigger --subsystem-match=input
udevadm trigger --subsystem-match=misc --name-match=uinput

echo "Input access removed. The first-run permission dialog will appear again on"
echo "the next launch. If the ACL still sticks, log out and back in once."
