# cyborg-os-vcpc: make (build/vcpc); make CC=x86_64-w64-mingw32-gcc for Windows (to come)
CC ?= cc
CFLAGS ?= -O2 -Wall
# the build number VER shows on a vCPC: docs/build-info.txt's top entry (YYYYMMDD-N) into src/cos_build.h
# (tools/stampver.js, node) - add the entry, then make. make win: build/vcpc.exe with MinGW's gcc
build/vcpc: src/vcpc.c src/genmdl.c src/genmdl.h src/tables.h src/z80/z80.c src/z80/z80.h docs/build-info.txt
	-node tools/stampver.js
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc/z80 -Isrc -o $@ src/vcpc.c src/genmdl.c src/z80/z80.c -lm
win: src/vcpc.c src/genmdl.c src/z80/z80.c docs/build-info.txt
	-node tools/stampver.js
	mkdir -p build
	x86_64-w64-mingw32-gcc -O2 -Isrc/z80 -Isrc -o build/vcpc.exe src/vcpc.c src/genmdl.c src/z80/z80.c -lws2_32 -liphlpapi
clean:
	rm -f build/vcpc build/vcpc.exe
.PHONY: clean win
