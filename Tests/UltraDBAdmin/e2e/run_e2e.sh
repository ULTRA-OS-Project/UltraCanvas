#!/usr/bin/env bash
# Tests/UltraDBAdmin/e2e/run_e2e.sh
# UltraDBAdmin's browser build end to end: the page (UltraDBAdmin.html) in a
# headless Chromium, a real ultradbadmin-relay, and a test OpenID provider
# (test_oidc_provider.py), all on 127.0.0.1. The page signs in through the
# relay's OpenID Connect sign-in, browses a SQLite table through the relay,
# and runs a query the relay's statement time-out stops; the relay's audit
# log is the record each step is checked against.
#
#   run_e2e.sh <ultradbadmin-relay> <folder with UltraDBAdmin.html/.js/.wasm/.data> <out folder>
#
# Environment: E2E_CHANNEL / E2E_CHROME choose the browser, E2E_PLAYWRIGHT
# the playwright package, as in e2e.mjs. Ports 8765 (relay), 8780 (page),
# 9100 (provider) must be free.
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS
set -euo pipefail

RELAY_BIN="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
PAGE_DIR="$(cd "$2" && pwd)"
OUT="$(mkdir -p "$3" && cd "$3" && pwd)"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
WORK="$(mktemp -d)"
PIDS=()
cleanup() {
    for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null || true; done
    wait 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

wait_for() {      # url, what
    for _ in $(seq 1 100); do
        curl -fsS -o /dev/null "$1" 2>/dev/null && return 0
        sleep 0.2
    done
    echo "e2e: $2 did not come up at $1" >&2
    return 1
}

# A table to browse: 1200 orders.
python3 - "$WORK/shop.db" <<'EOF'
import sqlite3, sys
db = sqlite3.connect(sys.argv[1])
db.execute("CREATE TABLE orders(id INTEGER PRIMARY KEY, customer TEXT, total REAL)")
db.executemany("INSERT INTO orders(customer, total) VALUES (?, ?)",
               [("customer %d" % (i % 37), i * 1.25) for i in range(1, 1201)])
db.commit()
EOF

cat > "$WORK/relay.ini" <<EOF
listen = 127.0.0.1:8765
path = /relay
audit = $WORK/audit.log
public_url = http://127.0.0.1:8765/relay

[signin]
label = Test SSO
issuer = http://127.0.0.1:9100
client_id = uda

[connection shop]
driver = sqlite
database = $WORK/shop.db

[user anna]
email = anna@example.com
shop = write

[limits]
statement_timeout = 2
EOF

python3 "$HERE/test_oidc_provider.py" --port 9100 --client uda --email anna@example.com > "$OUT/provider.log" 2>&1 &
PIDS+=($!)
wait_for http://127.0.0.1:9100/.well-known/openid-configuration "the test provider"
"$RELAY_BIN" --config "$WORK/relay.ini" > "$OUT/relay.log" 2>&1 &
PIDS+=($!)
wait_for http://127.0.0.1:8765/health "the relay"
(cd "$PAGE_DIR" && exec python3 "$ROOT/UltraCanvas/OS/WASM/demo/serve.py" 8780) > "$OUT/serve.log" 2>&1 &
PIDS+=($!)
wait_for http://127.0.0.1:8780/UltraDBAdmin.html "the page's server"

status=0
node "$HERE/e2e.mjs" "http://127.0.0.1:8780/UltraDBAdmin.html?relay=ws://127.0.0.1:8765/relay" "$OUT" || status=$?
cp "$WORK/audit.log" "$OUT/audit.log" 2>/dev/null || true

# What the relay saw, step by step.
check() {         # pattern, what
    if grep -E -q "$1" "$OUT/audit.log" 2>/dev/null; then
        echo "ok    $2"
    else
        echo "FAIL  $2 (no audit line matching: $1)"
        status=1
    fi
}
check $'signed in with Test SSO as anna@example\\.com: user anna' "the page signed in through the relay"
check $'\tanna\t[^\t]*\tshop\tok\tSELECT \\* FROM "main"\\."orders"' "a page of orders browsed as anna"
check $'\tanna\t[^\t]*\tshop\tok\tSELECT count\\(\\*\\)' "the rows counted"
check $'\tanna\t[^\t]*\tshop\tfailed\tWITH RECURSIVE' "the endless query stopped by the time-out"
if [ "$status" -eq 0 ]; then echo "e2e: passed"; else echo "e2e: FAILED - screenshots and logs in $OUT"; fi
exit "$status"
