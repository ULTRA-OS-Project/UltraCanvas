#!/bin/bash
# scripts/build-windows-installer.sh - the UltraCanvas Windows installer
# (Windows/Installer/UltraCanvasSetup.nsi) from the folder package-win.sh
# filled, so the installer and the zip ship the same files.
#
# Usage:
#   scripts/build-windows-installer.sh <dist dir> <version> <arch> <output.exe>
#
# Needs makensis: in the MSYS2 shell `pacman -S nsis` (or NSIS for Windows on
# the PATH); on Linux `apt install nsis` builds the same installer.
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
# Version: 0.1.1 - no Start-menu entry for UltraCanvasShellGuard
# Version: 0.1.0
# Last Modified: 2026-10-14
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

if ! command -v makensis >/dev/null 2>&1; then
    echo "Error: makensis not found. Install NSIS: in the MSYS2 shell 'pacman -S nsis', on Linux 'apt install nsis'." >&2
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

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$(dirname "$OUTFILE")"
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
# GUI programs that are not applications: UltraCanvasShellGuard is what the
# sign-in starts when UltraDesktop runs instead of Explorer - started from
# the Start menu it would only start UltraDesktop.
NOT_APPLICATIONS=" UltraCanvasShellGuard "
: > "$SHORTCUTS"
COUNT=0
for exe in "$DIST"/*.exe; do
    [ -e "$exe" ] || continue
    name="$(basename "$exe" .exe)"
    case "$NOT_APPLICATIONS" in *" $name "*) continue ;; esac
    [ "$(pe_subsystem "$exe")" = "2" ] || continue
    q="$(nsis_quote "$name")"
    printf 'CreateShortCut "$SMPROGRAMS\\${PRODUCT}\\%s.lnk" "$INSTDIR\\%s.exe"\n' "$q" "$q" >> "$SHORTCUTS"
    COUNT=$((COUNT + 1))
done
if [ "$COUNT" -eq 0 ]; then
    echo "Error: no Windows GUI program in '$DIST' - nothing for the Start menu" >&2
    exit 1
fi
echo "  Start menu: $COUNT applications"

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
makensis -V2 -INPUTCHARSET UTF8 \
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
