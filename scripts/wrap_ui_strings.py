#!/usr/bin/env python3
"""Wrap the interface strings of a source file in Tr(...), for review.

The migration tool of Docs/UltraCanvas/UltraCanvasLocalisationProposal.md
§8: for each file named, the string literal in the text-taking parameter
of every known entry point is wrapped in Tr(...), every other literal is
left alone, and the file is rewritten (or, with --diff, the change is
printed and nothing is written). The person migrating the file reads the
result: a literal that is data rather than text (a file name that happens
to be capitalised, a format token) is unwrapped by hand, and a sentence
built from pieces ("Could not read " + name) is rewritten as TrF by hand.
Never a CI check, never unattended.

    python3 scripts/wrap_ui_strings.py --diff UltraCanvas/core/UltraCanvasModalDialog.cpp
    python3 scripts/wrap_ui_strings.py Apps/UltraFiler/*.cpp

What counts as an entry point is the table below (the proposal's). A
literal is wrapped only when it looks like interface text: it starts with
a letter or a brace, and is not an identifier shape (one camelCase or
snake_case word, a path, a format token). A line that already calls Tr,
a #include, a comment and a stream line (debugOutput, std::cerr) are
left alone. The include of UltraLocalisation/UltraLocalisation.h is added
when a file gained its first Tr.
"""
import argparse
import difflib
import re
import sys
from pathlib import Path

# Member calls whose first argument is the text
TEXT_METHODS = ["SetText", "SetTitle", "SetTooltip", "SetPlaceholder", "SetPlaceholderText", "SetLabel",
                "SetWindowTitle", "SetStatus", "SetStatusText", "SetMessage", "SetCaption", "SetHint",
                "SetHeaderText", "SetAccessibleName", "SetAccessibleDescription", "SetDescription",
                "SetPrompt", "SetEmptyText", "SetGroupTitle", "SetButtonText"]
# Free or static calls whose first (and second, where noted) argument is text
TEXT_CALLS = {"ShowMessage": 2, "ShowInformation": 2, "ShowQuestion": 2, "ShowWarning": 2, "ShowError": 2,
              "ShowConfirmation": 2, "ShowInputDialog": 2, "ShowSimple": 2, "AddColumn": 1, "ListColumnDef": 1,
              "FileFilter": 1, "AddTab": 1, "AddPage": 1, "AddRow": 1, "AddOption": 1, "AddHeader": 1,
              "AddAction": 1, "AddSection": 1, "AddGroup": 1, "SetTitleText": 1}
# MenuItemData builders: the label is the first argument
MENU_BUILDERS = ["Action", "Checkbox", "Header", "Submenu", "Radio"]
# Factories and element constructors: the text is any literal argument after
# the first (the identifier)
FACTORIES = ["CreateButton", "CreateLabel", "CreateCheckbox", "CreateRadio", "CreateSwitch", "CreateGroupBox",
             "CreateMenuItem", "CreateToggle", "CreateLink", "CreateChip", "CreateBadge", "TreeNodeData"]
ELEMENT_CTORS = ["UltraCanvasButton", "UltraCanvasLabel", "UltraCanvasCheckbox", "UltraCanvasRadio",
                 "UltraCanvasGroupBox", "UltraCanvasSwitch", "UltraCanvasChip", "UltraCanvasBadge",
                 "UltraCanvasLink"]
# Member assignments of text: config.title = "..."
TEXT_MEMBERS = ["title", "message", "text", "label", "tooltip", "placeholder", "caption", "header", "okText",
                "cancelText", "yesText", "noText", "prompt", "hint", "description", "statusText", "emptyText",
                "buttonText", "windowTitle", "subtitle", "heading", "detail", "details"]

LITERAL = r'"((?:[^"\\\n]|\\.)*)"'
INCLUDE_LINE = '#include "UltraLocalisation/UltraLocalisation.h"'


def looks_like_text(s: str) -> bool:
    if len(s) < 2:
        return False
    if not (s[0].isalpha() or s[0] == "{"):
        return False
    if re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", s) and (s[0].islower() or (s[1:2].isupper() and len(s) < 4)):
        return False                                   # camelCase identifier, two-letter code
    if re.fullmatch(r"[A-Za-z0-9_./:\\-]+", s) and ("_" in s or "/" in s or "\\" in s or (("." in s) and not s.endswith("..."))):
        return False                                   # an identifier, a path, a file name
    if re.fullmatch(r"[A-Z][a-z]*(?:[A-Z][a-z0-9]*)+", s):
        return False                                   # PascalCase identifier: an element id
    if s.startswith(("#", "%", "<", "http", "mailto:")):
        return False
    return True


