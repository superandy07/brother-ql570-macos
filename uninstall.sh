#!/bin/sh
# Remove the native QL-570 driver and its queue (the keep-alive is separate:
# keepalive/install_keepalive.sh --remove).
#   sudo ./uninstall.sh
set -eu
QUEUE=Brother_QL_570_Native
DEST=/Library/Printers/BrotherQL570Native

if [ "$(id -u)" != 0 ]; then
    echo "Please run with sudo: sudo $0" >&2
    exit 1
fi
if lpstat -p "$QUEUE" >/dev/null 2>&1; then
    echo "Removing queue $QUEUE"
    lpadmin -x "$QUEUE"
fi
rm -f "/Library/Printers/PPDs/Contents/Resources/Brother QL-570 (native).gz"
rm -f "$DEST/rastertoql570" "$DEST/Brother_QL-570.ppd"
# qlstatus stays if the keep-alive still uses it
[ -f "$DEST/ql570-keepalive.sh" ] || rm -f "$DEST/qlstatus"
rmdir "$DEST" 2>/dev/null || true
echo "Removed."
