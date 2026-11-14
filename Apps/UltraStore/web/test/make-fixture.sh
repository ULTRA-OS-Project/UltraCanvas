#!/usr/bin/env bash
# Apps/UltraStore/web/test/make-fixture.sh
# Builds the browser test's fixture: a small store published by the admin
# backend, the way a real one is - the apps with their icons (the repository's
# app icons) and drawn screenshots, the picture variants, the signed catalog
# and the web store's HTML pages - into test/fixture/, and the signing key's
# public half into test/fixture/trusted-key.hex, which the CI build compiles
# into the test module (ULTRASTORE_WEB_TEST_KEYS). The module, the loader and
# the runtime are not in the fixture: the test lays the build's over it.
#
#   Apps/UltraStore/web/test/make-fixture.sh <build dir with ultrastoreadmin>
#
# Run it again whenever the fixture should change (a new key each time);
# commit the result. Needs python3 with Pillow for the screenshots.
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS
set -euo pipefail
build=${1:?usage: make-fixture.sh <build dir>}
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../../.." && pwd)
admin="$build/ultrastoreadmin"
[ -x "$admin" ] || { echo "no ultrastoreadmin in $build" >&2; exit 2; }
work=$(mktemp -d)
store="$work/store"
site="$work/site"
out="$here/fixture"
trap 'rm -rf "$work"' EXIT

# A site folder as the web store's build leaves it, with stand-in bytes:
# publish needs it to write the loader script into the pages; the test
# serves the real files in their place.
mkdir -p "$site/runtime"
printf 'stand-in' > "$site/ultrastore-web.wasm"
printf 'stand-in' > "$site/ultracanvas-loader.js"
cp "$repo/media/appicon/UltraStore.png" "$site/ultrastore.png"
printf '{"runtime_version": "0.0.0-fixture"}\n' > "$site/site.json"
printf 'stand-in' > "$site/runtime/ultracanvas-runtime.js"
printf 'stand-in' > "$site/runtime/ultracanvas-runtime.wasm"
printf 'stand-in' > "$site/runtime/ultracanvas-runtime.data"

a() { "$admin" --store "$store" "$@"; }
a init >/dev/null
a settings set catalog.base_url https://store.example >/dev/null
a settings set store.contact https://ultraos.eu/contact >/dev/null
a settings set web.store_download_url https://ultraos.eu/ultrastore >/dev/null
a settings set web.site_dir "$site" >/dev/null
a publisher add ultra-os-project "ULTRA OS Project" https://ultraos.eu >/dev/null

# A package file per app: a zip with one file in it.
python3 - "$work" <<'PY'
import sys, zipfile, os
work = sys.argv[1]
for name in ["ultravideo", "ultrapaint", "artcreator", "ultramail", "ultrafiler", "texter", "ultrapassword", "ultraviewer", "anchorpoint", "ultracanvas-runtime"]:
    with zipfile.ZipFile(os.path.join(work, name + ".zip"), "w") as z:
        z.writestr(name + "/README.txt", "fixture package for " + name)
PY

# Drawn screenshots: a window mock-up at 1400 x 900, as the design draws them.
python3 - "$work" <<'PY'
import sys, os
from PIL import Image, ImageDraw
work = sys.argv[1]
def shot(name, title, accent):
    im = Image.new("RGB", (1400, 900), (15, 23, 42))
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, 1400, 56], fill=(30, 41, 59))
    for i, c in enumerate([(239, 68, 68), (234, 179, 8), (34, 197, 94)]):
        d.ellipse([20 + i * 26, 20, 36 + i * 26, 36], fill=c)
    d.text((100, 20), title, fill=(226, 232, 240))
    d.rectangle([0, 56, 260, 900], fill=(30, 41, 59))
    for i in range(8):
        d.rectangle([24, 90 + i * 44, 236, 118 + i * 44], fill=(51, 65, 85))
    d.rectangle([300, 100, 1360, 700], fill=(30, 41, 59))
    d.rectangle([320, 120, 1340, 680], fill=accent)
    d.rectangle([300, 740, 1360, 860], fill=(30, 41, 59))
    for i in range(12):
        d.rectangle([320 + i * 86, 760, 380 + i * 86, 840], fill=(71, 85, 105))
    im.save(os.path.join(work, name + ".png"), optimize=True)
