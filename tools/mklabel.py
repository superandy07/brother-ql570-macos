#!/usr/bin/env python3
"""mklabel.py OUT.pdf W H : a one-page test label (points): text, lines, a grey ramp and a radial-ish gradient."""
import sys
out, W, H = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
ops = ["BT /F1 14 Tf 8 %.1f Td (QL-570 test label) Tj ET" % (H - 20),
       "BT /F1 7 Tf 8 %.1f Td (The quick brown fox 0123456789) Tj ET" % (H - 32),
       "0 g 1 w 4 4 m %.1f 4 l S" % (W - 4),
       "0.5 w 4 %.1f m %.1f %.1f l S" % (H - 40, W - 4, H - 40)]
n = 16
for i in range(n):  # grey ramp, black -> white
    g = i / (n - 1)
    ops.append("%.3f g %.2f %.1f %.2f 14 re f" % (g, 6 + i * (W - 12) / n, H - 60, (W - 12) / n))
for i in range(40):  # smooth gradient band (exercise halftoning)
    g = i / 39
    ops.append("%.3f g %.2f %.1f %.2f 20 re f" % (g, 6 + i * (W - 12) / 40, H - 85, (W - 12) / 40 + 0.1))
ops.append("0 g 6 %.1f 20 20 re f" % (H - 115))
ops.append("0 g 3 w 40 %.1f 16 0 360 arc S" % (H - 105) if False else "")
content = "\n".join(o for o in ops if o).encode()
objs = ["<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [4 0 R] /Count 1 >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.2f %.2f] /Contents 5 0 R /Resources << /Font << /F1 3 0 R >> >> >>" % (W, H),
        "<< /Length %d >>\nstream\n%s\nendstream" % (len(content), content.decode())]
b = bytearray(b"%PDF-1.4\n"); offs = []
for i, o in enumerate(objs):
    offs.append(len(b)); b += ("%d 0 obj\n%s\nendobj\n" % (i + 1, o)).encode()
x = len(b)
b += ("xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)).encode()
for o in offs: b += ("%010d 00000 n \n" % o).encode()
b += ("trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, x)).encode()
open(out, "wb").write(b)
