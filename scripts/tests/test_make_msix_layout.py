#!/usr/bin/env python3
"""What scripts/make_msix_layout.py --app puts in the package of one
application: its program, the DLLs the import tables reach from it, from the
plug-ins and from the run-time modules, the media the framework reads - and
not the other programs, their DLLs, another application's plug-in family or
the demo's samples. Also the four-part version of a two-part one (Texter's),
and a manifest with the one Start menu entry under the name the package
gives the application. The PE images are test_pe_imports.py's. Run with

    python3 -m unittest discover -s scripts/tests -v
"""
import importlib.util
import io
import os
import re
import sys
import tempfile
import unittest
from pathlib import Path

TESTS = Path(__file__).resolve().parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


layout = load("make_msix_layout", TESTS.parent / "make_msix_layout.py")
build_pe = load("test_pe_imports_images", TESTS / "test_pe_imports.py").build_pe

GUI, CONSOLE = 2, 3


def write(path, data=b""):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def program(imports, subsystem=GUI):
    """A PE image importing `imports`, with the Subsystem pe_subsystem reads
    (92 bytes after "PE\\0\\0", which build_pe puts at 0x40)."""
    image = bytearray(build_pe(imports))
    image[0x40 + 92:0x40 + 94] = subsystem.to_bytes(2, "little")
    return bytes(image)


def make_dist(root):
    dist = os.path.join(root, "dist")
    write(os.path.join(dist, "Texter.exe"), program(["KERNEL32.dll", "libUltraCanvas.dll"]))
    write(os.path.join(dist, "Texter.com"), program(["KERNEL32.dll"], CONSOLE))
    write(os.path.join(dist, "ChatCube.exe"), program(["libUltraCanvas.dll", "chat.dll"]))
    write(os.path.join(dist, "videofx.exe"), program(["KERNEL32.dll"], CONSOLE))
    # Imported under another case than the file has, as Windows allows.
    write(os.path.join(dist, "libUltraCanvas.dll"), build_pe(["LIBCAIRO-2.DLL", "USER32.dll"]))
    write(os.path.join(dist, "libcairo-2.dll"), build_pe(["KERNEL32.dll"]))
    write(os.path.join(dist, "chat.dll"), build_pe(["KERNEL32.dll"]))
    write(os.path.join(dist, "Plugins", "UltraNet", "ultranet_mdns.dll"), build_pe(["libcurl-4.dll"]))
    write(os.path.join(dist, "libcurl-4.dll"), build_pe(["KERNEL32.dll"]))
    write(os.path.join(dist, "Plugins", "UltraIM", "ultraim_direct.dll"), build_pe(["im.dll"]))
    write(os.path.join(dist, "im.dll"), build_pe(["KERNEL32.dll"]))
    write(os.path.join(dist, "lib", "vips-modules-8.16", "vips-heif.dll"), build_pe(["libheif.dll"]))
    write(os.path.join(dist, "libheif.dll"), build_pe(["KERNEL32.dll"]))
    write(os.path.join(dist, "etc", "ImageMagick-7", "policy.xml"), b"<policymap/>")
    write(os.path.join(dist, "Resources", "media", "fonts", "Sans.ttf"))
    write(os.path.join(dist, "Resources", "media", "docs", "Logo_Texter.png"))
    write(os.path.join(dist, "Resources", "media", "videos", "sample.mp4"))
    write(os.path.join(dist, "Resources", "Docs", "README.md"))
    write(os.path.join(dist, "cacert.pem"), b"-----BEGIN CERTIFICATE-----")
    write(os.path.join(dist, "uc-diagnose.bat"))
    return dist


def files(folder):
    found = set()
    for directory, _, names in os.walk(folder):
        for name in names:
            found.add(os.path.relpath(os.path.join(directory, name), folder).replace(os.sep, "/"))
    return found


class CopyApp(unittest.TestCase):
    def test_texter_alone(self):
        with tempfile.TemporaryDirectory() as root:
            dist = make_dist(root)
            target = os.path.join(root, "layout")
            count = layout.copy_app(dist, target, "Texter")
            self.assertEqual(files(target), {
                "Texter.exe", "Texter.com",
                "libUltraCanvas.dll", "libcairo-2.dll",         # the program's closure
                "Plugins/UltraNet/ultranet_mdns.dll", "libcurl-4.dll",
                "lib/vips-modules-8.16/vips-heif.dll", "libheif.dll",
                "etc/ImageMagick-7/policy.xml",
                "Resources/media/fonts/Sans.ttf", "Resources/media/docs/Logo_Texter.png",
                "cacert.pem",
            })
            self.assertEqual(count, 4)

    def test_chatcube_keeps_its_plugin_family(self):
        with tempfile.TemporaryDirectory() as root:
            dist = make_dist(root)
            target = os.path.join(root, "layout")
            layout.copy_app(dist, target, "ChatCube")
            found = files(target)
            self.assertIn("Plugins/UltraIM/ultraim_direct.dll", found)
            self.assertIn("im.dll", found)
            self.assertIn("chat.dll", found)
            self.assertNotIn("Texter.exe", found)

    def test_replaces_an_old_layout(self):
        with tempfile.TemporaryDirectory() as root:
            dist = make_dist(root)
            target = os.path.join(root, "layout")
            write(os.path.join(target, "left-over.dll"))
            layout.copy_app(dist, target, "Texter")
            self.assertNotIn("left-over.dll", files(target))


