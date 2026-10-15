#!/bin/sh
# Install the native Brother QL-570 driver and add a print queue.
#
#   sudo ./install.sh            install, and add queue "Brother_QL_570_Native"
#   sudo ./install.sh --no-queue install the driver only
#
# The existing Brother queue is left untouched so the two can be compared.
set -eu

QUEUE=Brother_QL_570_Native
DEST=/Library/Printers/BrotherQL570Native
PPDDIR="/Library/Printers/PPDs/Contents/Resources"
PPDNAME="Brother QL-570 (native).gz"
HERE=$(cd "$(dirname "$0")" && pwd)

if [ "$(id -u)" != 0 ]; then
    echo "Please run with sudo: sudo $0 $*" >&2
    exit 1
fi
if [ ! -x "$HERE/build/rastertoql570" ]; then
    echo "Build first: make" >&2
    exit 1
fi

echo "Installing filter to $DEST"
mkdir -p "$DEST"
install -o root -g wheel -m 755 "$HERE/build/rastertoql570" "$DEST/rastertoql570"
install -o root -g wheel -m 755 "$HERE/build/qlstatus" "$DEST/qlstatus"
install -o root -g wheel -m 644 "$HERE/ppd/Brother_QL-570.ppd" "$DEST/Brother_QL-570.ppd"

echo "Installing PPD to $PPDDIR"
mkdir -p "$PPDDIR"
gzip -9 -c "$HERE/ppd/Brother_QL-570.ppd" > "$PPDDIR/$PPDNAME"
chown root:wheel "$PPDDIR/$PPDNAME"
chmod 644 "$PPDDIR/$PPDNAME"

if [ "${1:-}" = "--no-queue" ]; then
    echo "Driver installed (no queue added)."
    exit 0
fi

URI=$(lpstat -v 2>/dev/null | sed -n 's/^device for [^:]*: \(usb:\/\/Brother\/QL-570[^ ]*\).*/\1/p' | head -1)
if [ -z "$URI" ]; then
    URI=$(lpinfo -v 2>/dev/null | sed -n 's/^direct \(usb:\/\/Brother\/QL-570[^ ]*\).*/\1/p' | head -1)
fi
if [ -z "$URI" ]; then
    echo "Could not find the QL-570's USB address - connect and switch it on, then run this again." >&2
    exit 1
fi

echo "Adding queue $QUEUE -> $URI"
lpadmin -p "$QUEUE" -E -v "$URI" -P "$DEST/Brother_QL-570.ppd" \
    -D "Brother QL-570 (native)" -L "USB" -o printer-is-shared=false
cupsenable "$QUEUE"
cupsaccept "$QUEUE"
echo "Done."
