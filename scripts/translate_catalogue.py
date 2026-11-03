#!/usr/bin/env python3
"""Draft a language's catalogues with UltraAI, through ultralocalisation-draft.

Docs/UltraCanvas/UltraCanvasLocalisationProposal.md §8. For a language
tag, runs the ultralocalisation-draft tool (Apps/UltraLocalisationDraft,
built when the UltraAI module is) over the English source catalogues -
the standard set first, since every application inherits its quality,
then the base sets, then each application's set - and writes the drafts
under media/locale/<lang>/, each marked "@translated-by: UltraAI draft
<date> ..., reviewed by -" until a person changes it. Entries a target
already has are kept; --redo drafts them again.

    python3 scripts/translate_catalogue.py de
    python3 scripts/translate_catalogue.py de fr es --sets Standard base/Video UltraMail
    python3 scripts/translate_catalogue.py ja --provider anthropic --api-key-env ANTHROPIC_API_KEY
    python3 scripts/translate_catalogue.py de --dry-run        # what would be sent, nothing written

The provider is whatever UltraAI is configured with (--provider names
one, --api-key-env / --vault-ref give it a key, --model a model); without
any of them the UltraAI route decides, local models first. The tool is
looked for in build/bin and beside this script's build directory, or
named with --tool. After drafting, run scripts/check_locale_catalogues.py.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOCALE = ROOT / "media" / "locale"

CONTEXTS = {
    "Standard": "every desktop application of the ULTRA OS suite",
    "base/Computer": "a system tool: files, drives, devices, processes, settings",
    "base/Web": "a web browser and a website builder",
    "base/Audio": "an audio editor",
    "base/Image": "an image editor",
    "base/Video": "a video editor",
    "base/Text": "a text editor and word processor",
    "base/Spreadsheet": "a spreadsheet",
    "base/Presentation": "a slide presentation program",
    "base/Planning": "a task, calendar and project planner",
    "base/CAD": "a technical drawing (CAD) program",
    "base/Banking": "a banking and bookkeeping program",
    "base/Games": "a game",
    "base/Shopping": "an online shop",
    "base/Fashion": "a fashion and clothing shop",
    "base/Communication": "a mail, chat and social media client",
    "base/Security": "a password manager and authenticator",
}


def find_tool(named):
    if named:
        return Path(named)
    exe = "ultralocalisation-draft.exe" if os.name == "nt" else "ultralocalisation-draft"
    for folder in (ROOT / "build" / "bin", ROOT / "build", ROOT / "cmake-build-debug" / "bin"):
        if (folder / exe).exists():
            return folder / exe
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("languages", nargs="+", help="language tags: de, fr, pt-BR, zh-Hans ...")
    parser.add_argument("--sets", nargs="*", help="which sets (Standard, base/Video, UltraMail ...); default: all")
    parser.add_argument("--provider", default="")
    parser.add_argument("--model", default="")
    parser.add_argument("--api-key-env", default="")
    parser.add_argument("--vault-ref", default="")
    parser.add_argument("--batch", type=int, default=40)
    parser.add_argument("--redo", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--tool", help="the ultralocalisation-draft executable")
    args = parser.parse_args()

    tool = find_tool(args.tool)
    if tool is None or not tool.exists():
        print("translate_catalogue: ultralocalisation-draft not found; build it (needs the UltraAI module) "
              "or name it with --tool", file=sys.stderr)
        return 2

    en = LOCALE / "en"
    sets = args.sets
    if not sets:
        sets = ["Standard"]
        sets += sorted("base/" + p.stem for p in (en / "base").glob("*.json")) if (en / "base").exists() else []
        sets += sorted(p.stem for p in en.glob("*.json") if p.stem != "Standard")

    failed = 0
    for language in args.languages:
        for name in sets:
            source = en / (name + ".json")
            if not source.exists():
                print("translate_catalogue: no source catalogue %s" % source.relative_to(ROOT), file=sys.stderr)
                failed += 1
                continue
            target = LOCALE / language / (name + ".json")
            cmd = [str(tool), "--language", language, "--source", str(source), "--target", str(target),
                   "--batch", str(args.batch)]
            context = CONTEXTS.get(name, "the application " + name)
            cmd += ["--context", context]
            for flag, value in (("--provider", args.provider), ("--model", args.model),
                                ("--api-key-env", args.api_key_env), ("--vault-ref", args.vault_ref)):
                if value:
                    cmd += [flag, value]
            if args.redo:
                cmd.append("--redo")
            if args.dry_run:
                cmd.append("--dry-run")
            print("== %s / %s" % (language, name))
            result = subprocess.run(cmd)
            if result.returncode != 0:
                failed += 1
    if failed:
        print("translate_catalogue: %d set(s) failed" % failed, file=sys.stderr)
        return 1
    print("translate_catalogue: done; now run python3 scripts/check_locale_catalogues.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
