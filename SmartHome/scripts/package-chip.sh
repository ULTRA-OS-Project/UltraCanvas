#!/usr/bin/env bash
# SmartHome/scripts/package-chip.sh
#
# Turns a connectedhomeip chip-tool build into something a CMake project can
# link. The SDK's GN build produces no installable library: chip-tool links a
# few hundred loose object files plus ~45 static archives, listed in one ninja
# edge. This script reads that edge, drops what belongs to chip-tool itself
# (its commands, the websocket server, editline), archives the loose objects
# into one library, and writes the link list CMake consumes.
#
#   package-chip.sh <chip checkout> <gn out dir, e.g. out/host> <output dir>
#
# Produces in <output dir>:
#   libchip-objects.a   the loose objects
#   link.txt            one path or -l flag per line, in link order
#
# Author: UltraCanvas Framework
set -euo pipefail

CHIP_SRC=$(cd "$1" && pwd)
CHIP_OUT=$(cd "$2" && pwd)
OUT=$3
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

EDGE_FILE="$CHIP_OUT/obj/chip-tool.ninja"
[ -f "$EDGE_FILE" ] || { echo "no chip-tool link edge at $EDGE_FILE; build chip-tool first" >&2; exit 1; }

# The edge is one logical line: "build ./chip-tool: link <inputs...>", with
# ninja's "$" continuations. Join them, then split into words.
inputs=$(awk '
    /^build \.\/chip-tool: link/ { collecting = 1 }
    collecting {
        line = $0
        if (sub(/\$$/, "", line)) { printf "%s", line " " } else { print line; collecting = 0 }
    }' "$EDGE_FILE" | sed 's/^build \.\/chip-tool: link //' | tr ' ' '\n' | grep -v '^$' | grep -v '^|')

objects=()
archives=()
while IFS= read -r input; do
    case "$input" in
        obj/chip-tool.*|*/examples/chip-tool/*) continue ;;                    # chip-tool's own code
        */libwebsockets/*|*/websocket-server/*|*/editline/*) continue ;;       # its interactive shell
        *.o) objects+=("$CHIP_OUT/$input") ;;
        *.a) archives+=("$CHIP_OUT/$input") ;;
    esac
done <<< "$inputs"

[ ${#objects[@]} -gt 0 ] || { echo "no objects found in the chip-tool edge" >&2; exit 1; }
for f in "${objects[@]}" "${archives[@]}"; do
    [ -f "$f" ] || { echo "missing $f — is the chip-tool build complete?" >&2; exit 1; }
done

rm -f "$OUT/libchip-objects.a"
ar rcs "$OUT/libchip-objects.a" "${objects[@]}"

{
    echo "$OUT/libchip-objects.a"
    printf '%s\n' "${archives[@]}"
    # What chip-tool's edge adds beyond the archives ("libs = ..." in the same file).
    grep -m1 '^  libs = ' "$EDGE_FILE" | sed 's/^  libs = //' | tr ' ' '\n' | grep -v '^$'
} > "$OUT/link.txt"

echo "packaged ${#objects[@]} objects and ${#archives[@]} archives into $OUT"
