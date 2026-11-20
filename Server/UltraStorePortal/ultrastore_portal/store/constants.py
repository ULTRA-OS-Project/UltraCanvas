"""The fixed lists the catalog is written from - the same words
Apps/UltraStore/core/Catalog.cpp reads (Docs/UltraStore/README.md, *The
admin backend's settings*). A word added here that the native store does
not know is refused or ignored there, so the lists move together.

Version: 0.1.0
"""

from __future__ import annotations

import re

# The store's categories, in the order its browser shows them
# (StoreCategories(), Catalog.h).
CATEGORIES = [
    ("audio", "Music and audio"),
    ("video", "Video"),
    ("graphics", "Graphics and photos"),
    ("office", "Office"),
    ("communication", "Communication"),
    ("internet", "Internet"),
    ("development", "Developer tools"),
    ("education", "Education"),
    ("science", "Science"),
    ("finance", "Finance"),
    ("security", "Security"),
    ("games", "Games"),
    ("system", "System"),
    ("utilities", "Utilities"),
]
CATEGORY_NAMES = dict(CATEGORIES)

KINDS = [("app", "Application"), ("runtime", "Runtime"), ("library", "Library")]

OPERATING_SYSTEMS = [
    ("linux", "Linux"),
    ("windows", "Windows"),
    ("macos", "macOS"),
    ("ultraos", "ULTRA OS"),
    ("android", "Android"),
    ("ios", "iOS"),
    ("wasm", "WebAssembly"),
]
OS_NAMES = dict(OPERATING_SYSTEMS)

ARCHITECTURES = [
    ("x86_64", "x86_64 (Intel / AMD 64-bit)"),
    ("arm64", "arm64"),
    ("armv7", "armv7"),
    ("riscv64", "riscv64"),
    ("wasm32", "wasm32"),
    ("universal", "universal (a macOS fat binary)"),
    ("any", "any (no native code)"),
]

# The archive formats the native installer unpacks (Installer.cpp), by the
# file name's ending - the longest first, so .tar.gz is not read as .gz.
PACKAGE_FORMATS = [
    (".tar.gz", "tar.gz"),
    (".tar.xz", "tar.xz"),
    (".tar.zst", "tar.zst"),
    (".tar.bz2", "tar.bz2"),
    (".tgz", "tgz"),
    (".tar", "tar"),
    (".zip", "zip"),
    (".7z", "7z"),
    (".appimage", "appimage"),
]

CHANNELS = [("stable", "Stable"), ("beta", "Beta")]

PRICING_MODELS = [("free", "Free"), ("one-time", "One-time purchase"), ("monthly", "Monthly subscription"), ("yearly", "Yearly subscription")]
UPDATE_PERIODS = [("always", "Always"), ("never", "The bought version only"), ("until", "Until a date")]
VISIBILITIES = [
    ("listed", "Listed - everyone finds it"),
    ("unlisted", "Unlisted - reachable by its link only"),
    ("hidden", "Hidden - served only to machines that have it"),
]
LIFECYCLES = [
    ("active", "Active"),
    ("deprecated", "Deprecated - installs with a notice"),
    ("withdrawn", "Withdrawn - no new installs"),
]
AGE_RATINGS = [("", "Everyone"), ("3", "3+"), ("7", "7+"), ("12", "12+"), ("16", "16+"), ("18", "18+")]

REPORT_KINDS = [
    ("malware", "Malware or harmful behaviour"),
    ("broken", "Does not install or start"),
    ("misleading", "Misleading listing"),
    ("content", "Inappropriate content"),
    ("copyright", "Copyright or trademark"),
    ("other", "Something else"),
]

# Catalog.cpp, IsReverseDns: letters, digits, '.', '-', '_', at least one
# dot, none at an end. The portal is stricter about case and length.
APP_ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9_-]*(\.[a-z0-9][a-z0-9_-]*)+$")
VERSION_PATTERN = re.compile(r"^[0-9]+(\.[0-9]+){0,3}([a-z0-9.+-]*)$")
MIN_OS_PATTERN = re.compile(r"^[0-9]+(\.[0-9]+)*$")
HEX64_PATTERN = re.compile(r"^[0-9a-f]{64}$")


def package_format_of(file_name: str) -> str:
    """The format a file name names, or '' for one the store cannot unpack."""
    lower = file_name.lower()
    for ending, name in PACKAGE_FORMATS:
        if lower.endswith(ending):
            return name
    return ""