def wrap_match(args: str, positions, skip_first=False):
    """Wrap the literal(s) in the given argument positions (0-based) of an
    argument list; positions None means every literal after the first."""
    out = []
    depth = 0
    i = 0
    index = 0
    start = 0
    parts = []
    # split top-level arguments
    while i < len(args):
        c = args[i]
        if c == '"':
            j = i + 1
            while j < len(args) and args[j] != '"':
                if args[j] == "\\":
                    j += 1
                j += 1
            i = j + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            parts.append(args[start:i])
            start = i + 1
        i += 1
    parts.append(args[start:])
    changed = False
    for k, part in enumerate(parts):
        wanted = (positions is None and k > 0) or (positions is not None and k in positions)
        if skip_first and k == 0:
            wanted = False
        if wanted:
            m = re.fullmatch(r"(\s*)" + LITERAL + r"(\s*)", part)
            if m and looks_like_text(m.group(2)):
                parts[k] = m.group(1) + 'Tr("' + m.group(2) + '")' + m.group(3)
                changed = True
    return ",".join(parts), changed


def find_call_args(text: str, open_paren: int):
    """The argument text between the parenthesis at open_paren and its match."""
    depth = 0
    i = open_paren
    while i < len(text):
        c = text[i]
        if c == '"':
            j = i + 1
            while j < len(text) and text[j] != '"':
                if text[j] == "\\":
                    j += 1
                j += 1
            i = j + 1
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[open_paren + 1:i], i
        i += 1
    return None, None


def wrap_line_assignments(line: str) -> str:
    def repl(m):
        if looks_like_text(m.group(3)):
            return m.group(1) + 'Tr("' + m.group(3) + '")'
        return m.group(0)
    pattern = r"(\b(?:" + "|".join(TEXT_MEMBERS) + r")\s*=\s*)" + r"()" + LITERAL
    return re.sub(pattern, repl, line)


def wrap_text(text: str) -> str:
    lines = text.split("\n")
    out_lines = []
    for line in lines:
        stripped = line.lstrip()
        if (stripped.startswith(("#", "//", "*")) or "debugOutput" in line or "std::cerr" in line or "std::cout" in line
                or "<<" in line or "Tr(" in line or "TrC(" in line or "TrF(" in line or "TrN(" in line):
            out_lines.append(line)
            continue
        out_lines.append(wrap_line_assignments(line))
    text = "\n".join(out_lines)

    # Calls: scan for each known name followed by '('
    names = ([(m, "method") for m in TEXT_METHODS] + [(c, "call") for c in TEXT_CALLS] +
             [("MenuItemData::" + b, "menu") for b in MENU_BUILDERS] + [(f, "factory") for f in FACTORIES] +
             [("make_shared<" + e + ">", "factory") for e in ELEMENT_CTORS] + [(e, "factory") for e in ELEMENT_CTORS])
    for name, kind in names:
        pattern = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"\s*\(")
        pos = 0
        while True:
            m = pattern.search(text, pos)
            if not m:
                break
            open_paren = m.end() - 1
            line_start = text.rfind("\n", 0, m.start()) + 1
            line = text[line_start:text.find("\n", m.start()) if text.find("\n", m.start()) != -1 else len(text)]
            if line.lstrip().startswith(("#", "//")) or "<<" in line:
                pos = m.end()
                continue
            args, close = find_call_args(text, open_paren)
            if args is None:
                pos = m.end()
                continue
            if kind == "method":
                new_args, changed = wrap_match(args, {0})
            elif kind == "call":
                new_args, changed = wrap_match(args, set(range(TEXT_CALLS[name])))
            elif kind == "menu":
                new_args, changed = wrap_match(args, {0})
            else:
                new_args, changed = wrap_match(args, None)
            if changed:
                text = text[:open_paren + 1] + new_args + text[close:]
            pos = open_paren + 1 + len(new_args)
    return text


def add_include(text: str) -> str:
    if INCLUDE_LINE in text:
        return text
    lines = text.split("\n")
    last_include = -1
    for i, line in enumerate(lines):
        if line.startswith('#include "'):
            last_include = i
    if last_include == -1:
        for i, line in enumerate(lines):
            if line.startswith("#include <"):
                last_include = i
    if last_include == -1:
        return INCLUDE_LINE + "\n" + text
    lines.insert(last_include + 1, INCLUDE_LINE)
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("--diff", action="store_true", help="print the change and write nothing")
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()
    total = 0
    for path in args.paths:
        before = path.read_text(encoding="utf-8")
        after = wrap_text(before)
        if after != before:
            after = add_include(after)
        count = after.count('Tr("') - before.count('Tr("')
        total += count
        if args.diff:
            sys.stdout.writelines(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                                       str(path), str(path) + " (wrapped)"))
        elif after != before:
            path.write_text(after, encoding="utf-8")
        print("%s: %d string(s) wrapped" % (path, count), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
