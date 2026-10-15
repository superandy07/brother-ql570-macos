#!/bin/sh
# Offline tests: nothing is sent to the printer.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)

echo "== PPD check"
cupstestppd -W translations -W filters -W sizes "$ROOT/ppd/Brother_QL-570.ppd" | head -1

echo "== Comparison with Brother's QL-570 filter (captures in caps/)"
if [ -n "$(ls "$ROOT"/caps/*.ql 2>/dev/null)" ]; then
    python3 -I "$ROOT/test/compare_brother.py" "$ROOT" | tail -1
else
    echo "no captures - run tools/capture_brother.sh while Brother's driver is installed"
fi
