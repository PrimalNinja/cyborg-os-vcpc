# cyborg-os-vcpc: make (build/vcpc); make CC=x86_64-w64-mingw32-gcc for Windows (to come)
CC ?= cc
CFLAGS ?= -O2 -Wall
build/vcpc: src/vcpc.c src/z80/z80.c src/z80/z80.h
	mkdir -p build
	$(CC) $(CFLAGS) -Isrc/z80 -o $@ src/vcpc.c src/z80/z80.c
clean:
	rm -f build/vcpc build/vcpc.exe
.PHONY: clean
