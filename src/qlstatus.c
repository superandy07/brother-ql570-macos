/*
 * qlstatus - send a status request to a Brother QL label printer over USB
 * and print the decoded reply.  Used as a keep-alive: the request is the
 * documented "invalidate + initialize + status information request"
 * sequence, which prints nothing and changes no settings.
 *
 * usage: qlstatus [-q] [-p product-id] [-x hexbytes]
 *        (default product 0x2028 = QL-570; -x sends exactly the given bytes,
 *        no preamble, and prints any reply in hex)
 * exit:  0 ok, 2 printer not connected (off), 3 busy (a print job has it),
 *        1 other error
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BROTHER_VID 0x04f9

static int quiet;
static const char *rawhex;	/* -x: send exactly these bytes, dump the reply */

static void add_int(CFMutableDictionaryRef d, const char *key, int v)
{
    CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingASCII);
    CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &v);
    CFDictionarySetValue(d, k, n);
    CFRelease(k);
    CFRelease(n);
}

/* Find the printer-class interface of the given Brother device. */
static io_service_t find_interface(int pid)
{
    CFMutableDictionaryRef match = IOServiceMatching("IOUSBHostInterface");
    CFMutableDictionaryRef props = CFDictionaryCreateMutable(NULL, 0,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    io_service_t svc;

    add_int(props, "idVendor", BROTHER_VID);
    add_int(props, "idProduct", pid);
    add_int(props, "bInterfaceClass", 7);	/* printer */
    CFDictionarySetValue(match, CFSTR(kIOPropertyMatchKey), props);
    CFRelease(props);
    svc = IOServiceGetMatchingService(kIOMainPortDefault, match);	/* consumes match */
    return svc;
}

static void describe(const unsigned char *s)
{
    static const char *errs1[8] = {
	"no media", "end of media", "cutter jam", "weak batteries",
	"printer in use", "printer turned off", "high-voltage adapter", "fan motor error"
    };
    static const char *errs2[8] = {
	"replace media", "expansion buffer full", "communication error",
	"communication buffer full", "cover open", "cancel key", "media cannot be fed",
	"system error"
    };
    static const char *types[] = { "reply to status request", "printing completed",
	"error occurred", "", "", "turned off", "notification", "phase change" };
    int i, any = 0;

    printf("model byte '%c', media %u mm, media type 0x%02x (%s), length %u mm\n",
	   s[4], s[10], s[11],
	   s[11] == 0x0a ? "continuous" : s[11] == 0x0b ? "die-cut labels" : "none/unknown",
	   s[17]);
    printf("status type: %s, phase: %s\n",
	   s[18] < 8 ? types[s[18]] : "?", s[19] == 0 ? "waiting to receive" : "printing");
    for (i = 0; i < 8; i++)
	if (s[8] & (1 << i)) {
	    printf("error: %s\n", errs1[i]);
	    any = 1;
	}
    for (i = 0; i < 8; i++)
	if (s[9] & (1 << i)) {
	    printf("error: %s\n", errs2[i]);
	    any = 1;
	}
    if (!any)
	printf("no errors\n");
}

int main(int argc, char **argv)
{
    int pid = 0x2028, c, rc = 1;
    io_service_t svc;
    IOCFPlugInInterface **plug = NULL;
    IOUSBInterfaceInterface300 **intf = NULL;
    SInt32 score;
    UInt8 nep, pipe, out = 0, in = 0;
    IOReturn kr;
    unsigned char req[205], st[64];
    UInt32 got = 0;
    int tries;

    while ((c = getopt(argc, argv, "qp:x:")) != -1)
	switch (c) {
	case 'q': quiet = 1; break;
	case 'x': rawhex = optarg; break;
	case 'p': pid = (int)strtol(optarg, NULL, 0); break;
	default:
	    fprintf(stderr, "usage: qlstatus [-q] [-p product-id]\n");
	    return 1;
	}

    if (!(svc = find_interface(pid))) {
	if (!quiet)
	    printf("printer not connected (or switched off)\n");
	return 2;
    }
    kr = IOCreatePlugInInterfaceForService(svc, kIOUSBInterfaceUserClientTypeID,
					   kIOCFPlugInInterfaceID, &plug, &score);
    IOObjectRelease(svc);
    if (kr || !plug) {
	fprintf(stderr, "qlstatus: cannot access USB interface (0x%x)\n", kr);
	return 1;
    }
    (*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300),
			    (LPVOID *)&intf);
    (*plug)->Release(plug);
    if (!intf) {
	fprintf(stderr, "qlstatus: no USB interface object\n");
	return 1;
    }
    if ((kr = (*intf)->USBInterfaceOpen(intf)) != kIOReturnSuccess) {
	(*intf)->Release(intf);
	if (kr == kIOReturnExclusiveAccess) {
	    if (!quiet)
		printf("printer busy (in use by a print job)\n");
	    return 3;
	}
	fprintf(stderr, "qlstatus: cannot open interface (0x%x)\n", kr);
	return 1;
    }

    (*intf)->GetNumEndpoints(intf, &nep);
    for (pipe = 1; pipe <= nep; pipe++) {
	UInt8 dir, num, type;
	UInt16 maxp;
	UInt8 interval;
	if ((*intf)->GetPipeProperties(intf, pipe, &dir, &num, &type, &maxp, &interval))
	    continue;
	if (type == kUSBBulk && dir == kUSBOut && !out)
	    out = pipe;
	if (type == kUSBBulk && dir == kUSBIn && !in)
	    in = pipe;
    }
    if (!out || !in) {
	fprintf(stderr, "qlstatus: bulk endpoints not found\n");
	goto done;
    }

    if (rawhex) {
	unsigned char raw[256];
	size_t n = 0;
	const char *h = rawhex;
	while (*h && n < sizeof raw) {
	    unsigned v;
	    while (*h == ' ' || *h == ':') h++;
	    if (!*h || sscanf(h, "%2x", &v) != 1) break;
	    raw[n++] = (unsigned char)v;
	    h += 2;
	}
	printf("sent %zu bytes:", n);
	for (size_t i = 0; i < n; i++) printf(" %02x", raw[i]);
	printf("\n");
	if ((kr = (*intf)->WritePipeTO(intf, out, raw, (UInt32)n, 2000, 5000))) {
	    fprintf(stderr, "qlstatus: write failed (0x%x)\n", kr);
	    goto done;
	}
	for (tries = 0; tries < 20 && got < sizeof st; tries++) {
	    UInt32 m = sizeof st - got;
	    kr = (*intf)->ReadPipeTO(intf, in, st + got, &m, 500, 1000);
	    if (kr == kIOReturnSuccess) got += m;
	    else if (kr == kIOUSBPipeStalled) (*intf)->ClearPipeStallBothEnds(intf, in);
	    usleep(100000);
	}
	printf("reply %u bytes:", (unsigned)got);
	for (UInt32 i = 0; i < got; i++) printf("%s%02x", i % 16 ? " " : "\n  ", st[i]);
	printf("\n");
	rc = got ? 0 : 4;
	goto done;
    }

    /* 200 x NUL (invalidate), ESC @ (initialize), ESC i S (status request) */
    memset(req, 0, 200);
    memcpy(req + 200, "\x1b\x40\x1b\x69\x53", 5);
    if ((kr = (*intf)->WritePipeTO(intf, out, req, sizeof req, 2000, 5000))) {
	fprintf(stderr, "qlstatus: write failed (0x%x)\n", kr);
	goto done;
    }
    for (tries = 0; tries < 10 && got < 32; tries++) {
	UInt32 n = sizeof st - got;
	kr = (*intf)->ReadPipeTO(intf, in, st + got, &n, 500, 1000);
	if (kr == kIOReturnSuccess)
	    got += n;
	else if (kr == kIOUSBPipeStalled)
	    (*intf)->ClearPipeStallBothEnds(intf, in);
	if (got < 32)
	    usleep(100000);
    }
    if (got < 32 || st[0] != 0x80 || st[1] != 0x20) {
	fprintf(stderr, "qlstatus: no valid status reply (%u bytes)\n", (unsigned)got);
	goto done;
    }
    if (!quiet) {
	time_t now = time(NULL);
	char ts[32];
	strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&now));
	printf("%s QL status OK\n", ts);
	describe(st);
    }
    rc = 0;

done:
    (*intf)->USBInterfaceClose(intf);
    (*intf)->Release(intf);
    return rc;
}
