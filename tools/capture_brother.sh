#!/bin/sh
# Run the Brother QL-570 filter (Intel, via Rosetta) on test labels for every
# label size and option value, saving its raster output in caps/.
# Nothing is sent to the printer.
#   tools/capture_brother.sh [brother.ppd]
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BP=${1:-/etc/cups/ppd/Brother_QL_570.ppd}
BF=/Library/Printers/Brother/Filter/rastertobrotherQL570.bundle/Contents/MacOS/rastertobrotherQL570
OUT="$ROOT/caps"
mkdir -p "$OUT"

# label PDFs sized to each media, so the raster matches what macOS sends
size() { grep -E "^\\*PaperDimension $1/" "$BP" | sed "s/.*:[[:space:]]*\"\([0-9.]*\)[[:space:]]*\([0-9.]*\)\".*/\1 \2/"; }

cap() {	# name pagesize "filter options" [copies]
    name=$1 ps=$2 opts=$3 copies=${4:-1}
    set -- $(size "$ps")
    pdf="$OUT/$ps.pdf"
    [ -f "$pdf" ] || python3 -I "$ROOT/tools/mklabel.py" "$pdf" "$1" "$2"
    ras="$OUT/$ps.$(echo "$opts" | tr ' =' '_-').ras"
    ropts=$(echo "$opts" | tr ' ' '\n' | grep -E '^Resolution=' | sed 's/^/-o /' || true)
    cupsfilter -p "$BP" -m application/vnd.cups-raster -o "PageSize=$ps" $ropts "$pdf" > "$ras" 2>/dev/null
    PPD="$BP" "$BF" 1 user test "$copies" "media=$ps PageSize=$ps $opts" "$ras" > "$OUT/$name.ql" 2> "$OUT/$name.log"
    cp "$ras" "$OUT/$name.ras"
}

for ps in $(grep -E '^\*PageSize ' "$BP" | sed 's/^\*PageSize \([^/]*\)\/.*/\1/'); do
    cap "size_$ps" "$ps" ""
done
cap base 62mm ""
cap prio_speed 62mm "BrPriority=BrSpeed"
cap res600 62mm "Resolution=300x600dpi"
for h in BrBinary BrDither BrErrorDiffusion; do cap "ht_$h" 62mm "BrHalftonePattern=$h"; done
for v in -20 -10 -5 5 10 20; do cap "bright_$v" 62mm "BrBrightness=$v"; cap "contrast_$v" 62mm "BrContrast=$v"; done
cap autocut_off 62mm "BrAutoTapeCut=OFF"
cap cutevery_3 62mm "BrCutLabel=3"
cap cutatend_off 62mm "BrCutAtEnd=OFF"
cap mirror 62mm "BrMirror=ON"
cap trim 62mm "BrRemoveBlkSpace=ON"
cap bidi_off 62mm "BrBiDiPrint=OFF"
cap copies3 62mm "" 3
cap dc07 DC07 ""
cap dc07_cut3 DC07 "BrCutLabel=3" 3
echo "captured $(ls "$OUT"/*.ql | wc -l | tr -d ' ') jobs"
