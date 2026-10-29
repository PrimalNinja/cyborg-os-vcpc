# cyborg-os-vcpc - todo

## done
- 2026-10-29 Windows: src/compat.h (Winsock, folders - a small dirent for cl -, the clock, the console:
  ANSI codes on, raw keys, the arrows as ESC [ A-D), src/build.bat (Visual C++, as Julian builds: cl /O2
  /MT, vcvars64 when cl isn't on the path). Built here with MinGW and run under Wine: DIR, TICKER,
  DUMP / REVIVE as on Linux. Not yet tried with cl itself, nor --tty on a real Windows console.
- 2026-10-29 C0's op 37 (SLOT: the DMA - in the bank here, VS + 5 -, a file open, the build's stamp
  #7643, g 2: no store) - CyborgOS's DUMP / REVIVE run: TICKER dumped from slot 2, revived in 3,
  stays at its count while paused and counts on after UNPAUSE. The cursor bytes moved to #FF30
  (#FF00-#FF27 are the CPC's store and scrollback words).
- 2026-10-29 The vCPC in C (vcpc.c) on superzazu's Z80 (its zexdoc / zexall all OK here): the real
  KERNEL.CYB and commands run - DIR, PS, HELP, PAUSE / UNPAUSE, RAT + MODEL + RATS through ports.
  64 banks (4MB) - slots, the kernel, stores from the free ones; a slot's whole state in its bank;
  a slot switched out only with interrupts on (switching it anywhere wrote its registers into DIR's
  thread). A: the node's folder over CyborgOS's set, Z: the set; letters as path starts ("A:HELP/").
  A first version in JavaScript (Z80.js) came before it and is gone: superzazu's core, in C, was
  the one asked for.

## next
- SEND / SLOTD: the network ops' listening (bind, listen, accept) as the M4 gets them
- RAMDISC: its own Z80 driver run in a page, as C0 does
- the link (the Pi protocol's frames: a console channel a slot, files in and out, port calls)
- a Dockerfile, the PHP stub that starts nodes