class Version(unittest.TestCase):
    def test_four_parts(self):
        self.assertEqual(layout.msix_version("0.9.609"), "0.9.609.0")
        self.assertEqual(layout.msix_version("1.94"), "1.94.0.0")
        self.assertEqual(layout.msix_version("1.94.2.0"), "1.94.2.0")

    def test_refuses_what_is_no_version(self):
        for bad in ("1", "1.94.x", "1.70000", "1.2.3.4.5"):
            with self.assertRaises(SystemExit, msg=bad):
                layout.msix_version(bad)


class Manifest(unittest.TestCase):
    def test_one_application_under_the_package_s_name(self):
        with tempfile.TemporaryDirectory() as root:
            dist = make_dist(root)
            apps = [app for app in layout.find_apps(dist, layout.REPO_ROOT) if app.exe == "Texter"]
            self.assertEqual(len(apps), 1)
            # A console program without a desktop entry is no application.
            self.assertNotIn("videofx", [app.exe for app in layout.find_apps(dist, layout.REPO_ROOT)])
            apps[0].display_name = "UltraTexter"
            apps[0].description = "UltraTexter, an UltraCanvas application"
            tree = layout.build_manifest(
                apps, "ULTRAOSDevelopment.UltraTexter", "CN=Test", "Test", "UltraTexter",
                apps[0].description, "1.94.0.0", "x64", "10.0.18362.0", "10.0.26100.0")
            ns = {"m": "http://schemas.microsoft.com/appx/manifest/foundation/windows10",
                  "uap": "http://schemas.microsoft.com/appx/manifest/uap/windows10"}
            package = tree.getroot()
            self.assertEqual(package.find("m:Identity", ns).get("Version"), "1.94.0.0")
            self.assertEqual(package.find("m:Properties/m:DisplayName", ns).text, "UltraTexter")
            applications = package.findall("m:Applications/m:Application", ns)
            self.assertEqual([a.get("Executable") for a in applications], ["Texter.exe"])
            visual = applications[0].find("uap:VisualElements", ns)
            self.assertEqual(visual.get("DisplayName"), "UltraTexter")
            # Its file types come along: Explorer's "Open with".
            self.assertIn(".md", [t.text for t in applications[0].iter(
                "{http://schemas.microsoft.com/appx/manifest/uap/windows10}FileType")])

    def written(self, apps):
        tree = layout.build_manifest(
            apps, "ULTRAOSDevelopment.Test", "CN=Test", "Test", "Test", "Test",
            "1.0.0.0", "x64", "10.0.18362.0", "10.0.26100.0")
        out = io.BytesIO()
        tree.write(out, encoding="utf-8", xml_declaration=True)
        return out.getvalue().decode("utf-8")

    def check_ignorable_prefixes_are_declared(self, apps, expected):
        text = self.written(apps)
        ignorable = re.search(r'IgnorableNamespaces="([^"]*)"', text).group(1).split()
        # makeappx refuses a prefix named here that the manifest does not
        # declare (0x80080204), and ElementTree declares only the used ones.
        for prefix in ignorable:
            self.assertIn(f"xmlns:{prefix}=", text)
        self.assertEqual(sorted(ignorable), sorted(expected))

    def test_ignorable_namespaces_without_a_toast_activator(self):
        texter = layout.App("Texter", "UltraTexter", "Texter.png", "An editor",
                            [layout.TEXT_TYPES])
        self.check_ignorable_prefixes_are_declared([texter], ["uap", "uap3", "rescap"])

    def test_ignorable_namespaces_with_a_toast_activator(self):
        mail = layout.App("UltraMail", "UltraMail", "UltraMail.png", "Mail", [],
                          "org.ultraos.ultramail")
        self.check_ignorable_prefixes_are_declared(
            [mail], ["uap", "com", "desktop", "rescap"])

    def test_ignorable_namespaces_of_a_plain_application(self):
        plain = layout.App("UltraFiler", "UltraFiler", "UltraFiler.png", "Files")
        self.check_ignorable_prefixes_are_declared([plain], ["uap", "rescap"])


if __name__ == "__main__":
    unittest.main()
