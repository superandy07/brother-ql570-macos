/*
 * rastertoql570 - native CUPS raster filter for the Brother QL-570 label
 * printer (Brother "raster" command set, QL-500..1060N reference v6.0).
 *
 * Input:  8-bit greyscale CUPS raster (K colour space, 255 = black) at
 *         300x300 or 300x600 dpi, one page per label.
 * Output: 200 NULs, ESC @, then per label ESC i a / ESC i z / cut settings,
 *         one uncompressed 90-byte "g" row per raster line, and 0x0C between
 *         labels / 0x1A after the last - the same stream Brother's own
 *         filter produces.
 *
 * Halftoning follows Brother's driver: brightness (~1.96 levels per step)
 * then contrast (x(1 + c/100) about mid-grey), then one of
 *   binary  - black from level 125,
 *   dither  - Brother's 4x4 ordered matrix,
 *   error diffusion - Brother's tone curve + Floyd-Steinberg.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * usage (CUPS filter): rastertoql570 job user title copies options [file]
 */

#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ed_tone.h"

#define HEAD_PINS 720
#define ROW_BYTES 90

/* ------------------------------------------------------------------ */
/* Media                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *prefix;		/* PageSize name, or prefix for variants */
    int die_cut;		/* 0 = continuous length tape */
    int width_mm, length_mm;	/* ESC i z n3, n4 */
    int right_pins, area_pins;	/* print-head pin layout */
    int feed;			/* ESC i d margin (dots) */
} media_t;

/* Pin layout from Brother's command reference (section 3.2.5) */
static const media_t media[] = {
    { "DC01",  1, 17, 54,   0, 165,  0 },	/* 17 x 54 mm */
    { "DC02",  1, 17, 87,   0, 165,  0 },	/* 17 x 87 mm */
    { "DC20",  1, 23, 23,  42, 236,  0 },	/* 23 x 23 mm */
    { "DC03",  1, 29, 90,   6, 306,  0 },	/* 29 x 90 mm */
    { "DC04",  1, 38, 90,  12, 413,  0 },	/* 38 x 90 mm */
    { "DC17",  1, 39, 48,   6, 425,  0 },	/* 39 x 48 mm */
    { "DC24",  1, 52, 29,   0, 578,  0 },	/* 52 x 29 mm */
    { "DC06",  1, 62, 29,  12, 696,  0 },	/* 62 x 29 mm */
    { "DC07",  1, 62, 100, 12, 696,  0 },	/* 62 x 100 mm */
    { "DC12",  1, 12, 12, 113,  94, 35 },	/* 12 mm round */
    { "DC13",  1, 24, 24,  42, 236,  0 },	/* 24 mm round */
    { "DC05",  1, 58, 58,  51, 618,  0 },	/* 58 mm round */
    { "12mm",  0, 12, 0,   29, 106,  0 },	/* continuous tapes */
    { "29mm",  0, 29, 0,    6, 306,  0 },
    { "38mm",  0, 38, 0,   12, 413,  0 },
    { "50mm",  0, 50, 0,   12, 554,  0 },
    { "54mm",  0, 54, 0,    0, 590,  0 },
    { "62mm",  0, 62, 0,   12, 696,  0 },
};

/*
 * Map a PageSize name to its media.  Brother's names: DCnn (die-cut),
 * NNmm / NNmmxK (continuous, wider pages cropped to the tape), and
 * C_<media>_NN presets ("Shipping Label" etc.).
 */
