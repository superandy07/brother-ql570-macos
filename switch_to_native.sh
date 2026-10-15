#!/bin/sh
# Switch the QL-570 over to the native driver: share the native queue,
# remove the old Brother queue, and remove Brother's (Intel-only) driver.
#   sudo ./switch_to_native.sh
# Back up /Library/Printers/Brother, its PPDs and your queue's PPD first if you
# might want the old driver back (see README).
set -eu

NATIVE=Brother_QL_570_Native
OLD=Brother_QL_570
PKG=com.Brother.Brotherdriver.Brother_PrinterDrivers_Ptouch_noSTM

if [ "$(id -u)" != 0 ]; then
    echo "Please run with sudo: sudo $0" >&2
    exit 1
fi
if ! lpstat -p "$NATIVE" >/dev/null 2>&1; then
    echo "Queue $NATIVE not found - run install.sh first" >&2
    exit 1
fi

echo "Sharing $NATIVE"
lpadmin -p "$NATIVE" -o printer-is-shared=true

if lpstat -p "$OLD" >/dev/null 2>&1; then
    echo "Removing old queue $OLD"
    lpadmin -x "$OLD"
fi

if grep -l 'Library/Printers/Brother/' /etc/cups/ppd/*.ppd >/dev/null 2>&1; then
    echo "Another queue still uses Brother's driver - leaving it installed."
    exit 0
fi

echo "Removing Brother's P-touch/QL driver"
if pkgutil --pkg-info "$PKG" >/dev/null 2>&1; then
    pkgutil --files "$PKG" | grep -E '^Library/Printers/PPDs/Contents/Resources/.+' | while read -r f; do
        rm -f "/$f"
    done
    pkgutil --forget "$PKG" >/dev/null
fi
rm -rf /Library/Printers/Brother
echo "Done."
