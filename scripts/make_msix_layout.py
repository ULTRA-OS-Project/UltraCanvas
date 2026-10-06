#!/usr/bin/env python3
"""Turn the Windows dist/ folder into an MSIX package layout.

package-win.sh builds dist/: every application's .exe beside one shared set of
DLLs, plus Resources/, lib/, Plugins/ and data/. This script copies that folder
into a package layout and adds what an MSIX needs on top of it:

  AppxManifest.xml   the package identity, and one <Application> - one Start
                     menu entry - for every application in APPS whose .exe is
                     in dist/
  Assets/            the Start menu, taskbar and Store logos, rendered from
                     media/appicon/ with ImageMagick

It also writes a second, small folder (--pri-root) holding only the manifest
and Assets/, for makepri to index. package-win-msix.sh runs makepri there,
copies the resources.pri it makes into the layout, and packs the layout with
makeappx. This script runs no Windows SDK tool itself, so it runs (and is
checked) on Linux too.

One package for the whole suite, the way dist/ and the macOS suite folder are
one: the applications share hundreds of DLLs, and a package per
application would carry its own copy of all of them.

A new application gets a row in APPS below. That is the whole job: its .exe is
already in dist/ (package-win.sh copies every one), and its icon is the one
CMake embeds (media/appicon/, ultracanvas_embed_app_icon).
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The Publisher suffix that lets Windows 11 install a package that is not
# signed (Add-AppxPackage -AllowUnsigned, from an elevated PowerShell). For
# testing only; Microsoft documents it as "Create an unsigned MSIX package".
UNSIGNED_PUBLISHER_OID = "OID.2.25.311729368913984317654407730594956997722=1"


@dataclass
class FileTypes:
    """A group of file types an application opens (Explorer's "Open with")."""
    name: str                 # lower case [-_.a-z0-9]; unique in the package
    display_name: str
    extensions: List[str]


@dataclass
class App:
    exe: str                  # file name in dist/, without .exe
    display_name: str         # Start menu name
    icon: str                 # file in media/appicon/
    description: str
    file_types: List[FileTypes] = field(default_factory=list)

    @property
    def app_id(self) -> str:
        # Application Id: letters and digits, starting with a letter
        # (ST_AsciiWindowsId); "UOS-Settings" would not be one.
        return re.sub(r"[^A-Za-z0-9]", "", self.exe)


# Windows refuses a package that claims one of its reserved file types
# (.bat, .cmd, .js, .ps1, .reg, .vbs, ... - the ones that run code when
# opened), so those are not here even where Texter edits them.
TEXT_TYPES = FileTypes("ultracanvas.text", "Text and source code", [
    ".txt", ".text", ".log", ".md", ".markdown", ".ini", ".cfg", ".conf",
    ".json", ".xml", ".yaml", ".yml", ".toml", ".csv",
    ".c", ".h", ".cpp", ".hpp", ".cc", ".cxx", ".hxx", ".m", ".mm",
    ".py", ".ts", ".java", ".cs", ".rs", ".go", ".sh",
    ".cmake", ".css", ".html", ".htm",
])

MEDIA_TYPES = FileTypes("ultracanvas.media", "Images, video, audio and documents", [
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tif", ".tiff", ".webp",
    ".heic", ".heif", ".avif", ".ico", ".tga", ".psd", ".svg",
    ".mp4", ".mov", ".mkv", ".webm", ".avi",
    ".mp3", ".wav", ".flac", ".ogg", ".opus", ".m4a",
    ".pdf", ".epub", ".ucd",
])

# Every application that gets a Start menu entry. An .exe in dist/ that is
# not listed here (the command-line tools, the ULTRA OS desktop components)
# still ships in the package; it just has no entry. A listed .exe that this
# build did not produce is skipped, as package-macos.sh's package_if_built
# does.
APPS = [
    App("UltraCanvasDemo", "UltraCanvas Demo", "Demo.png",
        "Every UltraCanvas element in one window, with its source code"),
    App("Texter", "UltraCanvas Texter", "Texter.png",
        "Text and source code editor", [TEXT_TYPES]),
    App("UltraFiler", "UltraFiler", "UltraFiler.png",
        "File manager with folder tree and media preview"),
    App("UltraViewer", "UltraViewer", "UltraViewer.png",
        "Viewer for images, video, audio, PDF and e-books", [MEDIA_TYPES]),
    App("UltraPaint", "UltraPaint", "UltraPaint.png", "Bitmap editor"),
    App("ArtCreator", "ArtCreator", "ArtCreator.png", "Vector drawing editor"),
    App("UltraAIApp", "UltraAI", "UltraAI.png",
        "AI capabilities: chat, speech, image, video and music generation"),
    App("UltraMail", "UltraMail", "UltraMail.png", "Mail client"),
    App("EmailCleaner", "EmailCleaner", "EmailCleaner.png",
        "Maps who sends what, and when, across several mail accounts"),
    App("UltraCleaner", "UltraCleaner", "UltraCleaner.png",
        "Removes temporary files, caches, logs and other leftovers"),
    App("UltraClaude", "UltraClaude", "UltraClaude.png",
        "Chat with Claude through the Claude Code command line"),
    App("UltraNetMonitor", "UltraNetMonitor", "UltraNetMonitor.png",
        "Which processes hold which network connections, live"),
    App("DeviceExplorer", "DeviceExplorer", "DeviceExplorer.png",
        "Printers, scanners, cameras and other connected devices"),
    App("UltraAuthenticator", "UltraAuthenticator", "UltraAuthenticator.png",
        "Two-factor authentication codes (TOTP and HOTP)"),
    App("UltraPassword", "UltraPassword", "UltraPassword.png", "Password vault"),
    App("UltraClipboard", "UltraClipboard", "UltraClipboard.png",
        "Clipboard history: texts, links, colours, images and files"),
    App("ultrafibu", "UltraFIBU", "UltraFIBU.png",
        "Double-entry accounting for Germany: DATEV, UStVA, ELSTER"),
    App("UltraCanvasStart", "UltraCanvasStart", "UltraCanvasStart.png",
        "Sets this computer up for UltraCanvas development"),
]

# dist/ files that do not belong in the package. uc-diagnose starts an .exe
# from its own folder, which a packaged application's folder
# (C:\Program Files\WindowsApps\...) does not allow.
EXCLUDED_FILES = {"uc-diagnose.bat", "uc-diagnose.ps1"}

# Names makeappx writes itself; a file of that name in the layout is an error.
RESERVED_NAMES = {"appxmanifest.xml", "appxblockmap.xml", "appxsignature.p7x",
                  "[content_types].xml", "appxmetadata", "resources.pri"}

# Square44x44Logo is the icon of the Start menu's list, the taskbar, the
# title bar and Explorer. The targetsize-* files are drawn at exactly that
# size; the altform-unplated ones are drawn without the accent-coloured plate
# behind them, which is how a desktop application's icon should look. The
# scale-* files serve the 100% to 400% display scales.
TARGET_SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
SCALES = [100, 150, 200, 400]

# makepri's configuration: MakePri.exe createconfig's default, less its
# <packaging> element. With that element makepri splits the index into
# resources.pri plus resources.scale-200.pri and the like (resource packs for
# a bundle), and the logos' scale variants would not be in the one file the
# package carries.
PRI_CONFIG = """<?xml version="1.0" encoding="utf-8"?>
<resources targetOsVersion="10.0.0" majorVersion="1">
  <index root="\\" startIndexAt="\\">
    <default>
      <qualifier name="Language" value="en-US"/>
      <qualifier name="Contrast" value="standard"/>
      <qualifier name="Scale" value="100"/>
      <qualifier name="HomeRegion" value="001"/>
      <qualifier name="TargetSize" value="256"/>
      <qualifier name="LayoutDirection" value="LTR"/>
      <qualifier name="Theme" value="dark"/>
      <qualifier name="AlternateForm" value=""/>
      <qualifier name="DXFeatureLevel" value="DX9"/>
      <qualifier name="Configuration" value=""/>
      <qualifier name="DeviceFamily" value="Universal"/>
      <qualifier name="Custom" value=""/>
    </default>
    <indexer-config type="folder" foldernameAsQualifier="true" filenameAsQualifier="true" qualifierDelimiter="."/>
  </index>
</resources>
"""


def fail(message: str) -> None:
    print(f"Error: {message}", file=sys.stderr)
    sys.exit(1)


def msix_version(version: str) -> str:
    """x.y.z (the changelog's) -> x.y.z.0, MSIX's four-part version.

    The Store requires the fourth part to be 0; it reserves it.
    """
    parts = version.split(".")
    if len(parts) == 3:
        parts.append("0")
    if len(parts) != 4 or not all(p.isdigit() and int(p) <= 65535 for p in parts):
        fail(f"version '{version}' is not x.y.z with each part 0..65535")
    return ".".join(str(int(p)) for p in parts)


def find_imagemagick(explicit: Optional[str]) -> List[str]:
    if explicit:
        return [explicit]
    for name in ("magick", "convert"):
        path = shutil.which(name)
        # Windows has its own convert.exe (FAT to NTFS); it is not this one.
        if path and not (name == "convert" and "system32" in path.lower()):
            return [path]
    fail("ImageMagick (magick or convert) is needed to render the logos")
    return []


def render_pngs(magick: List[str], source: str,
                outputs: List[Tuple[str, int, float]]) -> None:
    """Render `source` centred on transparent squares, one per output.

    Each output is (path, size, fill): `fill` is the share of the square the
    icon may take. The icons are not all square (Demo.png is 847x917), so they
    are fitted, not stretched. One ImageMagick run per icon, not per file:
    process start-up is what costs on Windows.
    """
    cmd = magick + [source, "-strip", "-background", "none"]
    for path, size, fill in outputs:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        inner = max(1, round(size * fill))
        cmd += ["(", "+clone", "-resize", f"{inner}x{inner}",
                "-gravity", "center", "-extent", f"{size}x{size}",
                "-write", f"PNG32:{path}", "+delete", ")"]
    cmd += ["null:"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    missing = [path for path, _, _ in outputs if not os.path.isfile(path)]
    if result.returncode != 0 or missing:
        fail(f"could not render the logos from {source}: "
             f"{result.stderr.strip() or ', '.join(missing)}")


def render_app_assets(magick: List[str], icon: str, folder: str) -> None:
    """Square150x150Logo and Square44x44Logo for one application."""
    outputs = []
    for scale in SCALES:
        # The 150 tile keeps a margin around the icon, as Start draws it.
        outputs.append((os.path.join(folder, f"Square150x150Logo.scale-{scale}.png"),
                        150 * scale // 100, 0.66))
        outputs.append((os.path.join(folder, f"Square44x44Logo.scale-{scale}.png"),
                        44 * scale // 100, 1.0))
    for size in TARGET_SIZES:
        for qualifier in (f"targetsize-{size}", f"targetsize-{size}_altform-unplated"):
            outputs.append((os.path.join(folder, f"Square44x44Logo.{qualifier}.png"),
                            size, 1.0))
    render_pngs(magick, icon, outputs)


def render_package_assets(magick: List[str], icon: str, assets: str) -> None:
    """StoreLogo: the package's own logo (App Installer, Settings > Apps)."""
    render_pngs(magick, icon, [
        (os.path.join(assets, f"StoreLogo.scale-{scale}.png"), 50 * scale // 100, 1.0)
        for scale in SCALES])


def copy_dist(dist: str, layout: str) -> None:
    if os.path.exists(layout):
        shutil.rmtree(layout)

    def ignore(directory: str, names: List[str]) -> List[str]:
        if os.path.abspath(directory) != os.path.abspath(dist):
            return []
        return [n for n in names if n in EXCLUDED_FILES]

    shutil.copytree(dist, layout, ignore=ignore)
    for name in os.listdir(layout):
        if name.lower() in RESERVED_NAMES or name.lower() == "assets":
            fail(f"dist/ already holds '{name}', a name the package layout "
                 "uses itself")


def build_manifest(apps: List[App], identity_name: str, publisher: str,
                   publisher_display_name: str, display_name: str,
                   description: str, version: str, arch: str,
                   min_version: str, max_version_tested: str) -> ET.ElementTree:
    ns = {
        "": "http://schemas.microsoft.com/appx/manifest/foundation/windows10",
        "uap": "http://schemas.microsoft.com/appx/manifest/uap/windows10",
        "uap3": "http://schemas.microsoft.com/appx/manifest/uap/windows10/3",
        "rescap": "http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities",
    }
    for prefix, uri in ns.items():
        ET.register_namespace(prefix, uri)

    def q(tag: str) -> str:
        prefix, _, local = tag.rpartition(":")
        return f"{{{ns[prefix]}}}{local}"

    package = ET.Element(q("Package"), {"IgnorableNamespaces": "uap uap3 rescap"})
    ET.SubElement(package, q("Identity"), {
        "Name": identity_name,
        "Publisher": publisher,
        "Version": version,
        "ProcessorArchitecture": arch,
    })
    properties = ET.SubElement(package, q("Properties"))
    ET.SubElement(properties, q("DisplayName")).text = display_name
    ET.SubElement(properties, q("PublisherDisplayName")).text = publisher_display_name
    ET.SubElement(properties, q("Description")).text = description
    ET.SubElement(properties, q("Logo")).text = "Assets\\StoreLogo.png"

    dependencies = ET.SubElement(package, q("Dependencies"))
    ET.SubElement(dependencies, q("TargetDeviceFamily"), {
        "Name": "Windows.Desktop",
        "MinVersion": min_version,
        "MaxVersionTested": max_version_tested,
    })
    resources = ET.SubElement(package, q("Resources"))
    ET.SubElement(resources, q("Resource"), {"Language": "en-us"})

    applications = ET.SubElement(package, q("Applications"))
    for app in apps:
        application = ET.SubElement(applications, q("Application"), {
            "Id": app.app_id,
            "Executable": f"{app.exe}.exe",
            "EntryPoint": "Windows.FullTrustApplication",
        })
        ET.SubElement(application, q("uap:VisualElements"), {
            "DisplayName": app.display_name,
            "Description": app.description,
            "BackgroundColor": "transparent",
            "Square150x150Logo": f"Assets\\{app.app_id}\\Square150x150Logo.png",
            "Square44x44Logo": f"Assets\\{app.app_id}\\Square44x44Logo.png",
        })
        if app.file_types:
            extensions = ET.SubElement(application, q("Extensions"))
            for types in app.file_types:
                extension = ET.SubElement(extensions, q("uap:Extension"), {
                    "Category": "windows.fileTypeAssociation"})
                # A packaged desktop application is started with the file as
                # its first argument, as from a double-click without a package.
                association = ET.SubElement(extension, q("uap3:FileTypeAssociation"), {
                    "Name": types.name,
                    "Parameters": "\"%1\""})
                ET.SubElement(association, q("uap:DisplayName")).text = types.display_name
                supported = ET.SubElement(association, q("uap:SupportedFileTypes"))
                for ext in types.extensions:
                    ET.SubElement(supported, q("uap:FileType")).text = ext

    capabilities = ET.SubElement(package, q("Capabilities"))
    # Every application here is a plain Win32 program, not a sandboxed UWP
    # one. runFullTrust is a restricted capability: a Store submission says
    # why it needs it (it is a desktop application).
    ET.SubElement(capabilities, q("rescap:Capability"), {"Name": "runFullTrust"})

    ET.indent(package, space="  ")
    return ET.ElementTree(package)


def check_unique(apps: List[App]) -> None:
    seen = {}
    for app in apps:
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", app.app_id) or len(app.app_id) > 64:
            fail(f"'{app.exe}' gives no valid Application Id ('{app.app_id}')")
        other = seen.setdefault(app.app_id.lower(), app.exe)
        if other != app.exe:
            fail(f"'{app.exe}' and '{other}' give the same Application Id")
    names = [t.name for app in apps for t in app.file_types]
    if len(names) != len(set(names)):
        fail("two file type groups share a name")
    extensions = [e for app in apps for t in app.file_types for e in t.extensions]
    duplicates = sorted({e for e in extensions if extensions.count(e) > 1})
    if duplicates:
        fail(f"file types claimed twice: {', '.join(duplicates)}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--dist", default="dist", help="package-win.sh's output folder")
    parser.add_argument("--layout", required=True, help="package layout to create (replaced)")
    parser.add_argument("--pri-root", required=True,
                        help="folder to create for makepri: the manifest and Assets/ only")
    parser.add_argument("--pri-config", required=True,
                        help="makepri configuration file to write (outside --pri-root)")
    parser.add_argument("--version", required=True, help="x.y.z, from the changelog")
    parser.add_argument("--arch", required=True, choices=["x64", "arm64", "x86"])
    parser.add_argument("--identity-name", required=True)
    parser.add_argument("--publisher", required=True,
                        help="the signing certificate's subject, or Partner Center's")
    parser.add_argument("--publisher-display-name", required=True)
    parser.add_argument("--display-name", default="UltraCanvas")
    parser.add_argument("--description",
                        default="UltraCanvas applications: editors, viewers, mail, "
                                "file management and utilities")
    parser.add_argument("--unsigned-test", action="store_true",
                        help=f"add {UNSIGNED_PUBLISHER_OID} to the Publisher, so Windows 11 "
                             "installs the unsigned package with Add-AppxPackage -AllowUnsigned")
    parser.add_argument("--min-version", default="10.0.17763.0")
    parser.add_argument("--max-version-tested", default="10.0.26100.0")
    parser.add_argument("--package-icon", default="UltraCanvas.png",
                        help="file in media/appicon/ for the package's StoreLogo")
    parser.add_argument("--imagemagick", help="path to magick/convert")
    args = parser.parse_args()

    if not os.path.isdir(args.dist):
        fail(f"{args.dist}/ not found - run package-win.sh first")
    if not re.fullmatch(r"[-.A-Za-z0-9]{3,50}", args.identity_name):
        fail(f"identity name '{args.identity_name}' must be 3-50 letters, digits, '.' or '-'")
    publisher = args.publisher.strip()
    if not publisher.startswith("CN="):
        fail(f"publisher '{publisher}' must be a certificate subject (CN=...)")
    if args.unsigned_test and UNSIGNED_PUBLISHER_OID not in publisher:
        publisher = f"{publisher}, {UNSIGNED_PUBLISHER_OID}"
    version = msix_version(args.version)

    apps = []
    for app in APPS:
        if os.path.isfile(os.path.join(args.dist, f"{app.exe}.exe")):
            apps.append(app)
        else:
            print(f"  Not built, so no Start menu entry: {app.exe}")
    if not apps:
        fail(f"none of the applications in APPS is in {args.dist}/")
    check_unique(apps)

    print(f"Copying {args.dist}/ to {args.layout}/")
    copy_dist(args.dist, args.layout)

    magick = find_imagemagick(args.imagemagick)
    assets = os.path.join(args.layout, "Assets")
    appicon_dir = os.path.join(REPO_ROOT, "media", "appicon")
    package_icon = os.path.join(appicon_dir, args.package_icon)
    if not os.path.isfile(package_icon):
        fail(f"package icon {package_icon} not found")
    render_package_assets(magick, package_icon, assets)
    for app in apps:
        icon = os.path.join(appicon_dir, app.icon)
        if not os.path.isfile(icon):
            fail(f"icon {icon} for {app.exe} not found")
        render_app_assets(magick, icon, os.path.join(assets, app.app_id))
        print(f"  {app.display_name}: {app.exe}.exe, Id {app.app_id}")

    manifest = build_manifest(
        apps, args.identity_name, publisher, args.publisher_display_name,
        args.display_name, args.description, version, args.arch,
        args.min_version, args.max_version_tested)
    manifest_path = os.path.join(args.layout, "AppxManifest.xml")
    manifest.write(manifest_path, encoding="utf-8", xml_declaration=True)

    # makepri indexes everything under the folder it is given, and folder
    # names such as "en" or "scale-100" are read as qualifiers - so it gets a
    # folder of its own with only what the resources.pri is about.
    if os.path.exists(args.pri_root):
        shutil.rmtree(args.pri_root)
    shutil.copytree(assets, os.path.join(args.pri_root, "Assets"))
    shutil.copy2(manifest_path, os.path.join(args.pri_root, "AppxManifest.xml"))
    with open(args.pri_config, "w", encoding="utf-8", newline="\r\n") as config:
        config.write(PRI_CONFIG)

    print(f"Wrote {manifest_path}: {len(apps)} applications, version {version}, "
          f"{args.arch}, publisher '{publisher}'")


if __name__ == "__main__":
    main()
