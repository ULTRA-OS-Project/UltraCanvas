#!/usr/bin/env python3
"""check_notification_activators.py - every application that puts a
notification on screen also takes its clicks when the broker host is gone.

An application posts a `system.notification` on the UltraMessage bus, and the
broker's presenter shows it - on Windows as a toast under the application's
own AppUserModelID. While the program hosting the broker runs, a click comes
back over the bus. Once it has quit, the toast still waits in the Action
Center, and Windows hands a click on it to the application's COM activator,
which exists only when the application called
`UltraCanvas::RegisterNotificationActivator` (UltraCanvasNotificationActivation.h).
Without it the click does nothing - as for every application's toast until
UltraMail registered one in 0.10.65.

So: an application folder under Apps/ whose sources post a notification
(`UltraMsg_Post(..., UltraMsgTopics::SystemNotification, ...)`, also across
lines) must call `RegisterNotificationActivator(` somewhere in the same
folder. One that has a reason not to - sample traffic posted Silent, which no
presenter shows - says so in the posting file with
`// notification-activator-exempt: <why>`.

The MSIX package needs the same activators declared in its manifest, where
Windows reads a packaged application's activator from: so an application that
registers one must have its row in NOTIFICATION_ACTIVATORS in
scripts/make_msix_layout.py (its executable and the application ID it
registers under), a row there must name an application that registers one, and
the manifest's CLSID must be the one RegisterNotificationActivator derives -
pinned below to the value Tests/UltraMessage/test_adapters.cpp pins for C++.

  python3 scripts/check_notification_activators.py            # report, exit 1 on a finding
  python3 scripts/check_notification_activators.py --strict   # the same (CI)

Version: 1.1.0 - the MSIX manifest's activators (make_msix_layout.py) checked too
Version: 1.0.0
"""

import importlib.util
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APPS = os.path.join(ROOT, "Apps")
SOURCE_SUFFIXES = (".cpp", ".cc", ".cxx", ".mm", ".h", ".hpp")

# An UltraMsg_Post whose topic argument is the notification topic, the call
# possibly spanning lines.
POST_RE = re.compile(r"UltraMsg_Post\s*\([^;]*?UltraMsgTopics::SystemNotification\s*[,)]", re.S)
REGISTER_RE = re.compile(r"\bRegisterNotificationActivator\s*\(")
EXEMPT_RE = re.compile(r"//\s*notification-activator-exempt:\s*\S")
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"')
COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)


def code_of(text):
    """The text without comments and string literals - a snippet of example
    code shown in a label is not a post."""
    text = COMMENT_RE.sub(lambda m: "" if m.group(0).startswith("/*") else "", text)
    return STRING_RE.sub('""', text)


# NotificationActivatorClassId("org.ultraos.ultramail"), as the C++ test pins it.
PINNED_CLASS_ID = ("org.ultraos.ultramail", "A8386CF6-5A41-5DB0-A2CF-3E7B3312307E")


def msix_layout():
    spec = importlib.util.spec_from_file_location(
        "make_msix_layout", os.path.join(ROOT, "scripts", "make_msix_layout.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check_msix(registering, findings):
    """`registering`: the Apps/ folders that call RegisterNotificationActivator,
    with the text of their sources."""
    layout = msix_layout()
    table = layout.NOTIFICATION_ACTIVATORS
    app_id, class_id = PINNED_CLASS_ID
    if layout.notification_activator_class_id(app_id) != class_id:
        findings.append(
            f"scripts/make_msix_layout.py: notification_activator_class_id({app_id!r}) is no longer {class_id}, "
            "the CLSID UltraCanvas::NotificationActivatorClassId derives (Tests/UltraMessage/test_adapters.cpp) - "
            "the package would declare an activator no application serves")
    by_lower = {exe.lower(): exe for exe in table}
    for folder, text in sorted(registering.items()):
        exe = by_lower.get(folder.lower())
        if exe is None:
            findings.append(
                f"Apps/{folder}: registers a notification activator, but scripts/make_msix_layout.py has no "
                f"row for it in NOTIFICATION_ACTIVATORS - from the MSIX package a click on its toast after the "
                "broker host quit would reach nobody. Add its executable and the ID it registers under")
        elif f'"{table[exe]}"' not in text:
            findings.append(
                f"scripts/make_msix_layout.py: NOTIFICATION_ACTIVATORS gives {exe} the ID {table[exe]!r}, which "
                f"appears nowhere in Apps/{folder} - the manifest would declare another application's CLSID")
    for exe in sorted(table):
        if exe.lower() not in {folder.lower() for folder in registering}:
            findings.append(
                f"scripts/make_msix_layout.py: NOTIFICATION_ACTIVATORS lists {exe}, but Apps/{exe} registers no "
                "notification activator")


def main():
    findings = []
    registering = {}
    for app in sorted(os.listdir(APPS)):
        folder = os.path.join(APPS, app)
        if not os.path.isdir(folder):
            continue
        posting = []        # files that post a notification and are not exempt
        registers = False
        sources = []
        for directory, _, files in os.walk(folder):
            for name in files:
                if not name.endswith(SOURCE_SUFFIXES):
                    continue
                path = os.path.join(directory, name)
                with open(path, encoding="utf-8", errors="replace") as source:
                    text = source.read()
                sources.append(text)
                code = code_of(text)
                if REGISTER_RE.search(code):
                    registers = True
                if POST_RE.search(code) and not EXEMPT_RE.search(text):
                    posting.append(os.path.relpath(path, ROOT))
        if registers:
            registering[app] = "\n".join(sources)
        if posting and not registers:
            for path in posting:
                findings.append(
                    f"{path}: posts a system.notification, but {os.path.relpath(folder, ROOT)} never calls "
                    "UltraCanvas::RegisterNotificationActivator - a click on its Windows toast does nothing "
                    "once the broker host has quit. Register it at the start "
                    "(UltraCanvasNotificationActivation.h), or say why not with "
                    "// notification-activator-exempt: <why>")
    check_msix(registering, findings)
    for finding in findings:
        print(finding)
    if findings:
        print(f"check_notification_activators: {len(findings)} finding(s)")
        return 1
    print("check_notification_activators: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
