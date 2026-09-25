#!/usr/bin/env python3
"""Generate Apps/UltraMail/engine build-tree header UltraMailOAuthDefaults.h.

Fills UltraMailOAuthDefaults.h.in with the OAuth client credentials to bake into
a shipped UltraMail build. When --xor-key is non-zero the values are
XOR-obfuscated so a plain `strings` on the binary does not surface them; this is
anti-scraping friction only, not confidentiality (see the .in template and
UltraMailOAuth.h). CMake invokes this only when at least one credential is
non-empty and obfuscation is on; the empty/plain cases go through configure_file.

Credentials are read from the environment (UM_GOOGLE_ID, UM_GOOGLE_SECRET,
UM_YAHOO_ID, UM_MICROSOFT_ID) so they never appear in a process listing.
"""
import argparse
import os
import sys


def c_literal(value: str, key: int) -> str:
    """A C string-literal token for `value`, XOR-obfuscated against `key`.

    Each byte is emitted as its own "\\xNN" literal so adjacent-literal
    concatenation stops the hex escape from greedily eating the next byte. The
    key must not equal any input byte (that would yield a NUL and truncate the
    C string); ASCII input with a high-bit key (>=128) guarantees this.
    """
    if not value:
        return '""'
    data = value.encode("utf-8")
    if key == 0:
        # Plain: values here are OAuth ids/secrets, [A-Za-z0-9._-] only, so no
        # C-escaping is needed; guard the two characters that would matter.
        escaped = value.replace("\\", "\\\\").replace('"', '\\"')
        return f'"{escaped}"'
    out = []
    for b in data:
        x = b ^ key
        if x == 0:
            sys.exit(
                f"generate_oauth_defaults: byte 0x{b:02x} XOR key 0x{key:02x} is "
                f"NUL; choose a different --xor-key (a value >=128 works for "
                f"ASCII credentials)."
            )
        out.append(f'"\\x{x:02x}"')
    return " ".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--template", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--xor-key", type=int, default=0)
    args = ap.parse_args()

    key = args.xor_key & 0xFF
    with open(args.template, "r", encoding="utf-8") as f:
        text = f.read()

    subs = {
        "@UM_GOOGLE_ID_C@":     c_literal(os.environ.get("UM_GOOGLE_ID", ""), key),
        "@UM_GOOGLE_SECRET_C@": c_literal(os.environ.get("UM_GOOGLE_SECRET", ""), key),
        "@UM_YAHOO_ID_C@":      c_literal(os.environ.get("UM_YAHOO_ID", ""), key),
        "@UM_MICROSOFT_ID_C@":  c_literal(os.environ.get("UM_MICROSOFT_ID", ""), key),
        "@UM_OBFUSCATED@":      "1" if key else "0",
        "@UM_XOR_KEY@":         str(key),
    }
    for placeholder, value in subs.items():
        text = text.replace(placeholder, value)

    with open(args.out, "w", encoding="utf-8") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
