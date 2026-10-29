# cyborg-os-vcpc - todo

## done
- 2026-10-29 BDOS 131 ops 35 LISTEN (a socket listening on a port - SO_REUSEADDR, any address) and 36
  ACCEPT (a connection that came: a socket of its own, 254 none yet - a tick's sleep, as a port's not
  yet: an idle SLOTD costs next to nothing). Nothing listens unless a program asks: cyborg-os's SLOTD,
  only while it runs. With RMOVE / RCOPY: two nodes on one PC move and copy running slots and send
  programs (vcpctest: 15). -v also says when a socket was closed by the other end or broke.
- 2026-10-29 GUI runs here (cyborg-os: GUI refused any host but the CPC; its CPC head draws nothing on
  host 4 - no gate array, no #C000 screen - the desktop goes only to RGUI, R=). GUIDEMO opens its
  window on it through the port; the mouse found: KEYS (MP2, AMX, SF ports read 255 here). R= itself
  not tried here (no VDU server reachable). Reporting host 0 instead would put the kernel on its CPC
  path (KRun, the gate array): not that. vcpctest: gui (10 checks).
- 2026-10-29 --cpu P (5-95): a node keeps to P% of one core - after each stretch of work (50 ms at most)
  it sleeps as the share says, measured in CPU time (getrusage / GetProcessTimes) - and runs at below-
  normal priority (nice 10 / BELOW_NORMAL_PRIORITY_CLASS). Measured, CRUNCH and TICKER running and
  SPEED: all of a core - SPEED 62.8 times a CPC; --cpu 95: 57.5; 50: 26.8 (a third of the wall time);
  25: 14.5. For the gateway: --cpu for the whole PC, a node a core, each node its share.
- 2026-10-29 FILE 100 (the kernel opening a program) takes a name with a folder - C/PS.CYB: the kernel now
  looks in C/ on the boot drive after the slot's drive (cyborg-os: all the commands in C/). With only
  --sys CYBORGOS (no CYBORGOS/C), PAUSE, SPEED, IPCONFIG are found there.
- 2026-10-29 IPCONFIG: the host's own network - its first IPv4 interface that is up: the address, the
  netmask, the gateway, two DNS servers, the MAC, its name in the message ("vCPC: the host's eth0").
  Linux: getifaddrs, /proc/net/route, /etc/resolv.conf, /sys/class/net; Windows: GetAdaptersAddresses
  (build.bat links iphlpapi.lib). It said 127.0.0.1 before. YING's keys page and back: fine here - it
  was the Windows 7 second stop. vcpctest: ipconfig (9 checks). Scripts: #n types key n alone.
- 2026-10-29 Windows: the slots stopped after about 7 seconds (RATBOX found it: its 9 second wait for
  RAT never ended) - superzazu's cycle count is an unsigned long, 32 bits on Windows: it wrapped at
  4G cycles and a slice's end, worked out from it, then came before its start. Only differences of
  the count are taken now. Under Wine: TICKER past 2 million in 40 s, RATBOX's message after 9 s.
  Scripts: ~ms (time passing, nothing typed); -vv: the kernel's requests to C0 on stderr.
- 2026-10-29 SPEED (cyborg-os's new command) here: 478 times a 4 MHz CPC alone (about 120 million DJNZ
  a second), 69 with CRUNCH and TICKER beside it. vcpctest: speed (8 checks).
- 2026-10-29 MEM: C0's answers to the kernel are kept between its ops, each op setting only its own (as
  C0's Serve) - clearing them all lost MEMINFO's top and pages under the kernel's next op (MEM showed
  #9738-#FFFF free and no map); HOT and INKEY now give all three of theirs each time. MEMMAP in MEM's
  legend (B a slot's or the kernel's, U a store's, - free), 257 pages as the CPC's; MEM.CYB has a vCPC
  section (host 4) in cyborg-os. vcpctest: mem (7 checks).
- 2026-10-29 --tty without flicker: only the rows that changed are drawn, in place (no clearing), the
  cursor hidden while drawing, in one write; a slot to the front or a MODE draws it all again. run.bat
  (Windows: the node in this console, CyborgOS from ../cyborg-os or the folder given - not start.bat:
  START is Windows' own command).
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
- the gateway: one port (or the Pi's link), nodes as LUNs behind it; --cpu P for the whole PC - nodes =
  cores x P / 100 rounded up, each given its share (--cpu to each); started, stopped, restarted
- TOP's key: Control + ESC is Windows' Start menu (most emulators lose it too - WinAPE hooks it); a
  second TOP key in CyborgOS, and the kernel's hint for host 4 (Control + T then 1-9 / T)
- RAMDISC: its own Z80 driver run in a page, as C0 does
- the link (the Pi protocol's frames: a console channel a slot, files in and out, port calls)
- a Dockerfile, the PHP stub that starts nodes
