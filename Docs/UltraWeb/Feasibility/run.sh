#!/bin/bash
# Runs the feasibility matrix after setup.sh: every app on every engine, then
# the API-recording runs, then the host-call cost test. One JSON line per run
# in $WORK/runs/; summarise with summarise.py.
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-/tmp/ultraweb-feasibility}
cd "$WORK/dist"                       # the WASI engines see only this folder
mkdir -p "$WORK/runs"
OUT=$WORK/runs

Q=$WORK/quickjs-ng/build/qjs
QW=$WORK/quickjs-ng/build-wasi/qjs
WT=$WORK/tools/wasmtime/wasmtime
P=$WORK/tools/wamr/product-mini/platforms/linux
IW="--stack-size=8388608 --heap-size=0 --dir=."
declare -A APPS=( [react]="react.js" [ng-zoneless]="ng-zoneless.js" [ng-zone]="ng-zone-polyfills.js ng-zone.js" )

run() { # engine iterations app command...
    local eng=$1 it=$2 app=$3; shift 3
    echo "$(date +%T) $app on $eng x$it" >&2
    timeout 3600 "$@" timing "$it" "$eng" env.js ${APPS[$app]} > "$OUT/$app-$eng.json" 2> "$OUT/$app-$eng.err"
}
for app in react ng-zoneless ng-zone; do
    run node         5 $app node runner-node.mjs
    run node-jitless 5 $app node --jitless runner-node.mjs
    run qjs          5 $app "$Q" runner-qjs.mjs
    run wasmtime     5 $app "$WT" run --dir=. "$QW" runner-qjs.mjs
    run wamr-fastjit 3 $app "$P/build-jit/iwasm" $IW "$QW" runner-qjs.mjs
    run wamr-interp  1 $app "$P/build-interp/iwasm" $IW "$QW" runner-qjs.mjs
done

# API recording: which DOM members each framework touches, and how much of
# each operation's time is spent inside the (JavaScript) test DOM.
for app in react ng-zoneless ng-zone; do
    echo "$(date +%T) $app surface on node / qjs" >&2
    timeout 3600 node runner-node.mjs surface 1 node env.js ${APPS[$app]} > "$OUT/$app-node-surface.json" 2> "$OUT/$app-node-surface.err"
    timeout 3600 "$Q" runner-qjs.mjs surface 1 qjs env.js ${APPS[$app]} > "$OUT/$app-qjs-surface.json" 2> "$OUT/$app-qjs-surface.err"
done

# Parse cost and the bytecode cache, natively and inside wasmtime.
for f in react.js ng-zoneless.js typescript.js; do
    "$Q" parse-qjs.mjs $f > "$OUT/parse-qjs-$f.json"
    "$WT" run --dir=. "$QW" parse-qjs.mjs $f > "$OUT/parse-wasmtime-$f.json"
done

# Host-call cost of a guest calling into the host (WAMR native library).
cd "$WORK/hostcall"
"$P/build-jit/iwasm" --native-lib=./libhost.so guest.wasm 10000000 > "$OUT/hostcall-wamr-fastjit.json"
"$P/build-interp/iwasm" --native-lib=./libhost.so guest.wasm 10000000 > "$OUT/hostcall-wamr-interp.json"
./host-wasmtime guest.wasm 10000000 > "$OUT/hostcall-wasmtime.json"
UC_UNCHECKED=1 ./host-wasmtime guest.wasm 10000000 > "$OUT/hostcall-wasmtime-unchecked.json"

# C++ exceptions: which runtime runs which encoding.
cd "$WORK/exceptions"
for m in eh-exnref eh-legacy; do
    {
        echo "== $m: wasmtime";             "$WT" run -W exceptions=y $m.wasm 2>&1 | head -3
        echo "== $m: WAMR fast JIT";        "$P/build-jit/iwasm" $m.wasm 2>&1 | head -3
        echo "== $m: WAMR classic + EH";    "$P/build-classic-eh/iwasm" $m.wasm 2>&1 | head -3
    } >> "$OUT/exceptions.txt"
done
echo "done: python3 $HERE/summarise.py $OUT" >&2
