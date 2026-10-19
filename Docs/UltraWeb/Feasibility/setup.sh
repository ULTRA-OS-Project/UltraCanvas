#!/bin/bash
# Fetches and builds everything the UltraWeb feasibility runs need, at the
# versions the published numbers were measured with. Everything goes into
# $WORK (default /tmp/ultraweb-feasibility); nothing is written into the
# repository. Linux x86-64; needs git, cmake, clang/gcc, python3, curl and
# Node >= 22.22.3 (the Angular 22 CLI refuses older ones).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-/tmp/ultraweb-feasibility}
mkdir -p "$WORK/tools" "$WORK/dist"
cd "$WORK"
node -e 'const [a,b,c]=process.versions.node.split(".").map(Number); if (a<22||(a===22&&(b<22||(b===22&&c<3)))) { console.error("Node "+process.versions.node+" is too old for the Angular 22 CLI (need >= 22.22.3)"); process.exit(1) }'

QUICKJS_NG_COMMIT=140b26d1638866d30247756a1db7e938a826a4ec   # quickjs-ng 0.17.0
WASI_SDK=34
WASMTIME=v49.0.2
WAMR=WAMR-2.4.5

# --- QuickJS-ng: a native qjs and a wasm32-wasip1 qjs -----------------------
if [ ! -x quickjs-ng/build/qjs ]; then
    git init -q quickjs-ng
    git -C quickjs-ng fetch -q --depth 1 https://github.com/quickjs-ng/quickjs.git $QUICKJS_NG_COMMIT
    git -C quickjs-ng checkout -q FETCH_HEAD
    cmake -S quickjs-ng -B quickjs-ng/build -DCMAKE_BUILD_TYPE=Release
    cmake --build quickjs-ng/build -j"$(nproc)" --target qjs_exe
fi
if [ ! -d tools/wasi-sdk ]; then
    mkdir -p tools/wasi-sdk
    curl -sSfL https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-$WASI_SDK/wasi-sdk-$WASI_SDK.0-x86_64-linux.tar.gz \
        | tar xz -C tools/wasi-sdk --strip-components=1
fi
if [ ! -f quickjs-ng/build-wasi/qjs ]; then
    cmake -S quickjs-ng -B quickjs-ng/build-wasi -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$WORK/tools/wasi-sdk/share/cmake/wasi-sdk-p1.cmake" \
        -DWASI_SDK_PREFIX="$WORK/tools/wasi-sdk"
    cmake --build quickjs-ng/build-wasi -j"$(nproc)" --target qjs_exe
fi

# --- WASM runtimes: wasmtime (Cranelift) and WAMR (fast JIT, fast interpreter)
if [ ! -x tools/wasmtime/wasmtime ]; then
    mkdir -p tools/wasmtime
    curl -sSfL https://github.com/bytecodealliance/wasmtime/releases/download/$WASMTIME/wasmtime-$WASMTIME-x86_64-linux.tar.xz \
        | tar xJ -C tools/wasmtime --strip-components=1
fi
if [ ! -d tools/wamr ]; then
    git clone -q --depth 1 --branch $WAMR https://github.com/bytecodealliance/wasm-micro-runtime.git tools/wamr
fi
P=tools/wamr/product-mini/platforms/linux
[ -x $P/build-interp/iwasm ] || { cmake -S $P -B $P/build-interp -DCMAKE_BUILD_TYPE=Release -DWAMR_BUILD_FAST_INTERP=1 -DWAMR_BUILD_LIBC_WASI=1; cmake --build $P/build-interp -j"$(nproc)"; }
[ -x $P/build-jit/iwasm ]    || { cmake -S $P -B $P/build-jit -DCMAKE_BUILD_TYPE=Release -DWAMR_BUILD_FAST_JIT=1 -DWAMR_BUILD_LIBC_WASI=1; cmake --build $P/build-jit -j"$(nproc)"; }
# The one WAMR mode that claims exception handling: the classic interpreter.
[ -x $P/build-classic-eh/iwasm ] || { cmake -S $P -B $P/build-classic-eh -DCMAKE_BUILD_TYPE=Release -DWAMR_BUILD_FAST_INTERP=0 -DWAMR_BUILD_EXCE_HANDLING=1 -DWAMR_BUILD_LIBC_WASI=1; cmake --build $P/build-classic-eh -j"$(nproc)"; }
# wasmtime's C API, for the host-call test (UltraWeb's own build fetches it too).
if [ ! -f tools/wasmtime-c-api/lib/libwasmtime.a ]; then
    mkdir -p tools/wasmtime-c-api
    curl -sSfL https://github.com/bytecodealliance/wasmtime/releases/download/$WASMTIME/wasmtime-$WASMTIME-x86_64-linux-c-api.tar.xz \
        | tar xJ -C tools/wasmtime-c-api --strip-components=1
