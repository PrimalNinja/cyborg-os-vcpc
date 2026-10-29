# cyborg-os-vcpc - CyborgOS, The Headless Horseman

A vCPC: CyborgOS with no CPC under it. A node of its own, in a container, on a CPC-CPLink's Pi or
on any PC, that behaves as a CyborgOS machine - a CPC, a Next or an MSX - with no screen or
keyboard of its own and a faster Z80.

The kernel (`KERNEL.CYB`) and the programs (`.CYB`, `.COM`) are CyborgOS's own files, unchanged:
they run here on [superzazu's Z80](https://github.com/superzazu/z80). What the CPC's C0
(`CYBORGOS.BIN`) does in Z80 and firmware is done here in C: the 4MB memory (main RAM and 64
banks of 64K), the discs (host folders, as UniDOS's devices and drive letters), the consoles (a
text page in each slot's bank), the network (the host's TCP sockets behind the M4's ops) and the
services (ports, claims, the store, the tables). It reports itself as host 4 (0 the CPC, 1 the
Next, 3 the MSX).

CyborgOS itself is its own project (cyborg-os); this one needs its built set (its
`build/next/CYBORGOS`, or the CPC's installed CYBORGOS folder).

## The project

| | |
|---|---|
| `src/vcpc.c` | the vCPC: the native C0, the discs, the consoles, the network, the services, the terminal |
| `src/compat.h` | what differs between Windows and Unix: Winsock, folders, the clock, the console |
| `src/build.bat` | the Windows build (Visual C++'s cl: Visual Studio 2022's vcvars64 when cl isn't on the path) |
| `src/z80/` | superzazu's Z80 core (MIT, its LICENSE): unchanged but for two flag accessors |
| `build/vcpc` | the built binary (Linux x86-64 here; `make` builds it for the machine it runs on) |
| `docs/cyborgos-vcpc-todo.md` | what is done and what is next |
| `tools/regress/vcpctest.sh` | the checks: PS, DIR, HELP, RAT / MODEL / RATS through ports, PAUSE / UNPAUSE, DUMP / REVIVE |

## Building

    make                   (or: cc -O2 -Isrc/z80 -o build/vcpc src/vcpc.c src/z80/z80.c)

One C file and the Z80 core, nothing else: Linux, the Pi, macOS. On Windows, `src\build.bat`
(Visual C++: `cl /O2 /MT /Iz80 vcpc.c z80\z80.c /link ws2_32.lib`), or MinGW's gcc
(`gcc -O2 -Isrc/z80 -o build/vcpc.exe src/vcpc.c src/z80/z80.c -lws2_32`). The console needs
Windows 10 or later (its ANSI codes); paths may use \ or /.

## Testing

    tools/regress/vcpctest.sh [CYBORGOS]      (default ../cyborg-os/build/next/CYBORGOS)

## Running

    build/vcpc --sys CYBORGOS --sys CYBORGOS/C --data mynode --tty
    build\vcpc.exe --sys C:\cyborg-os\build\next\CYBORGOS --sys C:\cyborg-os\build\next\CYBORGOS\C --data mynode --tty

| Option | |
|---|---|
| `--sys DIR` | CyborgOS's set (KERNEL.CYB, the commands, HELP/): device SYS, read only. Again for more folders looked through in turn (the Next's CYBORGOS and CYBORGOS/C) |
| `--data DIR` | this node's own files: device DATA (made if not there) |
| `--slots n` | slots, 1-20 (10) |
| `--tty` | the terminal is its screen and keyboard: Control + T, then 1-9 / 0 a slot to the front, T TOP, Q quit |
| `--script "a;b"` | commands typed in turn, each given `--wait` ms (1500); `^2` slot 2 to the front, `^T` TOP, `?2` slot 2's last line printed then |
| `--show 1,2` | with `--script`: the slots whose screens are printed at the end (`all`, `front`) |
| `-v` | each slot's BDOS calls on stderr |

The drives: **A:** is `DATA:` - the node's own folder, with CyborgOS's set seen through it (read
only: writing a set's file makes the node's own copy), so A: looks as the CPC's CYBORGOS folder
does. **Z:** is `SYS:`, the set. CD, CV, MKDIR, DIR's folders work as with UniDOS.

## How a slot is kept

All of a slot is in its bank, as on the CPC: while it isn't running its registers are on its own
stack with SP at #FFFE, its BDOS state at #EF48, its text page at #F700, the cursor at #FF30. A
slot is switched out only where an interrupt could be taken (interrupts on), as the CPC's
interrupt switches it: with them off, SP may be an NTC program's thread.

## Status

See docs/cyborgos-vcpc-todo.md.
