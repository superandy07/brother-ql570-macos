#!/bin/sh
# Keep the Brother QL-570 from auto powering off (it does so after 4 hours idle).
# Run hourly by launchd: sends one status request, which prints nothing and
# changes no settings but resets the printer's idle timer. Logs one line.
LOG=/Library/Logs/ql570-keepalive.log
/Library/Printers/BrotherQL570Native/qlstatus -q
case $? in
    0) result="ok" ;;
    2) result="printer off or unplugged" ;;
    3) result="busy printing (skipped)" ;;
    *) result="error" ;;
esac
echo "$(date '+%Y-%m-%d %H:%M:%S') $result" >> "$LOG"
# keep the log small
if [ "$(wc -l < "$LOG")" -gt 2000 ]; then
    tail -n 1000 "$LOG" > "$LOG.tmp" && mv "$LOG.tmp" "$LOG"
fi
exit 0
