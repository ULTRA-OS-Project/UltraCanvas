#!/bin/bash
# scripts/build-windows-installer.sh - the UltraCanvas Windows installer
# (Windows/Installer/UltraCanvasSetup.nsi) from the folder package-win.sh
# filled, so the installer and the zip ship the same files.
#
# Usage:
#   scripts/build-windows-installer.sh <dist dir> <version> <arch> <output.exe>
#
# Needs makensis: on the PATH (in the CLANG64 shell `pacman -S
# mingw-w64-clang-x86_64-nsis`; on Linux `apt install nsis` builds the same
# installer), else NSIS for Windows where its installer puts it (Program
# Files) - CLANGARM64 has no NSIS package, and the CI runners carry that one.
# MAKENSIS names another.
#
# Generates, in a temporary folder:
#   - the Start-menu list: one entry per application, where an application
#     is an executable built for the Windows GUI subsystem - read from its PE
#     header, so a command-line tool (console subsystem) never gets one -
#     less the helpers in NOT_APPLICATIONS below, which have no window of
#     their own to open;
#   - the removal list: every file it installs and every folder, deepest
#     first, so the uninstaller removes exactly those and leaves a folder
#     that still holds something of the user's;
#   - the installer's icon from media/appicon/UltraCanvas.png, when
#     ImageMagick is there to convert it.
# Version: 0.1.4 - the Start menu lists every program with a desktop entry,
#                  under its name - UltraDesktop and the other console-built
#                  applications included - and GUI programs without one
# Version: 0.1.3 - the folder and the output as absolute paths: makensis reads
#                  a relative one from the .nsi file's folder, not from here
# Version: 0.1.2 - makensis also from Program Files (the CI runners' NSIS)
# Version: 0.1.1 - no Start-menu entry for UltraCanvasShellGuard
# Version: 0.1.0
# Last Modified: 2026-10-15
set -e

if [ $# -ne 4 ]; then
    echo "Usage: $0 <dist dir> <version> <arch> <output.exe>" >&2
    exit 2
fi
DIST="$1"
VERSION="$2"
ARCH="$3"
OUTFILE="$4"
SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
NSI="$SCRIPT_DIR/Windows/Installer/UltraCanvasSetup.nsi"

if [ -z "${MAKENSIS:-}" ]; then
    if command -v makensis >/dev/null 2>&1; then
        MAKENSIS=makensis
    else
        for candidate in "/c/Program Files (x86)/NSIS/makensis.exe" "/c/Program Files/NSIS/makensis.exe"; do
            if [ -x "$candidate" ]; then MAKENSIS="$candidate"; break; fi
        done
    fi
fi
if [ -z "${MAKENSIS:-}" ]; then
    echo "Error: makensis not found. Install NSIS: in the CLANG64 shell 'pacman -S mingw-w64-clang-x86_64-nsis'," >&2
    echo "       on Windows the NSIS installer (Program Files), on Linux 'apt install nsis'." >&2
    exit 1
fi
if [ ! -d "$DIST" ] || [ -z "$(ls -A "$DIST")" ]; then
    echo "Error: '$DIST' is not a packaged folder - run package-win.sh first" >&2
    exit 1
fi
case "$VERSION" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "Error: version '$VERSION' is not x.y.z" >&2; exit 1 ;;
esac

# makensis reads a relative path from the .nsi file's folder
# (Windows/Installer), not from where it was started: both made absolute.
DIST="$(cd "$DIST" && pwd)"
mkdir -p "$(dirname "$OUTFILE")"
OUTFILE="$(cd "$(dirname "$OUTFILE")" && pwd)/$(basename "$OUTFILE")"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
SHORTCUTS="$WORK/shortcuts.nsh"
REMOVE="$WORK/remove.nsh"

# A path as makensis on this host reads it: a Windows path under MSYS2, the
# path itself on Linux.
native_path() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else echo "$1"; fi
}
# A relative path inside the install folder, as NSIS writes it.
nsis_rel() {
    printf '%s' "$1" | sed 's|^\./||; s|/|\\|g'
}
# The PE header's Subsystem field: 2 = Windows GUI, 3 = console; 0 when the
# file is no PE image. At the same offset in PE32 and PE32+: the optional
# header starts 24 bytes after "PE\0\0", Subsystem 68 bytes into it.
pe_subsystem() {
    local pe
    pe=$(od -An -tu4 -j60 -N4 "$1" 2>/dev/null | tr -d ' ')
    [ -n "$pe" ] || { echo 0; return; }
    [ "$(od -An -c -j"$pe" -N2 "$1" 2>/dev/null | tr -d ' ')" = "PE" ] || { echo 0; return; }
    od -An -tu2 -j$((pe + 92)) -N2 "$1" 2>/dev/null | tr -d ' '
}
# NSIS reads $ as a variable and " ends the string.
nsis_quote() {
    printf '%s' "$1" | sed 's/\$/$$/g; s/"/$\\"/g'
}

