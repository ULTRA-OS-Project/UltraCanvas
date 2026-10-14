#!/usr/bin/env python3
"""Generate UltraVideo's token files from the design's tokens.json.

    python3 scripts/generate_ultravideo_tokens.py            # write both
    python3 scripts/generate_ultravideo_tokens.py --check    # exit 1 when either is stale

Reads  Apps/UltraVideo/screens/tokens/tokens.json  (the source of truth) and writes

    Apps/UltraVideo/screens/tokens/tokens.css   - the tokens as CSS custom properties for
                                                  the live design pages (dark on :root,
                                                  light under [data-theme="light"])
    Apps/UltraVideo/UltraVideoTokens.h          - the tokens as C++ for the application

Neither output is edited by hand: change tokens.json and run this.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOKENS = os.path.join(ROOT, "Apps", "UltraVideo", "screens", "tokens", "tokens.json")
CSS = os.path.join(ROOT, "Apps", "UltraVideo", "screens", "tokens", "tokens.css")
HEADER = os.path.join(ROOT, "Apps", "UltraVideo", "UltraVideoTokens.h")


def pascal(kebab):
    return "".join(part[:1].upper() + part[1:] for part in kebab.split("-"))


def rgba(text):
    """'#rrggbb' or 'rgba(r,g,b,a)' -> (r, g, b, a255)"""
    text = text.strip()
    if text.startswith("#"):
        return int(text[1:3], 16), int(text[3:5], 16), int(text[5:7], 16), 255
    m = re.match(r"rgba\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([\d.]+)\s*\)", text)
    if not m:
        raise SystemExit("not a colour: " + text)
    return int(m[1]), int(m[2]), int(m[3]), int(round(float(m[4]) * 255))


def number(v):
    return str(int(v)) if float(v).is_integer() else repr(float(v))


def css_text(t):
    fonts = t["font"]
    sans = '"%s", "Ubuntu", system-ui, sans-serif' % fonts["sans"]
    mono = '"%s", "Ubuntu Mono", ui-monospace, monospace' % fonts["mono"]
    out = ["/* UltraVideo design tokens - generated from tokens.json by scripts/generate_ultravideo_tokens.py. Do not edit by hand. */",
           "/* The dark theme is the default; set data-theme=\"light\" on the root for the light one. */",
           ":root {"]
    for k, c in t["color"].items():
        out.append("  --%s: %s;" % (k, c["dark"]))
    for k, s in t["shadow"].items():
        out.append("  --shadow-%s: %s;" % (k, s["dark"]))
    for k, v in t["space"].items():
        out.append("  --space-%s: %spx;" % (k, number(v["value"])))
    for k, v in t["radius"].items():
        out.append("  --radius-%s: %spx;" % (k, number(v["value"])))
    for k, v in t["size"].items():
        out.append("  --%s: %spx;" % (k, number(v["value"])))
    for k, v in t["opacity"].items():
        out.append("  --opacity-%s: %s;" % (k, number(v["value"])))
    for k, v in t["duration"].items():
        out.append("  --duration-%s: %sms;" % (k, number(v["value"])))
    out.append("  --font-sans: %s;" % sans)
    out.append("  --font-mono: %s;" % mono)
    for k, v in t["type"].items():
        out.append("  --text-%s: %d %spx/%spx var(--font-%s);" % (k, v["weight"], number(v["size"]), number(v["lineHeight"]), v["font"]))
    out.append("}")
    out.append('[data-theme="light"] {')
    for k, c in t["color"].items():
        out.append("  --%s: %s;" % (k, c["light"]))
    for k, s in t["shadow"].items():
        out.append("  --shadow-%s: %s;" % (k, s["light"]))
    out.append("}")
    return "\n".join(out) + "\n"


def header_text(t):
    L = ["// UltraVideo design tokens — generated from tokens.json. Do not edit by hand; regenerate.",
         "// Values are logical pixels; multiply by the UltraCanvas DPI scale.",
         "#pragma once",
         "#include <cstdint>",
         "",
         "namespace UltraVideo::Tokens {",
         "",
         "struct RGBA { std::uint8_t r, g, b, a; };",
         "",
         "struct Palette {"]
    for k, c in t["color"].items():
        L.append("    RGBA %s;   // %s" % (pascal(k), c["description"]))
    L.append("};")
    for theme in ("dark", "light"):
        L += ["", "inline constexpr Palette %s {" % theme.capitalize()]
        for k, c in t["color"].items():
            L.append("    /* %s */ {%d, %d, %d, %d}," % ((pascal(k),) + rgba(c[theme])))
        L.append("};")
    L += ["", "// Shadows (CSS syntax; map to UltraCanvas drop shadows)"]
    for k, s in t["shadow"].items():
        for theme in ("dark", "light"):
            L.append('inline constexpr const char* Shadow%s%s = "%s";' % (pascal(k), theme.capitalize(), s[theme]))
    L += ["", "// Spacing"]
    for k, v in t["space"].items():
        L.append("inline constexpr float Space%s = %sf;   // %s" % (pascal(k), "%.1f" % v["value"], v["description"]))
    L += ["", "// Radius"]
    for k, v in t["radius"].items():
        L.append("inline constexpr float Radius%s = %sf;   // %s" % (pascal(k), "%.1f" % v["value"], v["description"]))
    L += ["", "// Geometry"]
    for k, v in t["size"].items():
        L.append("inline constexpr float %s = %sf;   // %s" % (pascal(k), "%.1f" % v["value"], v["description"]))
    L.append("")
    for k, v in t["opacity"].items():
        L.append("inline constexpr float Opacity%s = %sf;   // %s" % (pascal(k), "%.1f" % v["value"], v["description"]))
    for k, v in t["duration"].items():
        L.append("inline constexpr int Duration%sMs = %d;   // %s" % (pascal(k), v["value"], v["description"]))
    fonts = t["font"]
    L += ["", '// Typography. Families: "%s" (UI), "%s" (timecode, tabular figures).' % (fonts["sans"], fonts["mono"]),
          "struct TextStyle { const char* family; float size; float lineHeight; int weight; };"]
    for k, v in t["type"].items():
        L.append('inline constexpr TextStyle %s {"%s", %sf, %sf, %d};   // %s' % (
            pascal(k), fonts[v["font"]], "%.1f" % v["size"], "%.1f" % v["lineHeight"], v["weight"], v["description"]))
    L += ["", "} // namespace UltraVideo::Tokens", ""]
    return "\n".join(L)


def main():
    check = "--check" in sys.argv
    with open(TOKENS, encoding="utf-8") as f:
        t = json.load(f)
    outputs = {CSS: css_text(t), HEADER: header_text(t)}
    stale = []
    for path, text in outputs.items():
        current = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        if current == text:
            continue
        stale.append(os.path.relpath(path, ROOT))
        if not check:
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
    if check:
        if stale:
            print("generate_ultravideo_tokens: stale: " + ", ".join(stale) + " (run the script)")
            return 1
        print("generate_ultravideo_tokens: up to date")
        return 0
    print("generate_ultravideo_tokens: wrote " + (", ".join(stale) if stale else "nothing (up to date)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