fi

# --- JavaScript: React, the linkedom test DOM, esbuild ----------------------
mkdir -p js && cd js
[ -f package.json ] || echo '{"name":"ultraweb-feasibility","private":true}' > package.json
npm install --silent --no-audit --no-fund react@19.3.0 react-dom@19.3.0 linkedom@0.18.13 esbuild@0.28.2
cd "$WORK"

# --- Angular 22: a CLI app with the benchmark component, two builds ---------
if [ ! -d ngapp ]; then
    npx -y @angular/cli@22.2.1 new ngapp --defaults --skip-git --skip-tests --style=css --ssr=false --interactive=false
    (cd ngapp && npm install --silent --no-audit --no-fund zone.js@0.16.3)
fi
cp "$HERE/angular/app.ts" "$HERE/angular/app.html" ngapp/src/app/
cp "$HERE/angular/main.ts" "$HERE/angular/main-zone.ts" ngapp/src/
python3 - ngapp/angular.json <<'EOF'
import json, sys
p = sys.argv[1]; j = json.load(open(p))
b = list(j['projects'].values())[0]['architect']['build']
b['options']['styles'] = []
b['options'].pop('assets', None)
prod = b['configurations']['production']
prod.update(outputHashing='none', budgets=[], outputPath='dist/zoneless')
b['configurations']['zone'] = dict(prod, browser='src/main-zone.ts', polyfills=['zone.js'], outputPath='dist/zone')
json.dump(j, open(p, 'w'), indent=2)
EOF
(cd ngapp && npx ng build --configuration production && npx ng build --configuration zone)

# --- Bundles: every app becomes a classic script the runners can load -------
ESB="$WORK/js/node_modules/.bin/esbuild"
export NODE_PATH="$WORK/js/node_modules"     # the harness imports react / linkedom from there
"$ESB" "$HERE/harness/env-entry.js" --bundle --format=iife --platform=neutral --main-fields=module,main --outfile=dist/env.js --log-level=warning
"$ESB" "$HERE/harness/react-entry.js" --bundle --format=iife --minify --jsx=automatic \
    --define:process.env.NODE_ENV='"production"' --outfile=dist/react.js --log-level=warning
"$ESB" ngapp/dist/zoneless/browser/main.js --bundle --format=iife --minify --outfile=dist/ng-zoneless.js --log-level=warning
"$ESB" ngapp/dist/zone/browser/polyfills.js --bundle --format=iife --minify --outfile=dist/ng-zone-polyfills.js --log-level=warning
"$ESB" ngapp/dist/zone/browser/main.js --bundle --format=iife --minify --outfile=dist/ng-zone.js --log-level=warning
cp "$HERE/harness/runner-qjs.mjs" "$HERE/harness/runner-node.mjs" "$HERE/harness/parse-qjs.mjs" dist/
cp ngapp/node_modules/typescript/lib/typescript.js dist/     # a 9 MB bundle for the parse test

# --- Host-call cost: a WAMR native library and a guest that calls it --------
mkdir -p hostcall
gcc -O2 -shared -fPIC -I tools/wamr/core/iwasm/include "$HERE/hostcall/host.c" -o hostcall/libhost.so
tools/wasi-sdk/bin/clang -O2 --target=wasm32-wasip1 --sysroot=tools/wasi-sdk/share/wasi-sysroot \
    -Wl,--allow-undefined "$HERE/hostcall/guest.c" -o hostcall/guest.wasm
gcc -O2 -I tools/wasmtime-c-api/include "$HERE/hostcall/host-wasmtime.c" tools/wasmtime-c-api/lib/libwasmtime.a \
    -lpthread -ldl -lm -o hostcall/host-wasmtime

# --- C++ exceptions in a guest, in both encodings ----------------------------
mkdir -p exceptions
SYSROOT=tools/wasi-sdk/share/wasi-sysroot
EH="--target=wasm32-wasip1 --sysroot=$SYSROOT -O2 -fwasm-exceptions -L$SYSROOT/lib/wasm32-wasip1/eh -lunwind"
tools/wasi-sdk/bin/clang++ $EH "$HERE/exceptions/eh.cpp" -o exceptions/eh-legacy.wasm
tools/wasi-sdk/bin/clang++ $EH -mllvm -wasm-use-legacy-eh=false "$HERE/exceptions/eh.cpp" -o exceptions/eh-exnref.wasm

echo "Ready. Run: WORK=$WORK $HERE/run.sh"
