#!/bin/sh
# Install (or with --remove, uninstall) the hourly QL-570 keep-alive.
#   sudo keepalive/install_keepalive.sh [--remove]
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
DEST=/Library/Printers/BrotherQL570Native
PLIST=/Library/LaunchDaemons/local.brother-ql570.keepalive.plist

if [ "$(id -u)" != 0 ]; then
    echo "Please run with sudo: sudo $0 $*" >&2
    exit 1
fi

if [ "${1:-}" = "--remove" ]; then
    launchctl bootout system "$PLIST" 2>/dev/null || true
    rm -f "$PLIST" "$DEST/ql570-keepalive.sh" "$DEST/qlstatus"
    rmdir "$DEST" 2>/dev/null || true
    echo "Keep-alive removed (log left at /Library/Logs/ql570-keepalive.log)."
    exit 0
fi

mkdir -p "$DEST"
install -o root -g wheel -m 755 "$HERE/build/qlstatus" "$DEST/qlstatus"
install -o root -g wheel -m 755 "$HERE/keepalive/ql570-keepalive.sh" "$DEST/ql570-keepalive.sh"
install -o root -g wheel -m 644 "$HERE/keepalive/local.brother-ql570.keepalive.plist" "$PLIST"
launchctl bootout system "$PLIST" 2>/dev/null || true
launchctl bootstrap system "$PLIST"
sleep 2
echo "Keep-alive installed; it runs now and then every hour."
tail -n 1 /Library/Logs/ql570-keepalive.log 2>/dev/null || true
