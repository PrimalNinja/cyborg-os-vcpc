# cyborg-os-vcpc - todo

## done
- 2026-11-07 BDOS 131 op 41: the net coprocessor for NTC's Net class (GEN.MDL, the k-trit GRU): src/genmdl.c
  is IntModel / Net.cls in C (same Horner over the planes, arithmetic shift, Tiny tables, sat((a*b+32)>>6)),
  one model a slot, the model bytes copied out of the slot at load. Sub 0 there?, 1 load (addr, n), 2
  reset, 3 feed (the V logits written at an address, before noise), 4 free - vcpc.c's comment above
  net_call. The same bytes as the Z80: test/net_t.bas (K=8) prints net_t.txt's string in 60 s on the
  Z80 engine (UseEngine 1) and inside the boot wait on op 41 (UseEngine 2), Backend 1 / 2. Windows,
  Linux and the Pi alike (genmdl.c in the Makefile and build.bat). The Pi's cosd has the same model
  for a real CPC (cyborg-os/pi/svc/mind.js + pi/ntck). Still to do: C0 forwarding op 41 to the Pi's
  MIND for a CPC with a Pi; and a vCPC on a Pi reaching cosd's other services (VDU, AUDIO, DISK) over
  the link, so every Pi service is a vCPC service too.
- 2026-11-01 Windows keys: _getwch instead of _getch (src/compat.h, tty_read) - _getch's wide twin,
  the same control codes (Control + T is 20) and the same 0 / 224 before an extended key, but a
  typed letter comes as its Unicode and is sent on as UTF-8, as a Unix terminal sends it: typed
  Cyrillic (LANG RU) and accents (LANG ES) reach the code page conversion. A 224 with nothing
  waiting after it is a typed a-grave (U+00E0), not an extended key. Built with MinGW (clean);
  not yet run on a Windows console - Julian to try Control + T and typing after LANG RU.
- 2026-11-01 Windows keys back to _getch (Control + T then a number stopped working for Julian with
  the ReadConsoleInputW reading of 31 Oct; the Linux terminal path was checked through a pty:
  Control + T 2 / 3 switch slots). The console's output code page stays UTF-8 (Russian and Spanish
  shown); typed Cyrillic on Windows is open until it can be tried on a Windows console.
- 2026-11-01 --no-autoexec: the boot's AUTOEXEC skipped (what holding Shift does on a CPC): BOOTDONE's
  kw[9]. SlotFrame (CyborgOS) needs nothing here: a vCPC's frame is read and built by LANG... by
  SlotFrame itself (its one frame, the call byte at #EF48).
- 2026-10-31 Windows: LANG's Russian showed as box characters (the console in its OEM code page,
  437, the vCPC writing UTF-8). The console is put in UTF-8 (CP 65001, input and output) for the
  session, the old code pages back at the end; keys are read as Unicode console events
  (ReadConsoleInputW) and handed on as UTF-8, so typed Cyrillic and Spanish reach the code page
  conversion too (_getch gave the OEM code page's bytes). Arrows and Control + keys as before.
- 2026-10-31 LANG: the kernel's KOp 38 (LANG l, its first argument) sets the code page for 128-255:
  1 Russian KOI8-R, 0 English / 2 Spanish Latin-1. The terminal (and --show) shows them as UTF-8;
  typed UTF-8 (and --script's) turned into the code page (a character it hasn't got: dropped).
  con_text no longer turns 128-255 into '.'. Open: the command line drops codes over 126 (the CPC
  key codes clash - see CyborgOS's todo); Windows' console input isn't UTF-8 yet.
- 2026-10-31 BDOS 131 op 40: the maths coprocessor for NTC's Float class (its shim's jump block
  sends here when this answers): singles, half, MulDiv, whole arrays (16 / 32 bit, single or
  fixed), matrix products (the fixed one: exact 64 bit sums, rounded once). The same answers as
  the class's Z80 engine (checked: the same test program both ways, identical output); a tie's
  last bit can differ (C rounds halves to even). The Pi's pi/svc/float.js is the same for a CPC.
- 2026-10-30 Fixed: pressing Q inside a slot (e.g. to stop SLOTD) could quit the whole vCPC. A Control+T
  prefix left armed by the Windows terminal (which binds Control+T to its own tabs and delivers it
  unreliably) would turn the next Q - meant for the slot - into Control+T Q = quit. The prefix now
  expires after ~1s, and any non-hotkey key after Control+T is passed through as a normal key instead
  of being swallowed. Control+T then a digit (two separate presses) still switches slots as before.
  Note: SLOTD runs in the slot it is started in, so for a free command line alongside it, start it in
  another slot (RUN 2 SLOTD) rather than typing slotd in your only prompt.
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

- 2026-11-03 (the merge after the other session's sandbox died): no vCPC change in either of
  Julian's sets (A and B the same source; B's zip without vcpc.exe). This copy kept: the _getwch
  compat.h and vcpc.exe. tools/regress/vcpctest.sh 15/15 against the merged CyborgOS build (its
  programs with the stack after the file, NTC's change). To come (the lost session's step 5):
  vcpc.c cases 41 and 42 - pages as bank quarters - for the slot lists.
- 2026-11-03 Memory protection (Julian: optional, on by default): wb_cb refuses a slot's write to
  #0000-#0007, #EF00-#EF7F, #F300-#FFFF (the kernel's bank is not checked; #EF80-#F2FF is the first
  stack a slot is given, SP #F2FC); run() stops a slot whose PC is in #EF49-#EF7F or #F300-#FFFF;
  run_slot prints "Memory protection: wrote #xxxx at #pppp - stopped" (or "ran into #xxxx") on the
  slot's console and reports the program ended (as BDOS 0). --protect 0 turns it off. PROTTEST.CYB
  (CyborgOS src/bas/cyborgosc2/tasks, built to build/extra - no disc, no card) writes #F700;
  vcpctest.sh checks it stopped, and with --protect 0 that it carried on: 17/17. The decision
  (no protection in CyborgOS itself; the store table and op 10 reach other slots by design) is in
  CyborgOS docs/cyborgos-memory-model.md 3.4.
  files: src/vcpc.c, tools/regress/vcpctest.sh, README.md, docs/cyborgos-vcpc-todo.md
