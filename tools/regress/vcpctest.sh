#!/bin/sh
# vcpctest.sh - the vCPC's checks against a CyborgOS build: tools/regress/vcpctest.sh [CYBORGOS]
#   CYBORGOS: cyborg-os's build/next/CYBORGOS (default: ../cyborg-os/build/next/CYBORGOS)
# Each: commands typed into slot 1, then what must be on a screen (?n: slot n's last line then)
cd "$(dirname "$0")/../.." || exit 1
S="${1:-../cyborg-os/build/next/CYBORGOS}"
[ -x build/vcpc ] || make >/dev/null || exit 1
[ -f "$S/KERNEL.CYB" ] || { echo "no KERNEL.CYB in $S"; exit 1; }
D=$(mktemp -d); fail=0; pass=0
run() { build/vcpc --sys "$S" --sys "$S/C" --data "$D/$1" --slots 4 --wait 1200 --script "$2" --show 1; }
check() { if echo "$3" | grep -q -- "$2"; then pass=$((pass+1)); echo "ok   $1"; else fail=$((fail+1)); echo "FAIL $1 (no \"$2\")"; echo "$3" | sed 's/^/     /'; fi; }
o=$(run ps "ps"); check ps "2   -" "$o"
o=$(run dir "dir z:"); check dir "KERNEL.CYB" "$o"
o=$(run help "help pause"); check help "UNPAUSE slot" "$o"
o=$(run ipconfig "ipconfig"); check ipconfig "^IP address   [1-9]" "$o"   # (the host's own address, not 127.0.0.1 or 0.0.0.0)
o=$(build/vcpc --sys "$S" --sys "$S/C" --data "$D/gui" --slots 4 --wait 2500 --script "run 2 gui;run 3 guidemo" --show 3); check gui "GUIDEMO: window 0 on GUI" "$o"   # (GUI headless, GUIDEMO's window through its port)
o=$(run mem "run 2 ticker;mem"); check mem "^BBBBBBBBBBBB---" "$o"   # (the kernel's bank and two slots': B, the rest free)
o=$(build/vcpc --sys "$S" --sys "$S/C" --data "$D/speed" --slots 4 --wait 2500 --script "speed 1" --show 1); check speed "times a 4 MHz CPC" "$o"
o=$(run rats "run 2 model ratmodel;run 3 rat;rats 3"); check rats "^1: [a-z]" "$o"   # (RATS shows a decision: the rat's choice from MODEL, through the ports)
o=$(run pause "run 2 ticker;pause 2;?2;ps;?2;unpause 2;?2")
a=$(echo "$o" | grep -- "--- slot 2" | sed -n 1p); b=$(echo "$o" | grep -- "--- slot 2" | sed -n 2p); c=$(echo "$o" | grep -- "--- slot 2" | sed -n 3p)
if [ "$a" = "$b" ] && [ "$b" != "$c" ]; then pass=$((pass+1)); echo "ok   pause (held at ${a#*: }, on to ${c#*: })"; else fail=$((fail+1)); echo "FAIL pause: $a / $b / $c"; fi
o=$(run dump "run 2 ticker;dump 2 t.dmp;revive 3 t.dmp;?3;ps;?3;unpause 3;?3")
a=$(echo "$o" | grep -- "--- slot 3" | sed -n 1p); b=$(echo "$o" | grep -- "--- slot 3" | sed -n 2p); c=$(echo "$o" | grep -- "--- slot 3" | sed -n 3p)
if [ "$a" = "$b" ] && [ "$b" != "$c" ] && echo "$a" | grep -q tick; then pass=$((pass+1)); echo "ok   dump (revived at ${a#*: }, on to ${c#*: })"; else fail=$((fail+1)); echo "FAIL dump: $a / $b / $c"; fi
rm -rf "$D"
[ $fail -eq 0 ] && echo "$pass passed - ALL PASS" || echo "$fail failed, $pass passed"
[ $fail -eq 0 ]
