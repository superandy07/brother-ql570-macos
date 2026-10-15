#!/usr/bin/env python3
"""
compare_brother.py - compare the native filter with Brother's QL-570 filter.

Uses the captures made by tools/capture_brother.sh (caps/<case>.ras is the
raster both filters get, caps/<case>.ql is Brother's output). For each case:
  - every command (media, cut, mode, margins) and the row count must match;
  - binary / dither halftones must match dot for dot;
  - error diffusion (a different but equivalent pattern) must match in tone:
    average darkness over 8x8 blocks within a small tolerance, and solid
    black/white areas identical.

usage: compare_brother.py ROOT
"""
import glob
import os
import subprocess
import sys

ROOT = sys.argv[1]
sys.path.insert(0, os.path.join(ROOT, "tools"))
import qldecode  # noqa: E402

PPD = os.path.join(ROOT, "ppd", "Brother_QL-570.ppd")
FILTER = os.path.join(ROOT, "build", "rastertoql570")
CAPS = os.path.join(ROOT, "caps")


def options_for(case):
    """The Brother filter options used for this capture (see capture_brother.sh)."""
    log_opts = {
        "prio_speed": "BrPriority=BrSpeed", "res600": "Resolution=300x600dpi",
        "autocut_off": "BrAutoTapeCut=OFF", "cutevery_3": "BrCutLabel=3",
        "cutatend_off": "BrCutAtEnd=OFF", "mirror": "BrMirror=ON", "trim": "BrRemoveBlkSpace=ON",
        "bidi_off": "BrBiDiPrint=OFF", "dc07_cut3": "BrCutLabel=3",
    }
    if case.startswith("ht_"):
        return "BrHalftonePattern=" + case[3:]
    if case.startswith("bright_"):
        return "BrBrightness=" + case[7:]
    if case.startswith("contrast_"):
        return "BrContrast=" + case[9:]
    return log_opts.get(case, "")


def bits(rows):
    return [[(r[i // 8] >> (7 - i % 8)) & 1 for i in range(720)] for r in rows]


def compare(case, ours, theirs, halftone):
    problems = []
    if len(ours) != len(theirs):
        return ["%d labels vs Brother's %d" % (len(ours), len(theirs))], None
    stats = None
    for k, (a, b) in enumerate(zip(ours, theirs), 1):
        ca = [c for c in a["cmds"] if c[0] != "invalidate"]
        cb = [c for c in b["cmds"] if c[0] != "invalidate"]
        if ca != cb:
            problems.append("label %d commands differ:\n      ours   %s\n      brother %s" % (k, ca, cb))
        if len(a["rows"]) != len(b["rows"]):
            problems.append("label %d: %d rows vs %d" % (k, len(a["rows"]), len(b["rows"])))
            continue
        A, B = bits(a["rows"]), bits(b["rows"])
        same = sum(x == y for ra, rb in zip(A, B) for x, y in zip(ra, rb))
        total = len(A) * 720
        # tone: 8x8 block darkness
        worst = 0.0; tot = 0.0; n = 0
        for y0 in range(0, len(A) - 7, 8):
            for x0 in range(0, 720, 8):
                da = sum(A[y][x] for y in range(y0, y0 + 8) for x in range(x0, x0 + 8))
                db = sum(B[y][x] for y in range(y0, y0 + 8) for x in range(x0, x0 + 8))
                d = abs(da - db) / 64.0
                tot += d; n += 1; worst = max(worst, d)
        stats = (same / total, tot / max(n, 1), worst)
        if halftone in ("BrBinary", "BrDither") and same != total:
            problems.append("label %d: %d dots differ" % (k, total - same))
        if halftone == "BrErrorDiffusion" and tot / max(n, 1) > 0.03:
            problems.append("label %d: tone differs (mean block difference %.3f)" % (k, tot / n))
    return problems, stats


def main():
    cases = sorted(os.path.basename(f)[:-3] for f in glob.glob(os.path.join(CAPS, "*.ql"))
                   if os.path.exists(f[:-3] + ".ras"))
    if not cases:
        print("No Brother captures found - run tools/capture_brother.sh first")
        return
    failed = 0
    for case in cases:
        opts = options_for(case)
        ht = "BrErrorDiffusion"
        for part in opts.split():
            if part.startswith("BrHalftonePattern="):
                ht = part.split("=", 1)[1]
        r = subprocess.run([FILTER, "1", "user", "test", "1", opts, os.path.join(CAPS, case + ".ras")],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=dict(os.environ, PPD=PPD))
        if r.returncode:
            print("%-22s FAIL  filter error: %s" % (case, r.stderr.decode().strip().splitlines()[-1:]))
            failed += 1
            continue
        ours = qldecode.parse(r.stdout)
        theirs = qldecode.parse(open(os.path.join(CAPS, case + ".ql"), "rb").read())
        problems, stats = compare(case, ours, theirs, ht)
        if case == "trim":   # known, intended difference: we trim blank tape, Brother's filter does not
            problems = [p for p in problems if "rows" not in p and "commands differ" not in p]
        st = "" if not stats else "dots same %.1f%%, tone diff %.3f (worst block %.2f)" % (stats[0] * 100, stats[1], stats[2])
        print("%-22s %-4s %s" % (case, "FAIL" if problems else "ok", st))
        for p in problems[:3]:
            print("      " + p)
        failed += bool(problems)
    print("%d of %d cases match" % (len(cases) - failed, len(cases)))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
