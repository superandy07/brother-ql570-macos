# Brother QL-570 native driver for macOS

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-deprecated-declarations
ARCHS   ?= -arch arm64 -arch x86_64
MINOS   ?= -mmacosx-version-min=12.0

all: build/rastertoql570 build/qlstatus ppd/Brother_QL-570.ppd

build/rastertoql570: src/rastertoql570.c src/ed_tone.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(ARCHS) $(MINOS) -o $@ src/rastertoql570.c -lcups

# status / setting tool, also used by the keep-alive
build/qlstatus: src/qlstatus.c
	@mkdir -p build
	$(CC) -O2 -Wall $(ARCHS) $(MINOS) -o $@ src/qlstatus.c -framework IOKit -framework CoreFoundation

ppd/Brother_QL-570.ppd: tools/genppd.py data/sizes.json
	python3 -I tools/genppd.py

test: all
	./test/run_tests.sh

clean:
	rm -rf build

.PHONY: all test clean
