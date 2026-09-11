#!/bin/bash
# scripts/verify-pe.sh - check that Windows PE files are what the package
# claims they are, before they are zipped and handed to anyone.
#
# Usage:  scripts/verify-pe.sh <x86_64|aarch64> file.exe [file.dll ...]
#
# Windows refuses to start an executable with "This app can't run on your PC"
# (ERROR_BAD_EXE_FORMAT / ERROR_EXE_MACHINE_TYPE_MISMATCH) and gives no other
# information. The same dialog covers a binary built for the other CPU
# architecture, a truncated or zero-length file, and a subsystem version the
# installed Windows is too old for. All three are visible in the PE header, so
# they are checked here, at packaging time, where a mistake is cheap - the CI
# matrix builds an x86_64 and an arm64 package from the same sources with
# identical contents, and nothing but the header tells the two apart.
#
# Reads the header bytes directly rather than through objdump: the MINGW64
# and CLANGARM64 environments ship different objdumps (GNU vs LLVM) with
# different output, and the fields needed here are a handful of fixed
# offsets. Prints one line per file; exits 1 if any file fails.
#
# The checks mirror those in uc-diagnose.ps1 (the launcher shipped in the
# package), which runs them on the target machine.
set -u

if [ $# -lt 2 ]; then
    echo "Usage: $0 <x86_64|aarch64> file.exe [file.dll ...]" >&2
    exit 2
fi

case "$1" in
    x86_64)  EXPECTED_MACHINE=8664; EXPECTED_NAME="x86-64" ;;
    aarch64) EXPECTED_MACHINE=aa64; EXPECTED_NAME="ARM64" ;;
    *) echo "Error: unknown architecture '$1' (want x86_64 or aarch64)" >&2; exit 2 ;;
esac
shift

# Little-endian unsigned integer of $2 bytes at byte offset $1 of file $3,
# printed in decimal. od handles the byte order; the offsets are small.
read_le() {
    od -An -t u"$2" -j "$1" -N "$2" "$3" 2>/dev/null | tr -d ' '
}

machine_name() {
    case "$1" in
        8664) echo "x86-64" ;;
        aa64) echo "ARM64" ;;
        014c) echo "x86 (32-bit)" ;;
        01c4) echo "ARM (32-bit)" ;;
        *)    echo "machine 0x$1" ;;
    esac
}

FAILED=0
for FILE in "$@"; do
    NAME=$(basename "$FILE")
    if [ ! -f "$FILE" ]; then
        echo "FAIL  $NAME: not a file"; FAILED=1; continue
    fi
    SIZE=$(wc -c < "$FILE" | tr -d ' ')
    if [ "$SIZE" -lt 64 ]; then
        echo "FAIL  $NAME: $SIZE bytes - empty or truncated, no PE header at all"; FAILED=1; continue
    fi
    if [ "$(head -c 2 "$FILE")" != "MZ" ]; then
        echo "FAIL  $NAME: no MZ signature - not a Windows executable"; FAILED=1; continue
    fi
    PE=$(read_le 60 4 "$FILE")                       # e_lfanew
    if [ -z "$PE" ] || [ $((PE + 24)) -gt "$SIZE" ]; then
        echo "FAIL  $NAME: PE header offset $PE lies beyond the end of the file (truncated?)"; FAILED=1; continue
    fi
    if [ "$(od -An -c -j "$PE" -N 4 "$FILE" | tr -d ' ')" != 'PE\0\0' ]; then
        echo "FAIL  $NAME: no PE signature at offset $PE"; FAILED=1; continue
    fi
    MACHINE=$(printf '%04x' "$(read_le $((PE + 4)) 2 "$FILE")")
    SECTIONS=$(read_le $((PE + 6)) 2 "$FILE")
    OPT_SIZE=$(read_le $((PE + 20)) 2 "$FILE")
    OPT=$((PE + 24))
    MAGIC=$(printf '%04x' "$(read_le "$OPT" 2 "$FILE")")
    case "$MAGIC" in
        020b) BITS="PE32+"; SUBSYS_MAJOR=$(read_le $((OPT + 48)) 2 "$FILE"); SUBSYS_MINOR=$(read_le $((OPT + 50)) 2 "$FILE"); SUBSYSTEM=$(read_le $((OPT + 68)) 2 "$FILE") ;;
        010b) BITS="PE32";  SUBSYS_MAJOR=$(read_le $((OPT + 48)) 2 "$FILE"); SUBSYS_MINOR=$(read_le $((OPT + 50)) 2 "$FILE"); SUBSYSTEM=$(read_le $((OPT + 68)) 2 "$FILE") ;;
        *)    echo "FAIL  $NAME: unknown optional header magic 0x$MAGIC"; FAILED=1; continue ;;
    esac

    # End of the last section's raw data must lie within the file. A download
    # or an extraction that stopped early leaves a header that parses but a
    # body that does not - and the loader answers with the same dialog.
    SECTION_TABLE=$((OPT + OPT_SIZE))
    NEED=0
    i=0
    while [ $i -lt "$SECTIONS" ]; do
        HDR=$((SECTION_TABLE + i * 40))
        RAW_SIZE=$(read_le $((HDR + 16)) 4 "$FILE")
        RAW_PTR=$(read_le $((HDR + 20)) 4 "$FILE")
        if [ -z "$RAW_SIZE" ] || [ -z "$RAW_PTR" ]; then NEED=$((SIZE + 1)); break; fi
        END=$((RAW_PTR + RAW_SIZE))
        [ "$END" -gt "$NEED" ] && NEED=$END
        i=$((i + 1))
    done

    PROBLEMS=""
    [ "$MACHINE" != "$EXPECTED_MACHINE" ] && PROBLEMS="$PROBLEMS; built for $(machine_name "$MACHINE"), package is $EXPECTED_NAME"
    [ "$NEED" -gt "$SIZE" ] && PROBLEMS="$PROBLEMS; truncated: sections end at byte $NEED, file is $SIZE bytes"
    # The Windows loader limit on the section table.
    [ "$SECTIONS" -gt 96 ] && PROBLEMS="$PROBLEMS; $SECTIONS sections, Windows loads at most 96"
    # Windows 10 and 11 both report 10.0; anything above that cannot start
    # anywhere yet. (MinGW links 5.02 or 6.00 by default.)
    [ "$SUBSYS_MAJOR" -gt 10 ] && PROBLEMS="$PROBLEMS; subsystem version $SUBSYS_MAJOR.$SUBSYS_MINOR is newer than any Windows"

    if [ -n "$PROBLEMS" ]; then
        echo "FAIL  $NAME: ${PROBLEMS#; }"; FAILED=1
    else
        printf 'ok    %-40s %s %s, %d sections, subsystem %d (%d.%02d), %d bytes\n' \
            "$NAME" "$(machine_name "$MACHINE")" "$BITS" "$SECTIONS" "$SUBSYSTEM" "$SUBSYS_MAJOR" "$SUBSYS_MINOR" "$SIZE"
    fi
done

exit $FAILED