shot("shot-timeline", "UltraVideo - holiday-cut.uvp", (37, 99, 235))
shot("shot-grading", "UltraVideo - colour grading", (124, 58, 237))
shot("shot-paint", "UltraPaint - poster.upx", (219, 39, 119))
shot("shot-mail", "UltraMail - Inbox", (5, 150, 105))
PY

# app <id> <name> <icon> <category> <summary> <description> <price|free> [trial] [featured]
app() {
    local id=$1 name=$2 icon=$3 category=$4 summary=$5 description=$6 price=$7 trial=${8:-0} featured=${9:-no}
    a app new "$id" "$name" --publisher ultra-os-project >/dev/null
    a app set "$id" categories "$category" >/dev/null
    a app set "$id" license MIT >/dev/null
    a app set "$id" homepage https://ultraos.eu >/dev/null
    a app set "$id" open_source true >/dev/null
    a app set "$id" source_language en >/dev/null
    a app text "$id" summary en "$summary" >/dev/null
    a app text "$id" summary de "$summary (de)" >/dev/null
    a app text "$id" description en "$description" >/dev/null
    a app media "$id" icon "$repo/media/appicon/$icon" >/dev/null
    if [ "$price" != free ]; then
        a app set "$id" price "$price" >/dev/null
        a app set "$id" currency EUR >/dev/null
        a app set "$id" pricing.model one-time >/dev/null
        a app set "$id" trial_days "$trial" >/dev/null
        a app set "$id" updates.until 2027-12-31 >/dev/null
        a app set "$id" updates.period until >/dev/null
    fi
    [ "$featured" = yes ] && a app set "$id" featured true >/dev/null
    return 0
}
app org.ultraos.ultravideo UltraVideo UltraVideo.png video \
    "Cut, arrange and grade video on a real timeline, then export with one button." \
    "UltraVideo is the video editor of ULTRA OS. Drop clips on a multi-track timeline, trim and split with the keyboard, add titles and transitions, and grade with the VideoFX engine. When the cut is done, one button renders to MP4, WebM or ProRes with the settings you used last time.

Projects are plain files you can keep in any folder, open on another machine, or hand to a colleague. Every effect is non-destructive and the original footage is never touched." \
    29.90 30 yes
a app media org.ultraos.ultravideo screenshot "$work/shot-timeline.png" >/dev/null
a app media org.ultraos.ultravideo screenshot "$work/shot-grading.png" >/dev/null
a app set org.ultraos.ultravideo age_rating 3 >/dev/null
app org.ultraos.ultrapaint UltraPaint UltraPaint.png graphics \
    "Bitmap editor with layers, brushes and the PixelFX filter stack." \
    "UltraPaint paints and retouches pictures with layers, brushes, selections and the PixelFX filters, and saves every format the image library writes." free 0 yes
a app media org.ultraos.ultrapaint screenshot "$work/shot-paint.png" >/dev/null
app org.ultraos.artcreator ArtCreator ArtCreator.png graphics \
    "Vector drawing with bezier tools, text on paths and live effects." \
    "ArtCreator draws vector art with bezier tools, text on paths, live effects and layers, and exports SVG, PDF and the raster formats." 19.90 14 yes
app org.ultraos.ultramail UltraMail UltraMail.png communication \
    "Fast email for IMAP and Exchange, with an encrypted local vault." \
    "UltraMail reads and writes mail on IMAP and Exchange accounts, keeps an encrypted local vault and searches it in an instant." free 0 yes
