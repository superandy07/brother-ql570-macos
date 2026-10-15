/*
 * edsim - simulate candidate error-diffusion halftones on a CUPS raster
 * (8-bit, K colour space: 255 = black) and compare with the dots Brother's
 * filter produced (PBM from qldecode.py, raster x at PBM column x + off).
 *
 * usage: edsim raster.ras brother.pbm lut.bin kernel serpentine dir thr off [out.pbm]
 *   kernel: fs | jjn | stucki | sierra | sierra2 | sierra-lite | burkes | atkinson
 *   dir: 0 = scan raster left->right, 1 = right->left (head order)
 * prints the fraction of matching dots.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int dx, dy, w; } tap;
static const struct { const char *name; int div; tap t[12]; } kernels[] = {
    { "fs", 16, { {1,0,7}, {-1,1,3}, {0,1,5}, {1,1,1}, {0,0,0} } },
    { "jjn", 48, { {1,0,7},{2,0,5},{-2,1,3},{-1,1,5},{0,1,7},{1,1,5},{2,1,3},{-2,2,1},{-1,2,3},{0,2,5},{1,2,3},{2,2,1} } },
    { "stucki", 42, { {1,0,8},{2,0,4},{-2,1,2},{-1,1,4},{0,1,8},{1,1,4},{2,1,2},{-2,2,1},{-1,2,2},{0,2,4},{1,2,2},{2,2,1} } },
    { "sierra", 32, { {1,0,5},{2,0,3},{-2,1,2},{-1,1,4},{0,1,5},{1,1,4},{2,1,2},{-1,2,2},{0,2,3},{1,2,2},{0,0,0} } },
    { "sierra2", 16, { {1,0,4},{2,0,3},{-2,1,1},{-1,1,2},{0,1,3},{1,1,2},{2,1,1},{0,0,0} } },
    { "sierra-lite", 4, { {1,0,2},{-1,1,1},{0,1,1},{0,0,0} } },
    { "burkes", 32, { {1,0,8},{2,0,4},{-2,1,2},{-1,1,4},{0,1,8},{1,1,4},{2,1,2},{0,0,0} } },
    { "atkinson", 8, { {1,0,1},{2,0,1},{-1,1,1},{0,1,1},{1,1,1},{0,2,1},{0,0,0} } },
};

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char hdr[1800], lut[256], *ras, *pbm;
    int W, H, bpl, pw, ph, k, serp, dir, thr, off, x, y, i;
    long match = 0, total = 0, ink_b = 0, ink_s = 0;
    double *err;
    char line[64];
    unsigned char *outbits;

    if (argc < 9) { fprintf(stderr, "usage: see source\n"); return 1; }
    f = fopen(argv[1], "rb"); fread(hdr, 1, 1800, f);
    W = hdr[4 + 372] | hdr[4 + 373] << 8; H = hdr[4 + 376] | hdr[4 + 377] << 8;
    bpl = hdr[4 + 392] | hdr[4 + 393] << 8;
    ras = malloc((size_t)bpl * H); fread(ras, 1, (size_t)bpl * H, f); fclose(f);
    f = fopen(argv[2], "rb");
    fgets(line, sizeof line, f); fgets(line, sizeof line, f); sscanf(line, "%d %d", &pw, &ph);
    pbm = malloc((size_t)(pw / 8) * ph); fread(pbm, 1, (size_t)(pw / 8) * ph, f); fclose(f);
    f = fopen(argv[3], "rb"); fread(lut, 1, 256, f); fclose(f);
    for (k = 0; k < 8 && strcmp(kernels[k].name, argv[4]); k++) ;
    if (k == 8) { fprintf(stderr, "unknown kernel\n"); return 1; }
    serp = atoi(argv[5]); dir = atoi(argv[6]); thr = atoi(argv[7]); off = atoi(argv[8]);
    err = calloc((size_t)(W + 8) * 3, sizeof *err);
    outbits = calloc((size_t)W * H, 1);

    for (y = 0; y < H && y < ph; y++) {
	int ltr = dir == 0, step;
	double *r0 = err + ((y) % 3) * (W + 8) + 4;
	if (serp && (y & 1)) ltr = !ltr;
	step = ltr ? 1 : -1;
	for (i = 0; i < W; i++) {
	    int xx = ltr ? i : W - 1 - i, b, s, t;
	    double v = lut[ras[(size_t)y * bpl + xx]] + r0[xx], e;
	    s = v >= thr;
	    e = v - (s ? 255 : 0);
	    for (t = 0; t < 12 && kernels[k].t[t].w; t++) {
		int nx = xx + kernels[k].t[t].dx * step, ny = kernels[k].t[t].dy;
		if (nx < 0 || nx >= W) continue;
		err[((y + ny) % 3) * (W + 8) + 4 + nx] += e * kernels[k].t[t].w / kernels[k].div;
	    }
	    r0[xx] = 0;
	    b = pbm[(size_t)y * (pw / 8) + (xx + off) / 8] >> (7 - (xx + off) % 8) & 1;
	    outbits[(size_t)y * W + xx] = (unsigned char)s;
	    match += b == s; total++; ink_b += b; ink_s += s;
	}
    }
    printf("%-11s serp=%d dir=%d thr=%d  match %.5f  (ink brother %ld, sim %ld)\n",
	   argv[4], serp, dir, thr, (double)match / total, ink_b, ink_s);
    if (argc > 9) {
	f = fopen(argv[9], "wb");
	fprintf(f, "P5\n%d %d\n255\n", W, H);
	for (i = 0; i < W * H; i++) fputc(outbits[i] ? 0 : 255, f);
	fclose(f);
    }
    return 0;
}
