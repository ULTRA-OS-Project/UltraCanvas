#!/usr/bin/env python3
# scripts/generate_module_map.py
# Draws Docs/Modules/ModuleMap.svg: how UltraCloud, UltraNet and the other
# shared modules relate, and which apps link which of them. The relations are
# typed in below from the CMake targets (target_link_libraries); re-check them
# there when a link changes, then run:  python3 scripts/generate_module_map.py
import sys
from xml.sax.saxutils import escape

W, H = 1400, 1110
out = []
def add(s): out.append(s)

# One hue per module family; chips and module borders share it.
FAM = {
    "cloud":  ("#0f766e", "#e6f4f2"),   # UltraCloud, UltraCloudUI
    "net":    ("#1d4ed8", "#e8eefc"),   # UltraNet
    "db":     ("#b45309", "#fbf0e1"),   # UltraDatabase
    "sec":    ("#7c3aed", "#f1ebfd"),   # UltraVault, UltraCrypt, Base32
    "msg":    ("#15803d", "#e7f4ea"),   # UltraMessage
}
MOD_FAM = {
    "UltraCloudUI": "cloud", "UltraCloud": "cloud", "UltraNet": "net",
    "UltraDatabase": "db", "UltraVault": "sec", "UltraCrypt": "sec",
    "UltraMessage": "msg",
}
INK, MUTED, LINE, PAPER, CARD = "#1f2937", "#6b7280", "#9ca3af", "#ffffff", "#f8fafc"

def text(x, y, s, size=13, weight="normal", fill=INK, anchor="start", style=""):
    st = f' font-style="{style}"' if style else ""
    add(f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{weight}" '
        f'fill="{fill}" text-anchor="{anchor}"{st}>{escape(s)}</text>')

def box(x, y, w, h, fam=None, dashed=False, fill=None, rx=10, sw=1.6):
    stroke, tint = FAM[fam] if fam else (LINE, CARD)
    dash = ' stroke-dasharray="6 4"' if dashed else ""
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" '
        f'fill="{fill or tint}" stroke="{stroke}" stroke-width="{sw}"{dash}/>')

def module(x, y, w, h, name, lines, fam, note=None):
    box(x, y, w, h, fam)
    text(x + 14, y + 24, name, 16, "bold", FAM[fam][0])
    for i, l in enumerate(lines):
        text(x + 14, y + 46 + i * 18, l, 12.5, fill=INK)
    if note:
        text(x + w - 12, y + 24, note, 11, fill=MUTED, anchor="end")

def arrow(x1, y1, x2, y2, label=None, lx=None, ly=None, dashed=False, color="#4b5563"):
    dash = ' stroke-dasharray="5 4"' if dashed else ""
    add(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" '
        f'stroke-width="1.6"{dash} marker-end="url(#arrow)"/>')
    if label:
        text(lx, ly, label, 11.5, fill=MUTED)

def band(y, title, sub=""):
    text(24, y, title.upper(), 12, "bold", MUTED)
    if sub:
        text(24 + 8.2 * len(title) + 14, y, sub, 12, fill=MUTED, style="italic")

add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
    f'viewBox="0 0 {W} {H}" font-family="Inter, \'Segoe UI\', Helvetica, Arial, sans-serif">')
add('<title>UltraCloud, UltraNet and the shared modules</title>')
add('<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" '
    'markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#4b5563"/></marker></defs>')
add(f'<rect width="{W}" height="{H}" fill="{PAPER}"/>')

text(24, 36, "UltraCloud, UltraNet and the shared modules", 22, "bold")
text(24, 58, "An arrow means “links and calls”. Solid = built today (main, framework 0.9.66); "
     "dashed = planned. Verified against the CMake targets.", 13, fill=MUTED)

# ---- Applications ---------------------------------------------------------
band(84, "Applications", "— the chips name the modules each app links")
apps = [
    ("UltraMail", ["UltraCloudUI", "UltraCloud", "UltraNet", "UltraDatabase",
                   "UltraVault", "UltraMessage", "UltraCrypt"], False, []),
    ("UltraFiler", ["UltraCloudUI", "UltraCloud"], False, []),
    ("UltraSocial", ["UltraNet", "UltraDatabase", "UltraVault", "UltraCrypt"], False, []),
    ("EmailCleaner", ["UltraNet", "UltraDatabase"], False, []),
    ("UltraFIBU", ["UltraDatabase", "UltraCrypt"], False, []),
    ("UltraAuthenticator", ["UltraCrypt"], False, []),
    ("AnchorPoint", ["UltraCrypt"], False, ["UltraNet"]),
    ("UltraCalendar", [], True, ["UltraCloud", "UltraNet", "UltraDatabase",
                                 "UltraVault", "UltraMessage"]),
]
cw, gap, ay, ah = 160, 12, 96, 172
for i, (name, mods, planned, later) in enumerate(apps):
    x = 18 + i * (cw + gap)
    box(x, ay, cw, ah, dashed=planned, fill=PAPER if planned else CARD)
    text(x + cw / 2, ay + 22, name, 13.5, "bold", MUTED if planned else INK, "middle")
    if planned:
        text(x + cw / 2, ay + 38, "planned (proposal)", 11, fill=MUTED, anchor="middle", style="italic")
    cy = ay + (48 if planned else 34)
    for m in mods + later:
        stroke, tint = FAM[MOD_FAM[m]]
        dashed = m in later
        dash = ' stroke-dasharray="4 3"' if dashed else ""
        add(f'<rect x="{x + 12}" y="{cy}" width="{cw - 24}" height="16" rx="8" '
            f'fill="{PAPER if dashed else tint}" stroke="{stroke}" stroke-width="1"{dash}/>')
        text(x + cw / 2, cy + 12, m + (" (later)" if dashed and not planned else ""),
             11, fill=stroke, anchor="middle")
        cy += 19

