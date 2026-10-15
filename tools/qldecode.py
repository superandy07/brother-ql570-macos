#!/usr/bin/env python3
"""
qldecode.py - decode a Brother QL raster stream (QL-500..1060N command set).

usage: qldecode.py FILE [-p out.pbm]   prints the command list (raster rows
                                       summarised); -p writes each page as a
                                       PBM (page N -> out-N.pbm)

As a module: parse(bytes) -> list of pages, each a dict with 'cmds' (list of
(name, hex-args) tuples) and 'rows' (list of 90-byte row bitmaps).
"""
import sys


def unpackbits(data, n):
    out = bytearray(); i = 0
    while i < len(data):
        c = data[i]; i += 1
        if c > 127:
            out += bytes([data[i]]) * (257 - c); i += 1
        else:
            out += data[i:i + c + 1]; i += c + 1
    if len(out) != n:
        raise ValueError("packbits row decoded to %d bytes, expected %d" % (len(out), n))
    return bytes(out)


def parse(b):
    pages, cur = [], {"cmds": [], "rows": []}
    compress = 0
    i, n = 0, len(b)
    nul = 0
    while i < n and b[i] == 0:
        i += 1; nul += 1
    if nul:
        cur["cmds"].append(("invalidate", "%d x 00" % nul))
    while i < n:
        c = b[i]
        if c == 0x1b:
            k = b[i + 1]
            if k == 0x40:
                cur["cmds"].append(("ESC @", "")); i += 2
            elif k == 0x69:
                s = b[i + 2]
                ln = {0x61: 1, 0x7a: 10, 0x4d: 1, 0x41: 1, 0x4b: 1, 0x64: 2, 0x53: 0, 0x55: 15}.get(s)
                if ln is None:
                    raise ValueError("unknown ESC i %c at %d" % (s, i))
                cur["cmds"].append(("ESC i " + chr(s), b[i + 3:i + 3 + ln].hex(" ")))
                i += 3 + ln
            else:
                raise ValueError("unknown ESC %02x at %d" % (k, i))
        elif c == 0x4d:  # M n  compression mode
            compress = b[i + 1]
            cur["cmds"].append(("M", "%02x" % compress)); i += 2
        elif c in (0x67, 0x47):  # g/G 00 n data  (raster line)
            ln = b[i + 2] if c == 0x67 else b[i + 1] | b[i + 2] << 8
            data = b[i + 3:i + 3 + ln]
            row = unpackbits(data, 90) if compress == 2 else data
            cur["rows"].append(row)
            i += 3 + ln
        elif c == 0x5a:  # Z  zero raster line
            cur["rows"].append(bytes(90)); i += 1
        elif c in (0x0c, 0x1a):
            cur["cmds"].append(("print" if c == 0x0c else "print+feed", "")); i += 1
            pages.append(cur)
            cur = {"cmds": [], "rows": []}
        elif c == 0:
            i += 1
        else:
            raise ValueError("unknown byte %02x at %d" % (c, i))
    if cur["cmds"] or cur["rows"]:
        pages.append(cur)
    return pages


def summary(pages):
    out = []
    for p, pg in enumerate(pages, 1):
        out.append("page %d: %d raster rows" % (p, len(pg["rows"])))
        for name, args in pg["cmds"]:
            out.append("  %-12s %s" % (name, args))
    return "\n".join(out)


def write_pbm(rows, path, mirror=True):
    # rows are 720 dots, sent right-to-left: flip each row so the image reads normally
    with open(path, "wb") as f:
        f.write(b"P4\n720 %d\n" % len(rows))
        for r in rows:
            if mirror:
                v = int.from_bytes(r, "big")
                r = int("{:0720b}".format(v)[::-1], 2).to_bytes(90, "big")
            f.write(r)


if __name__ == "__main__":
    data = open(sys.argv[1], "rb").read()
    pages = parse(data)
    print(summary(pages))
    if "-p" in sys.argv:
        base = sys.argv[sys.argv.index("-p") + 1].rsplit(".", 1)[0]
        for k, pg in enumerate(pages, 1):
            write_pbm(pg["rows"], "%s-%d.pbm" % (base, k))