# ===== THE START MENU =====
# An application is a program with a desktop entry (Apps/*/*.desktop, which
# the Linux menus read too), named as the entry names it, whatever its
# subsystem. A console companion (<App>.com) is never an entry: the Start menu
# starts the .exe.
# UltraDesktop's entry says NoDisplay - a Linux session starts the desktop -
# but on Windows the Start menu is where one starts it to try it. A GUI
# program with no entry (Texter, UltraCanvasDemo) is listed under its own
# name; a console program with none (ultramsg, videofx) is a command-line
# tool and is not.
# Not applications: UltraCanvasShellGuard is what the sign-in starts when
# UltraDesktop runs instead of Explorer - started from the Start menu it
# would only start UltraDesktop.
NOT_APPLICATIONS=" UltraCanvasShellGuard "
ENTRY_NAMES="$WORK/entry-names"
: > "$ENTRY_NAMES"
for entry in "$SCRIPT_DIR"/Apps/*/*.desktop; do
    [ -f "$entry" ] || continue
    program="$(sed -n 's/^Exec=//p' "$entry" | head -n1 | awk '{print $1}')"
    title="$(sed -n 's/^Name=//p' "$entry" | head -n1 | tr -d '\r')"
    program="$(basename "$program")"
    [ -n "$program" ] || continue
    # A name that is no Windows file name keeps the program's.
    case "$title" in ""|*[\\/:*?\"\<\>\|]*) title="$program" ;; esac
    printf '%s\t%s\n' "$(printf '%s' "$program" | tr '[:upper:]' '[:lower:]')" "$title" >> "$ENTRY_NAMES"
done
: > "$SHORTCUTS"
COUNT=0
LISTED=""
for exe in "$DIST"/*.exe; do
    [ -e "$exe" ] || continue
    name="$(basename "$exe" .exe)"
    case "$NOT_APPLICATIONS" in *" $name "*) continue ;; esac
    lower="$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')"
    title="$(awk -F'\t' -v p="$lower" '$1 == p { print $2; exit }' "$ENTRY_NAMES")"
    if [ -z "$title" ]; then
        [ "$(pe_subsystem "$exe")" = "2" ] || continue
        title="$name"
    fi
    printf 'CreateShortCut "$SMPROGRAMS\\${PRODUCT}\\%s.lnk" "$INSTDIR\\%s.exe"\n' \
        "$(nsis_quote "$title")" "$(nsis_quote "$name")" >> "$SHORTCUTS"
    COUNT=$((COUNT + 1))
    LISTED="$LISTED${LISTED:+, }$title"
done
if [ "$COUNT" -eq 0 ]; then
    echo "Error: no application in '$DIST' - nothing for the Start menu" >&2
    exit 1
fi
echo "  Start menu: $COUNT applications ($LISTED)"

# ===== WHAT THE UNINSTALLER REMOVES =====
: > "$REMOVE"
(cd "$DIST" && find . -type f | LC_ALL=C sort) | while IFS= read -r f; do
    printf 'Delete "$INSTDIR\\%s"\n' "$(nsis_quote "$(nsis_rel "$f")")" >> "$REMOVE"
done
# Deepest first, so each folder is empty - of what was installed - by the
# time its turn comes.
(cd "$DIST" && find . -mindepth 1 -type d | awk -F/ '{ print NF "\t" $0 }' | LC_ALL=C sort -rn | cut -f2-) |
while IFS= read -r d; do
    printf 'RMDir "$INSTDIR\\%s"\n' "$(nsis_quote "$(nsis_rel "$d")")" >> "$REMOVE"
done
echo "  Uninstaller: $(grep -c '^Delete' "$REMOVE") files, $(grep -c '^RMDir' "$REMOVE") folders"

# ===== THE ICON =====
ICON_DEFINE=()
ICON_SRC="$SCRIPT_DIR/media/appicon/UltraCanvas.png"
MAGICK=""
command -v magick >/dev/null 2>&1 && MAGICK="magick"
[ -z "$MAGICK" ] && command -v convert >/dev/null 2>&1 && MAGICK="convert"
if [ -n "$MAGICK" ] && [ -f "$ICON_SRC" ] &&
   "$MAGICK" "$ICON_SRC" -define icon:auto-resize=256,48,32,16 "$WORK/UltraCanvas.ico" 2>/dev/null; then
    ICON_DEFINE=("-DUC_ICON=$(native_path "$WORK/UltraCanvas.ico")")
else
    echo "  (no ImageMagick: the installer keeps NSIS's own icon)"
fi

# ===== BUILD =====
"$MAKENSIS" -V2 -INPUTCHARSET UTF8 \
    "-DUC_VERSION=$VERSION" \
    "-DUC_ARCH=$ARCH" \
    "-DUC_DIST=$(native_path "$DIST")" \
    "-DUC_OUTFILE=$(native_path "$OUTFILE")" \
    "-DUC_SHORTCUTS=$(native_path "$SHORTCUTS")" \
    "-DUC_REMOVE=$(native_path "$REMOVE")" \
    "-DUC_LICENSE=$(native_path "$SCRIPT_DIR/LICENSE")" \
    "${ICON_DEFINE[@]}" \
    "$(native_path "$NSI")"
echo "  Installer: $OUTFILE"