# ---- Shared service modules ---------------------------------------------
band(304, "Shared service modules")
module(380, 316, 270, 64, "UltraCloudUI", ["Add-account dialog · link picker"], "cloud")
module(380, 412, 270, 112, "UltraCloud",
       ["Accounts, upload, download, share links",
        "Nextcloud/ownCloud · WebDAV · FTP/SFTP",
        "Dropbox · OneDrive · Google Drive (OAuth2)",
        "Provider plug-ins (dlopen)"], "cloud")
module(760, 412, 250, 84, "UltraMessage",
       ["Message bus between apps", "journal in UltraDatabase"], "msg")

# ---- Foundation -----------------------------------------------------------
band(574, "Foundation modules")
module(40, 586, 340, 160, "UltraNet",
       ["HTTP(S) with any verb and header, SSE",
        "OAuth2 + PKCE, shared app registry",
        "DNS incl. SRV · TLS verified by default",
        "TCP/UDP sockets, WebSocket, FTP",
        "Plug-ins: webdav, smtp, imap, jmap, pop3,",
        "ssh, mqtt, amqp, sip, rtsp, mdns, …"], "net", "client side only")
module(540, 586, 290, 110, "UltraDatabase",
       ["Named connections, migrations",
        "SQLite (local) · PostgreSQL (server)",
        "password = UltraVault key (vault:…)"], "db")
module(990, 586, 280, 110, "UltraVault",
       ["Secrets by key; memory + file backends",
        "DeviceKeyVault: per-app vault that",
        "unlocks with the device key"], "sec")
module(990, 776, 280, 84, "UltraCrypt",
       ["Hash, HMAC, AEAD, Argon2id, CSPRNG",
        "TOTP maths for UltraAuthenticator"], "sec")
box(1286, 790, 100, 56, "sec")
text(1336, 814, "Base32", 13, "bold", FAM["sec"][0], "middle")
text(1336, 832, "codec", 11.5, fill=INK, anchor="middle")

# ---- Arrows between modules --------------------------------------------
arrow(515, 380, 515, 410)                                           # UI -> Cloud
arrow(430, 524, 300, 584)                                           # Cloud -> Net
text(352, 548, "HTTP, WebDAV, OAuth2", 11.5, fill=MUTED, anchor="end")
arrow(560, 524, 560, 584)                                           # Cloud -> DB
text(552, 568, "account list", 11.5, fill=MUTED, anchor="end")
# Cloud -> Vault as an orthogonal route, so it crosses the bus edge at a
# right angle instead of a shallow one.
add('<polyline points="610,524 610,550 1130,550 1130,584" fill="none" '
    'stroke="#4b5563" stroke-width="1.6" marker-end="url(#arrow)"/>')
text(940, 543, "tokens, passwords", 11.5, fill=MUTED)
arrow(800, 496, 800, 584)                                           # Msg -> DB
text(808, 522, "journal", 11.5, fill=MUTED)
arrow(830, 640, 988, 640)                                           # DB -> Vault
text(846, 632, "PostgreSQL password", 11.5, fill=MUTED)
arrow(1130, 696, 1130, 774)                                         # Vault -> Crypt
arrow(1270, 818, 1284, 818)                                         # Crypt -> Base32

# ---- Third-party libraries ---------------------------------------------
band(890, "Third-party libraries")
for x, w, label, fx in [(40, 340, "libcurl · OpenSSL · c-ares", 210),
                        (540, 290, "SQLite3 · libpq", 685),
                        (990, 280, "libsodium", 1130)]:
    box(x, 902, w, 44)
    text(fx, 929, label, 13, anchor="middle")
arrow(210, 746, 210, 900)
arrow(685, 696, 685, 900)
arrow(1130, 860, 1130, 900)

# ---- Servers -------------------------------------------------------------
band(986, "Servers")
box(40, 998, 440, 64, fill=PAPER)
text(56, 1022, "Cloud storage and mail servers", 13.5, "bold")
text(56, 1043, "Nextcloud, WebDAV, Dropbox, OneDrive, Google, FTP; IMAP/SMTP/JMAP", 12)
box(540, 998, 290, 64, fill=PAPER)
text(556, 1022, "PostgreSQL server", 13.5, "bold")
text(556, 1043, "UltraFIBU multi-user; LAN or VPN, TLS", 12)
box(880, 998, 500, 64, dashed=True, fill=PAPER)
text(896, 1022, "ULTRA cloud (planned, not chosen yet)", 13.5, "bold", MUTED)
text(896, 1043, "CalDAV · CardDAV · WebDAV + one ULTRA account (OIDC), e.g. Nextcloud", 12, fill=MUTED)
arrow(210, 946, 210, 996, dashed=True)
arrow(685, 946, 685, 996, dashed=True)
text(224, 976, "over the network", 11, fill=MUTED, style="italic")
text(699, 976, "over the network", 11, fill=MUTED, style="italic")

add("</svg>")
import os
dest = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "Docs", "Modules", "ModuleMap.svg")
with open(dest, "w", encoding="utf-8") as f:
    f.write("\n".join(out) + "\n")
