#!/bin/sh
# gate-ra-room.sh [install-dir] -- a Red Alert match started from a network ROOM: the lobby
# state machine every room screen drives (--mphost / --mpjoin, no screen drawn), in the Red
# Alert module, over loopback, through a local game list and a local relay stand-in.
#
# Hidden throughout (OPENCNC3D_BACKGROUND); nothing leaves this machine except the LAN
# advert in leg E, which is what a hosted room always sends.
#
#   A  THE ROOM: First Come First Serve (SCM10EA, temperate). The host plays the USSR with
#      shroud regrowth, 10000 credits and two seats given to the computer; the joiner asks
#      for Ukraine in orange before it readies; the computers are easy. Both must start the same match: the same
#      four seats (USSR, Ukraine, Germany, France, in their colours), the same seed (not
#      zero), the rules the host set, at least 100 world-hash reports agreeing and none
#      differing, and the Power Plant the host placed standing in the joiner's world.
#   B  THE LIST, NO RELAY: the host lists itself on a local game list by address
#      (--mppublic) on Shallow Grave (SCM08EA, snow); the joiner is told only where the list
#      is, picks the room off it and plays it.
#   C  THE RELAY: the same through a relay stand-in on loopback (the protocol the real relay
#      speaks: register by id, forward to the named id), the room listed by its code.
#   D  NOT TIBERIAN DAWN: a Tiberian Dawn copy joining a Red Alert room is refused by both
#      ends for its order wire.
#   E  THE LAN ADVERT names the room's map and carries Red Alert's order-wire layout, so a
#      browser can tell it from a Tiberian Dawn room.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
install=${1:-"$root/playable"}
install=$(CDPATH= cd -- "$install" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ra3d-room.XXXXXX")
pids=""
# stop PID...: gone, and reaped quietly
stop() { for p in "$@"; do kill -9 "$p" 2>/dev/null || true; wait "$p" 2>/dev/null || true; done; }
cleanup() {
    stop $pids
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM
cd "$install"
fail() { echo "gate-ra-room: FAIL: $1" >&2; exit 1; }
[ -f ra3d/RedAlert.dylib ] || fail "no Red Alert module is staged in $install"
[ -f ra3d/skirmish-maps.txt ] || fail "no Red Alert map list in $install/ra3d"

APP="./opencnc3d --module ra3d --no-movies --nosound"
TD="--cameos cameos.pack --dospack dossidebar.pack --dosinf dosinfantry.pack --dylib TiberianDawn.dylib --dir missions/ --content content/"
printf 'tick 5\nlclickobj MCV:@me\nactclickobj MCV:@me\ntick 250\nbuild POWR\nready POWR 1500\nstartplace POWR\nplace auto\ntick 1500\nobjdump\nquit\n' > "$work/host.txt"
printf 'tick 5\nlclickobj MCV:@me\nactclickobj MCV:@me\ntick 3000\nobjdump\nquit\n' > "$work/join.txt"

# wait_both PID PID SECONDS: both gone, or the time is up and both are killed
wait_both() {
    w=0
    while [ $w -lt "$3" ] && { kill -0 "$1" 2>/dev/null || kill -0 "$2" 2>/dev/null; }; do
        sleep 1; w=$((w + 1))
    done
    stop "$1" "$2"
}

# agree LEG: the two logs' hash reports, compared
agree() {
    grep '^NETSYNC|' "$work/$1-host.log" | sort -u > "$work/$1-h"
    grep '^NETSYNC|' "$work/$1-join.log" | sort -u > "$work/$1-j"
    n=$(comm -12 "$work/$1-h" "$work/$1-j" | grep -c . || true)
    sort -t'|' -k2,2 "$work/$1-h" > "$work/$1-hs"
    sort -t'|' -k2,2 "$work/$1-j" > "$work/$1-js"
    d=$(join -t'|' -j2 "$work/$1-hs" "$work/$1-js" | awk -F'|' '$3 != $5' | grep -c . || true)
    a=$(cat "$work/$1-host.log" "$work/$1-join.log" | grep -c '^NETDESYNC|' || true)
    [ "$d" = "0" ] || fail "$1: $d hash reports differ"
    [ "$a" = "0" ] || fail "$1: a desync alarm: $(grep -h '^NETDESYNC|' "$work/$1-host.log" "$work/$1-join.log" | head -1)"
    echo "$n"
}

# started LEG: both ends reached the match, the same seats and the same start line
started() {
    for e in host join; do
        grep -q '^MPLOBBY|started|' "$work/$1-$e.log" || fail "$1: the $e never started: $(grep -h 'MPLOBBY|FAIL' "$work/$1-$e.log" | head -1)"
        grep -q '^MPLOBBY|done|reason=0' "$work/$1-$e.log" || fail "$1: the $e did not end cleanly"
        grep '^APP|raroom|' "$work/$1-$e.log" > "$work/$1-$e.room"
        grep '^skirmish: seat=' "$work/$1-$e.log" > "$work/$1-$e.seats"
    done
    cmp -s "$work/$1-host.room" "$work/$1-join.room" || fail "$1: the two ends started different rooms: $(diff "$work/$1-host.room" "$work/$1-join.room" | head -2 | tr '\n' ' ')"
    cmp -s "$work/$1-host.seats" "$work/$1-join.seats" || fail "$1: the two engines seated different houses"
    seed=$(sed -n 's/^APP|raroom|start|.*|seed=\([0-9]*\)|.*/\1/p' "$work/$1-host.room")
    [ -n "$seed" ] && [ "$seed" != "0" ] || fail "$1: the room carried no seed"
    grep -Fq "seed $seed" "$work/$1-join.log" || fail "$1: the joiner's engine did not start on the room's seed"
}

# ---- A ------------------------------------------------------------------------------
OPENCNC3D_BACKGROUND=1 $APP --savedir "$work" --mphost 40 --mpmap SCM10EA --mpbots 2 --side soviet \
    --credits 10000 --shroudregrow --difficulty easy --script "$work/host.txt" > "$work/a-host.log" 2>&1 &
h=$!; pids="$pids $h"
sleep 3
OPENCNC3D_BACKGROUND=1 $APP --savedir "$work" --mpjoin 40 --mpmap SCM10EA --mpmyseat 4,4 \
    --script "$work/join.txt" > "$work/a-join.log" 2>&1 &
j=$!; pids="$pids $j"
wait_both $h $j 150
started a
n=$(agree a)
[ "$n" -ge 100 ] || fail "A: only $n hash reports agree"
for want in 'seat=0|person|country=USSR|colour=Red' 'seat=1|person|country=Ukraine|colour=Orange' \
            'seat=2|computer|country=Germany|colour=Grey' 'seat=3|computer|country=France|colour=Blue'; do
    grep -Fq "APP|raroom|$want" "$work/a-host.room" || fail "A: no seat $want"
done
grep -Fq '|credits=10000|ore=1|crates=0|shroud=1|ctf=0|difficulty=0|' "$work/a-host.room" || fail "A: the room's rules did not travel"
for e in host join; do
    grep -Fq 'skirmish: the computer players are easy' "$work/a-$e.log" || fail "A: the $e's engine did not make the computers easy"
done
grep -Fq 'MPLOBBY|join|seat=1|asked house=4 colour=4' "$work/a-join.log" || fail "A: the joiner did not ask for its row"
plant=$(grep '^OBJ|BUILDING|POWR|Multi1|' "$work/a-join.log" | grep -c '|limbo=0|' || true)
[ "$plant" -ge 1 ] || fail "A: the host's Power Plant is not in the joiner's world"
seed_a=$seed
echo "gate-ra-room: PASS: a Red Alert room on First Come First Serve: the USSR and a joiner who asked for Ukraine in orange, against Germany and France on the computer, easy, shroud regrowth on, seed $seed_a on both ends; $n hash reports agree and none differ, and the host's Power Plant stands in the joiner's world"

# ---- the list and the relay stand-in --------------------------------------------------
lport=8195; rport=8197
python3 "$root/tools/gamelist/gamelist.py" --host 127.0.0.1 --port $lport --ttl 120 --per-source 32 > "$work/list.log" 2>&1 &
pids="$pids $!"
python3 - $rport "$work/relay.log" > /dev/null 2>&1 <<'RELAY' &
import select, socket, struct, sys
rport, path = int(sys.argv[1]), sys.argv[2]
out = open(path, "a", buffering=1)
relay = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
relay.bind(("127.0.0.1", rport))
out.write("RELAY|up|%d\n" % rport)
at = {}
while True:
    ready, _, _ = select.select([relay], [], [], 1.0)
    for s in ready:
        data, src = s.recvfrom(4096)
        if len(data) < 8:
            continue
        snd, rcv = struct.unpack_from("<II", data, 0)
        if snd == 0 and rcv == 0:
            if len(data) == 50:
                relay.sendto(data[:12], src)
            continue
        if snd == 0 or snd == 0xFFFFFFFF or snd == rcv:
            continue
        if snd not in at:
            out.write("RELAY|register|id=%08x|port=%d\n" % (snd, src[1]))
        at[snd] = src
        if rcv == 0 or rcv not in at or at[rcv] == src:
            continue
        relay.sendto(data, at[rcv])
RELAY
pids="$pids $!"
up=0; i=0
while [ $i -lt 60 ]; do
    if grep -q '^RELAY|up|' "$work/relay.log" 2>/dev/null \
       && python3 -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:$lport/', timeout=1)" > /dev/null 2>&1; then
        up=1; break
    fi
    sleep 0.25; i=$((i + 1))
done
[ "$up" = "1" ] || fail "the local game list or the relay stand-in did not come up"

# ---- B ------------------------------------------------------------------------------
OPENCNC3D_BACKGROUND=1 $APP --savedir "$work" --mphost 40 --mpwait 90 --mpmap SCM08EA --mppublic \
    --mplist "http://127.0.0.1:$lport/" --mpname "RED ALERT DIRECT" --script "$work/host.txt" > "$work/b-host.log" 2>&1 &
h=$!; pids="$pids $h"
sleep 3
OPENCNC3D_BACKGROUND=1 $APP --savedir "$work" --mpjoin 40 --mpwait 90 --mpmap SCM08EA \
    --mplist "http://127.0.0.1:$lport/" --mpfromlist --script "$work/join.txt" > "$work/b-join.log" 2>&1 &
j=$!; pids="$pids $j"
wait_both $h $j 150
grep -Fq 'MPLOBBY|join|from-list|RED ALERT DIRECT|127.0.0.1:' "$work/b-join.log" || fail "B: the joiner did not pick the room off the list: $(grep -h 'MPLOBBY|FAIL' "$work/b-join.log" | head -1)"
started b
n=$(agree b)
[ "$n" -ge 10 ] || fail "B: only $n hash reports agree"
[ "$seed" != "$seed_a" ] || fail "B: two rooms offered the same seed"
echo "gate-ra-room: PASS: a Red Alert room listed by address on a local game list (Shallow Grave) was found by a joiner told only where the list is, and played: seed $seed, $n hash reports agree, none differ"

# ---- C ------------------------------------------------------------------------------
OPENCNC3D_BACKGROUND=1 $APP --savedir "$work" --mphost 40 --mpwait 90 --mpmap SCM10EA \
    --mprelay "127.0.0.1:$rport" --mplist "http://127.0.0.1:$lport/" --mpname "RED ALERT RELAYED" \
    --script "$work/host.txt" > "$work/c-host.log" 2>&1 &
h=$!; pids="$pids $h"
sleep 3
OPENCNC3D_BACKGROUND=1 $APP --savedir "$work" --mpjoin 40 --mpwait 90 --mpmap SCM10EA \
    --mprelay "127.0.0.1:$rport" --mplist "http://127.0.0.1:$lport/" --mpfromlist \
    --script "$work/join.txt" > "$work/c-join.log" 2>&1 &
j=$!; pids="$pids $j"
wait_both $h $j 150
code=$(sed -n 's/^MPLOBBY|host|room=\(.*\)$/\1/p' "$work/c-host.log" | head -1)
[ -n "$code" ] || fail "C: the relayed room has no code: $(grep -h 'MPLOBBY|FAIL' "$work/c-host.log" | head -1)"
grep -Fq "MPLOBBY|join|from-list|RED ALERT RELAYED|$code" "$work/c-join.log" || fail "C: the joiner did not pick the relayed room off the list"
started c
n=$(agree c)
[ "$n" -ge 10 ] || fail "C: only $n hash reports agree"
grep -q '^RELAY|register|' "$work/relay.log" || fail "C: nothing went through the relay"
echo "gate-ra-room: PASS: a Red Alert room through the relay stand-in, listed as $code, joined off the list and played: seed $seed, $n hash reports agree, none differ"

# ---- D ------------------------------------------------------------------------------
printf 'tick 30\nquit\n' > "$work/short.txt"
OPENCNC3D_BACKGROUND=1 $APP --savedir "$work" --mphost 40 --mpmap SCM01EA --script "$work/short.txt" > "$work/d-host.log" 2>&1 &
h=$!; pids="$pids $h"
sleep 3
OPENCNC3D_BACKGROUND=1 ./opencnc3d --scen SCG01EC --pack SCG01EA.pack $TD --menupack dosmenu.pack --nosound \
    --savedir "$work" --mpjoin 40 --script "$work/short.txt" > "$work/d-join.log" 2>&1 &
j=$!; pids="$pids $j"
w=0; while [ $w -lt 20 ] && kill -0 $j 2>/dev/null; do sleep 1; w=$((w + 1)); done
stop $h $j
grep -q 'order-wire layout' "$work/d-join.log" || fail "D: the Tiberian Dawn joiner was not refused for its order wire: $(grep -h 'MPLOBBY|FAIL' "$work/d-join.log" | head -1)"
grep -q '^NET|refused|.*order-wire layout' "$work/d-host.log" || fail "D: the Red Alert room did not refuse it"
! grep -q '^MPLOBBY|started|' "$work/d-join.log" || fail "D: the Tiberian Dawn joiner reached a match"
echo "gate-ra-room: PASS: a Tiberian Dawn copy joining a Red Alert room is refused by both ends for its order wire"

# ---- E ------------------------------------------------------------------------------
python3 - "$work/advert.txt" > /dev/null 2>&1 <<'ADVERT' &
import socket, struct, sys, time
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
if hasattr(socket, "SO_REUSEPORT"):
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
s.bind(("", 17420))
s.settimeout(1.0)
end = time.time() + 30
while time.time() < end:
    try:
        data, src = s.recvfrom(4096)
    except socket.timeout:
        continue
    if len(data) < 32 + 32 + 16:
        continue
    abi, scen = struct.unpack_from("<II", data, 24)
    name = data[32:64].split(b"\0")[0].decode("ascii", "replace")
    code = data[64:80].split(b"\0")[0].decode("ascii", "replace")
    if name == "RED ALERT LAN":
        open(sys.argv[1], "w").write("ADVERT|%s|%s|abi=%08X|scen=%08X\n" % (name, code, abi, scen))
        break
ADVERT
lp=$!; pids="$pids $lp"
sleep 1
OPENCNC3D_BACKGROUND=1 $APP --savedir "$work" --mphost 40 --mpwait 35 --mpmap SCM10EA --mpname "RED ALERT LAN" \
    --script "$work/short.txt" > "$work/e-host.log" 2>&1 &
h=$!; pids="$pids $h"
w=0; while [ $w -lt 30 ] && kill -0 $lp 2>/dev/null; do sleep 1; w=$((w + 1)); done
stop $h $lp
abi=$(sed -n 's/.*layout \([0-9A-F]*\),.*/\1/p' "$work/a-host.log" | head -1)
[ -s "$work/advert.txt" ] || fail "E: no LAN advert was heard from the Red Alert room"
grep -Fq "ADVERT|RED ALERT LAN|SCM10EA|abi=$abi|" "$work/advert.txt" || fail "E: the advert does not carry Red Alert's order wire $abi: $(cat "$work/advert.txt")"
echo "gate-ra-room: PASS: the Red Alert room's LAN advert names SCM10EA and carries Red Alert's order-wire layout $abi"
