#!/bin/bash
# Apps/ChatCube/tools/screenshot.sh
# Screenshots of ChatCube as a person sees it, with no display attached:
# a relay and a second identity ("Bob", chatcube-peer in echo mode) on this
# machine, ChatCube under Xvfb driven through XTest (tools/xinput.c) -
# sign in as Alice, add Bob, exchange two messages - and a picture of each
# stage. For checking a change to the window by eye, and for the docs.
#
#   Apps/ChatCube/tools/screenshot.sh [--dark] [--build <dir>] [--out <dir>] [--keep]
#
#   --dark        the dark look (ChatCube --dark); light otherwise
#   --build <dir> the build directory with ChatCube and chatcube-peer (build)
#   --out <dir>   where the pictures go (<build>/chatcube-screenshots/<look>)
#   --keep        leave the relay, Bob and the data folders running and in
#                 place afterwards (to drive the app further by hand)
#
# Writes signin.png, empty.png (signed in, no conversation open) and
# chat.png (the conversation with Bob). Needs: a built ChatCube and
# chatcube-peer, the relay's virtual environment
# (Server/ChatCubeRelay/.venv, see Apps/ChatCube/README.md), xvfb-run,
# ImageMagick's `import`, and a C compiler with libX11 and libXtst
# headers for the input helper (built once into <build>/chatcube-xinput).
#
# The clicks are at fixed places in the 1000x680 window ChatCube opens
# (ChatCubeWindow.cpp: Build*): a change to the sign-in card or the sidebar
# moves them, and the pictures then show where the clicks landed.
#
# Version: 0.1.0
# Last Modified: 2026-10-28
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
build="$root/build"
out=""
look=light
opt=""
keep=0
while [ $# -gt 0 ]; do
    case "$1" in
        --dark) look=dark; opt="--dark" ;;
        --build) build="$2"; shift ;;
        --out) out="$2"; shift ;;
        --keep) keep=1 ;;
        -h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "screenshot.sh: unknown option $1" >&2; exit 2 ;;
    esac
    shift
done
[ -n "$out" ] || out="$build/chatcube-screenshots/$look"

app="$build/ChatCube"
peer="$build/chatcube-peer"
relay="$root/Server/ChatCubeRelay/.venv/bin/chatcube-relay"
for f in "$app" "$peer" "$relay"; do
    [ -x "$f" ] || { echo "screenshot.sh: $f is missing (build ChatCube and chatcube-peer, make the relay's .venv)" >&2; exit 1; }
done
for t in xvfb-run import cc; do
    command -v "$t" >/dev/null || { echo "screenshot.sh: $t is not installed" >&2; exit 1; }
done

# The input helper, built once.
xinput="$build/chatcube-xinput"
if [ ! -x "$xinput" ] || [ "$root/Apps/ChatCube/tools/xinput.c" -nt "$xinput" ]; then
    cc -O1 -o "$xinput" "$root/Apps/ChatCube/tools/xinput.c" -lX11 -lXtst
fi

mkdir -p "$out"
work="$out/work"
rm -rf "$work"
mkdir -p "$work/home" "$work/bob"
port=18790
pids=()
cleanup() {
    [ "$keep" = 1 ] && { echo "screenshot.sh: left running: relay on 127.0.0.1:$port, Bob (pids ${pids[*]})"; return; }
    for p in "${pids[@]}"; do
        kill "$p" 2>/dev/null || true
        wait "$p" 2>/dev/null || true
    done
}
trap cleanup EXIT

# A relay of our own, and Bob, who answers everything.
CHATCUBE_RELAY_PORT=$port CHATCUBE_RELAY_DB="$work/relay.sqlite3" "$relay" > "$work/relay.log" 2>&1 &
pids+=($!)
for _ in $(seq 1 50); do
    curl -fsS "http://127.0.0.1:$port/healthz" >/dev/null 2>&1 && break
    sleep 0.2
done
curl -fsS "http://127.0.0.1:$port/healthz" >/dev/null 2>&1 ||
    { echo "screenshot.sh: the relay did not come up on 127.0.0.1:$port (see $work/relay.log)" >&2; exit 1; }
bob=$("$peer" --relay "http://127.0.0.1:$port" --data "$work/bob" --name "Bob Smith" --create --print-id \
      2>"$work/bob-create.log" | tail -1 || true)
[ ${#bob} = 32 ] || { echo "screenshot.sh: could not make Bob (see $work/relay.log)" >&2; exit 1; }
"$peer" --data "$work/bob" --relay "http://127.0.0.1:$port" --echo 600 > "$work/bob.log" 2>&1 &
pids+=($!)

# ChatCube under Xvfb, driven like a person: the window is 1000x680 at the
# screen's origin.
export APP="$app" OPT="$opt" XINPUT="$xinput" OUT="$out" WORK="$work" BOB="$bob" PORT="$port"
xvfb-run -a -s "-screen 0 1200x850x24" bash -c '
    set -e
    HOME="$WORK/home" XDG_DATA_HOME="$WORK/home/.local/share" XDG_CONFIG_HOME="$WORK/home/.config" \
        "$APP" $OPT > "$WORK/chatcube.log" 2>&1 &
    app=$!
    sleep 5
    import -window root "$OUT/signin.png"
    # The sign-in card: the name, the relay, the button.
    "$XINPUT" click 499 327 type "Alice Park" click 499 369 type "http://127.0.0.1:$PORT" click 499 443 sleep 4000
    import -window root "$OUT/empty.png"
    # The sidebar: the add-a-contact pill, then the first conversation row,
    # then the composer at the bottom of the window.
    "$XINPUT" click 120 69 type "$BOB" key Return sleep 3000
    "$XINPUT" click 160 176 sleep 1500
    "$XINPUT" click 600 625 type "Hi Bob, is the new look on your side too?" key Return sleep 3500
    "$XINPUT" type "It has avatars and a dark theme now." key Return sleep 3500
    import -window root "$OUT/chat.png"
    kill $app
    sleep 1
'
echo "screenshot.sh: $out/signin.png, empty.png, chat.png (logs in $work)"