static const media_t *find_media(const char *name)
{
    char base[32];
    unsigned i;

    if (!name || !*name)
	return NULL;
    if (!strncmp(name, "C_", 2)) {		/* C_DC07_05 -> DC07, C_62mm_10 -> 62mm */
	const char *s = name + 2, *e = strchr(s, '_');
	size_t n = e ? (size_t)(e - s) : strlen(s);
	if (n >= sizeof base)
	    return NULL;
	memcpy(base, s, n);
	base[n] = 0;
    } else {
	size_t n = strcspn(name, "x");		/* 62mmx2 -> 62mm */
	if (n >= sizeof base)
	    return NULL;
	memcpy(base, name, n);
	base[n] = 0;
    }
    for (i = 0; i < sizeof media / sizeof *media; i++)
	if (!strcmp(base, media[i].prefix))
	    return &media[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Options                                                              */
/* ------------------------------------------------------------------ */

enum { HT_BINARY, HT_DITHER, HT_DIFFUSION };

typedef struct {
    const char *pagesize;	/* from options, used if the raster has none */
    int quality;		/* BrPriority: 1 = quality, 0 = speed */
    int halftone;
    int brightness, contrast;	/* -20..20 */
    int autocut, cut_every, cut_at_end;
    int mirror, trim;
} opts_t;

static const char *marked(ppd_file_t *ppd, const char *opt)
{
    ppd_choice_t *c = ppd ? ppdFindMarkedChoice(ppd, opt) : NULL;
    return c ? c->choice : NULL;
}

static int on(const char *v, int def)
{
    if (!v)
	return def;
    return !strcasecmp(v, "ON") || !strcasecmp(v, "True") || !strcmp(v, "1");
}

static void get_options(opts_t *o, int argc, char **argv)
{
    int num = 0;
    cups_option_t *options = NULL;
    ppd_file_t *ppd = NULL;
    const char *v;
    static char pagesize[64];

    if (argc > 5)
	num = cupsParseOptions(argv[5], 0, &options);
    if (getenv("PPD") && (ppd = ppdOpenFile(getenv("PPD")))) {
	ppdMarkDefaults(ppd);
	cupsMarkOptions(ppd, num, options);
    }

    pagesize[0] = 0;
    if ((v = cupsGetOption("media", num, options)) || (v = marked(ppd, "PageSize")))
	snprintf(pagesize, sizeof pagesize, "%s", v);
    o->pagesize = pagesize;

    v = marked(ppd, "BrPriority");
    o->quality = !(v && !strcmp(v, "BrSpeed"));
    v = marked(ppd, "BrHalftonePattern");
    o->halftone = !v ? HT_DIFFUSION : !strcmp(v, "BrBinary") ? HT_BINARY
		: !strcmp(v, "BrDither") ? HT_DITHER : HT_DIFFUSION;
    o->brightness = (v = marked(ppd, "BrBrightness")) ? atoi(v) : 0;
    o->contrast = (v = marked(ppd, "BrContrast")) ? atoi(v) : 0;
    if (o->brightness < -20) o->brightness = -20;
    if (o->brightness > 20) o->brightness = 20;
    if (o->contrast < -20) o->contrast = -20;
    if (o->contrast > 20) o->contrast = 20;
    o->autocut = on(marked(ppd, "BrAutoTapeCut"), 1);
    o->cut_every = (v = marked(ppd, "BrCutLabel")) ? atoi(v) : 1;
    if (o->cut_every < 1) o->cut_every = 1;
    if (o->cut_every > 99) o->cut_every = 99;
    o->cut_at_end = on(marked(ppd, "BrCutAtEnd"), 1);
    o->mirror = on(marked(ppd, "BrMirror"), 0);
    o->trim = on(marked(ppd, "BrRemoveBlkSpace"), 0);

    cupsFreeOptions(num, options);
    if (ppd)
	ppdClose(ppd);
}

/* ------------------------------------------------------------------ */
/* Halftoning                                                           */
/* ------------------------------------------------------------------ */

/* Brother's ordered dither: black when level >= threshold [y%4][x%4] */
static const unsigned char dither4[4][4] = {
    {   8,  32,  64,  96 },
    { 144, 208, 192, 128 },
    { 176, 240, 224, 160 },
    {  16,  48,  80, 112 },
};

/* brightness then contrast, as Brother's driver applies them */
static void make_adjust(unsigned char *lut, int b, int c, int use_contrast)
{
    int shift = (b * 196) / 100, g;	/* 5 -> 9, 10 -> 19, 20 -> 39 */

    for (g = 0; g < 256; g++) {
	double v = g - shift;
	if (use_contrast)
	    v = 128 + (v - 128) * (1 + c / 100.0);
	if (v < 0) v = 0;
	if (v > 255) v = 255;
	lut[g] = (unsigned char)(v + 0.5);
    }
}

typedef struct {
    int w;			/* pixels across (print area) */
    int mode;
    unsigned char adj[256];
    int *err0, *err1;		/* Floyd-Steinberg error rows (x 16) */
} halftone_t;

static void ht_init(halftone_t *h, const opts_t *o, int w)
{
    h->w = w;
    h->mode = o->halftone;
    make_adjust(h->adj, o->brightness, o->contrast, o->halftone != HT_BINARY);
    h->err0 = calloc((size_t)w + 2, sizeof(int));
    h->err1 = calloc((size_t)w + 2, sizeof(int));
}

/* grey row (w pixels, 255 = black) -> out[x] = 1 for a dot */
static void ht_row(halftone_t *h, const unsigned char *grey, int y, unsigned char *out)
{
    int x;

    switch (h->mode) {
    case HT_BINARY:
	for (x = 0; x < h->w; x++)
	    out[x] = grey[x] && h->adj[grey[x]] >= 125;
	break;
    case HT_DITHER:
	for (x = 0; x < h->w; x++)
	    out[x] = grey[x] && h->adj[grey[x]] >= dither4[y & 3][x & 3];
	break;
    default: {
	int *cur = h->err0 + 1, *nxt = h->err1 + 1, *t;
	memset(h->err1, 0, sizeof(int) * ((size_t)h->w + 2));
	for (x = 0; x < h->w; x++) {
	    int v = grey[x] ? ed_tone[h->adj[grey[x]]] * 16 + cur[x] : 0;
	    int e;
	    if (!grey[x]) {		/* paper white stays white, error discarded */
		out[x] = 0;
		continue;
	    }
	    out[x] = v >= 128 * 16;
	    e = v - (out[x] ? 255 * 16 : 0);
	    cur[x + 1] += e * 7 / 16;
	    nxt[x - 1] += e * 3 / 16;
	    nxt[x] += e * 5 / 16;
	    nxt[x + 1] += e / 16;
	}
	t = h->err0; h->err0 = h->err1; h->err1 = t;
	break;
    }
    }
}

/* ------------------------------------------------------------------ */
/* Output                                                               */
/* ------------------------------------------------------------------ */

static FILE *outfp;		/* where put() writes: stdout or a label buffer */

static void put(const void *p, size_t n)
{
    fwrite(p, 1, n, outfp);
}

static void page_header(const media_t *m, const opts_t *o, int rows, int first, int hires)
{
    unsigned char z[13] = { 0x1b, 'i', 'z', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    unsigned char k = (o->cut_at_end ? 0x08 : 0) | (hires ? 0x40 : 0);

    put("\x1b" "ia\x01", 4);				/* raster mode */
    z[3] = 0x8e | (o->quality ? 0x40 : 0);		/* recover, kind/width/length valid */
    z[4] = m->die_cut ? 0x0b : 0x0a;
    z[5] = (unsigned char)m->width_mm;
    z[6] = (unsigned char)m->length_mm;
    z[7] = rows & 0xff;
    z[8] = (rows >> 8) & 0xff;
    z[9] = (rows >> 16) & 0xff;
    z[10] = (rows >> 24) & 0xff;
    z[11] = first ? 0 : 1;
    put(z, sizeof z);
    if (o->autocut) {
	unsigned char a[4] = { 0x1b, 'i', 'A', (unsigned char)o->cut_every };
	put("\x1b" "iM\x40", 4);
	put(a, 4);
    } else
	put("\x1b" "iM\x00", 4);
    {
	unsigned char kk[4] = { 0x1b, 'i', 'K', k };
	unsigned char d[5] = { 0x1b, 'i', 'd', (unsigned char)(m->feed & 0xff), (unsigned char)(m->feed >> 8) };
	put(kk, 4);
	put(d, 5);
    }
    put("M\x00", 2);					/* no compression */
}

/* number of tape strips for Brother's "NNmmxK" page sizes (wide pages) */
static int strip_count(const char *name)
{
    const char *x;

    if (!name || !strncmp(name, "C_", 2) || !(x = strchr(name, 'x')))
	return 1;
    return atoi(x + 1) > 1 ? atoi(x + 1) : 1;
}

static unsigned char *prev;	/* last label built, sent once we know if more follow */
static size_t prev_len;
static int labels;

/* Halftone one strip of the page (raster columns x0 .. x0+w-1) into a label. */
static void emit_label(const media_t *m, const opts_t *o, const cups_page_header2_t *hdr,
		       const unsigned char *pagebuf, int x0, int w)
{
    int H = (int)hdr->cupsHeight, y, x, rows, last_ink = -1;
    int invert = hdr->cupsColorSpace == CUPS_CSPACE_W || hdr->cupsColorSpace == CUPS_CSPACE_SW;
    unsigned char *grey = calloc((size_t)m->area_pins, 1);
    unsigned char *dots = calloc((size_t)m->area_pins, 1);
    unsigned char *out = calloc((size_t)H, ROW_BYTES);
    halftone_t ht;

    ht_init(&ht, o, m->area_pins);
    for (y = 0; y < H; y++) {
	const unsigned char *line = pagebuf + (size_t)y * hdr->cupsBytesPerLine + x0;
	unsigned char *row = out + (size_t)y * ROW_BYTES;
	for (x = 0; x < m->area_pins; x++) {
	    int sx = o->mirror ? w - 1 - x : x;	/* K colour space: 0 = white */
	    grey[x] = x < w ? (unsigned char)(invert ? 255 - line[sx] : line[sx]) : 0;
	}
	ht_row(&ht, grey, y, dots);
	for (x = 0; x < m->area_pins; x++)
	    if (dots[x]) {
		int pin = m->right_pins + (m->area_pins - 1 - x);
		row[pin / 8] |= (unsigned char)(0x80 >> (pin % 8));
		last_ink = y;
	    }
    }

    rows = H;
    if (o->trim && !m->die_cut) {	/* trim blank tape after the data (min 12.7 mm) */
	int min_rows = (int)(12.7 / 25.4 * hdr->HWResolution[1] + 0.5);
	rows = last_ink + 1 > min_rows ? last_ink + 1 : min_rows;
	if (rows > H)
	    rows = H;
    }

    if (prev) {			/* the previous label was not the last one */
	put(prev, prev_len);
	fputc(0x0c, stdout);
	free(prev);
    }
    {
	char *buf = NULL;
	size_t sz = 0;
	FILE *mem = open_memstream(&buf, &sz);
	outfp = mem;
	page_header(m, o, rows, labels == 0, hdr->HWResolution[1] == 600);
	for (y = 0; y < rows; y++) {
	    unsigned char g[3] = { 'g', 0, ROW_BYTES };
	    put(g, 3);
	    put(out + (size_t)y * ROW_BYTES, ROW_BYTES);
	}
	fclose(mem);
	outfp = stdout;
	prev = (unsigned char *)buf;
	prev_len = sz;
    }
    labels++;
    fprintf(stderr, "PAGE: %d 1\n", labels);
    free(grey);
    free(dots);
    free(out);
    free(ht.err0);
    free(ht.err1);
}

int main(int argc, char **argv)
{
    opts_t o;
    cups_raster_t *ras;
    cups_page_header2_t hdr;
    int fd = 0, page = 0;

    if (argc < 6 || argc > 7) {
	fputs("Usage: rastertoql570 job user title copies options [file]\n", stderr);
	return 1;
    }
    if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) {
	perror("ERROR: Unable to open raster file");
	return 1;
    }
    outfp = stdout;
    get_options(&o, argc, argv);
    ras = cupsRasterOpen(fd, CUPS_RASTER_READ);

    while (cupsRasterReadHeader2(ras, &hdr)) {
	const char *name = hdr.cupsPageSizeName[0] ? hdr.cupsPageSizeName : o.pagesize;
	const media_t *m = find_media(name);
	int W = (int)hdr.cupsWidth, H = (int)hdr.cupsHeight, y, k, n;
	unsigned char *pagebuf;

	if (!m) {
	    name = o.pagesize;
	    m = find_media(name);
	}
	if (!m) {
	    fprintf(stderr, "ERROR: Unknown label size \"%s\"\n", name);
	    return 1;
	}
	if (hdr.cupsBitsPerColor != 8 || hdr.cupsBitsPerPixel != 8) {
	    fprintf(stderr, "ERROR: Unsupported raster (%u bits per pixel)\n", hdr.cupsBitsPerPixel);
	    return 1;
	}
	page++;
	fprintf(stderr, "INFO: Printing page %d\n", page);

	if (!(pagebuf = calloc((size_t)hdr.cupsBytesPerLine, (size_t)H + 1))) {
	    fputs("ERROR: Out of memory\n", stderr);
	    return 1;
	}
	for (y = 0; y < H; y++)
	    if (cupsRasterReadPixels(ras, pagebuf + (size_t)y * hdr.cupsBytesPerLine,
				     hdr.cupsBytesPerLine) < 1)
		break;

	if (labels == 0) {
	    unsigned char nul[200] = { 0 };
	    put(nul, sizeof nul);
	    put("\x1b@", 2);
	}

	/* wide "xK" pages print as K strips, right-hand strip first (as Brother does) */
	n = m->die_cut ? 1 : strip_count(name);
	for (k = 0; k < n; k++) {
	    int end = W - m->area_pins * k;
	    int x0 = end - m->area_pins;
	    if (n == 1) {
		x0 = 0;
		end = W < m->area_pins ? W : m->area_pins;
	    }
	    if (x0 < 0)
		x0 = 0;
	    if (end <= x0)
		break;
	    emit_label(m, &o, &hdr, pagebuf, x0, end - x0);
	}
	free(pagebuf);
    }

    if (prev) {
	put(prev, prev_len);
	fputc(0x1a, stdout);		/* print, then feed and cut */
	free(prev);
    } else if (!page) {
	fputs("ERROR: No pages found\n", stderr);
	return 1;
    }
    cupsRasterClose(ras);
    if (fd)
	close(fd);
    fflush(stdout);
    return 0;
}