a app media org.ultraos.ultramail screenshot "$work/shot-mail.png" >/dev/null
app org.ultraos.ultrafiler UltraFiler UltraFiler.png utilities \
    "Two-pane file manager with cloud drives, archives and previews built in." \
    "UltraFiler manages files in two panes, opens archives and cloud drives as folders and previews what it finds." free
app org.ultraos.texter Texter Texter.png office \
    "A writing tool for notes, drafts and long documents." \
    "Texter writes plain and rich text, keeps every document as a file you own and exports PDF and HTML." free
app org.ultraos.ultrapassword UltraPassword UltraPassword.png security \
    "Password vault with a browser extension and two-factor codes." \
    "UltraPassword keeps passwords and two-factor codes in a vault sealed with UltraCrypt, filled into the browser through its extension." free
app org.ultraos.ultraviewer UltraViewer UltraViewer.png video \
    "Media viewer for pictures, video and audio." \
    "UltraViewer shows pictures, plays video and audio, and steps through a folder with the keyboard." free
app org.ultraos.anchorpoint AnchorPoint AnchorPoint.png internet \
    "Peer-to-peer file transfer between your own machines." \
    "AnchorPoint moves files between machines directly, encrypted end to end, with no account and no server in between." free

# The runtime the apps share, a component, and a release with a package per app.
a app new org.ultraos.ultracanvas-runtime "UltraCanvas runtime" --kind runtime --publisher ultra-os-project >/dev/null
a release add org.ultraos.ultracanvas-runtime 0.9.600 --date 2026-11-01 >/dev/null
a package add org.ultraos.ultracanvas-runtime 0.9.600 "$work/ultracanvas-runtime.zip" --os linux --arch x86_64 >/dev/null
release() {
    local id=$1 version=$2 date=$3 file=$4 notes=$5
    a release add "$id" "$version" --date "$date" --depends org.ultraos.ultracanvas-runtime@0.9.600 --notes en "$notes" >/dev/null
    a package add "$id" "$version" "$work/$file.zip" --os linux --arch x86_64 --entry "$file/README.txt" >/dev/null
    a package add "$id" "$version" "$work/$file.zip" --os windows --arch x86_64 --entry "$file/README.txt" >/dev/null
    a package add "$id" "$version" "$work/$file.zip" --os macos --arch arm64 --entry "$file/README.txt" >/dev/null
}
release org.ultraos.ultravideo 0.7.0 2026-10-22 ultravideo "Timeline: ripple delete and snap to clip edges. Export: ProRes 422 on macOS and Linux."
release org.ultraos.ultrapaint 0.2.25 2026-10-21 ultrapaint "Brushes keep their pressure curve between sessions."
release org.ultraos.artcreator 0.7.3 2026-10-18 artcreator "Text on paths follows the path's direction."
release org.ultraos.ultramail 0.10.59 2026-10-29 ultramail "Exchange calendars sync both ways."
release org.ultraos.ultrafiler 1.75.0 2026-10-30 ultrafiler "Archives open as folders without unpacking."
release org.ultraos.texter 1.67 2026-10-28 texter "Footnotes."
release org.ultraos.ultrapassword 1.2.0 2026-10-10 ultrapassword "Passkeys."
release org.ultraos.ultraviewer 0.9.0 2026-10-12 ultraviewer "HEIF and AVIF pictures."
release org.ultraos.anchorpoint 0.4.0 2026-10-05 anchorpoint "Resumes a transfer where it stopped."

a key new fixture --passphrase fixture >/dev/null
rm -rf "$out"
mkdir -p "$out"
a publish "$out" --key 1 --passphrase fixture
a key export 1 "$work/fixture.pub" >/dev/null
grep -E '^[0-9a-fA-F]{64}$' "$work/fixture.pub" | head -1 | tr 'A-F' 'a-f' > "$out/trusted-key.hex"
# The stand-ins are the build's job; the pages name them.
rm -f "$out/ultrastore-web.wasm" "$out/ultracanvas-loader.js"
rm -rf "$out/runtime"
echo "fixture in $out, key $(cat "$out/trusted-key.hex")"
du -sh "$out"
