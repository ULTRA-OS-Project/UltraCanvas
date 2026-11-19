#!/usr/bin/env python3
"""Rebuilds the password-protected OpenDocument fixtures OdfEncryptionTest reads.

    python3 Tests/fixtures/make-odf-encrypted-fixtures.py

LibreOffice itself saves them "with password" - through its UNO API, so the
encryption is an independent producer's, not UltraCanvas's idea of the format.
LibreOffice 24.2 (the checked-in files) writes ODF 1.2 encryption: every
entry deflated, then AES-256-CBC with its own IV, keyed by PBKDF2-HMAC-SHA-1
(100000 rounds, its own salt) from the SHA-256 of the UTF-8 password, with a
SHA-256 checksum of its first kilobyte in META-INF/manifest.xml.

The password is the Office fixtures' one, "Ültra-Pässwört 1" (non-ASCII, so
the UTF-8 start key is what is tested).

    odf-encrypted.odt   word97-formatting.odt, saved with the password
    odf-encrypted.ods   a sheet LibreOffice makes from the CSV below: "Name" /
                        "Total" in row 1, "Widget" / 42 and "Gadget" / 7 below
    odf-encrypted.odp   office-pptx-sample.odp, saved with the password

    odf14-encrypted.odt / .ods / .odp
                        the same three as LibreOffice saves them when its
                        profile asks for ODF 1.4 (DefaultVersion 11) in
                        experimental mode, set before it starts: the
                        "wholesome" encryption - the whole
                        package, deflated, in one entry "encrypted-package",
                        AES-256-GCM (its 12-byte IV in front, the tag at the
                        end), keyed by Argon2id (3 passes, 64 MiB, 4 lanes)
                        from the SHA-256 of the UTF-8 password

Needs LibreOffice (soffice) and its Python UNO bridge (python3-uno). Every run
draws new salts and IVs, so a rebuilt fixture differs in every encrypted byte.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PASSWORD = "Ültra-Pässwört 1"
PORT = 2093


def main() -> int:
    try:
        import uno
        from com.sun.star.beans import PropertyValue
    except ImportError:
        print("the LibreOffice Python UNO bridge (python3-uno) is not installed", file=sys.stderr)
        return 1
    soffice = shutil.which("soffice") or shutil.which("libreoffice")
    if not soffice:
        print("soffice (LibreOffice) is not on the PATH", file=sys.stderr)
        return 1

    def prop(name, value):
        p = PropertyValue()
        p.Name, p.Value = name, value
        return p

    with tempfile.TemporaryDirectory() as work:
        csv = os.path.join(work, "sheet.csv")
        with open(csv, "w", encoding="utf-8") as f:
            f.write("Name,Total\nWidget,42\nGadget,7\n")
        csv_args = (prop("FilterName", "Text - txt - csv (StarCalc)"), prop("FilterOptions", "44,34,76,1"))
        for prefix, odf14 in (("odf-encrypted", False), ("odf14-encrypted", True)):
            profile = os.path.join(work, "profile-" + prefix)
            os.makedirs(os.path.join(profile, "user"))
            if odf14:
                # The ODF version is read when LibreOffice starts: set in the
                # profile it starts with, not changed while it runs. 24.2
                # writes the whole-package encryption only in experimental
                # mode; with ODF 1.4 alone it keeps the per-entry one.
                with open(os.path.join(profile, "user", "registrymodifications.xcu"), "w") as f:
                    f.write('<?xml version="1.0" encoding="UTF-8"?>\n'
                            '<oor:items xmlns:oor="http://openoffice.org/2001/registry" '
                            'xmlns:xs="http://www.w3.org/2001/XMLSchema" '
                            'xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">\n'
                            '<item oor:path="/org.openoffice.Office.Common/Save/ODF">'
                            '<prop oor:name="DefaultVersion" oor:op="fuse"><value>11</value></prop></item>\n'
                            '<item oor:path="/org.openoffice.Office.Common/Misc">'
                            '<prop oor:name="ExperimentalMode" oor:op="fuse"><value>true</value></prop></item>\n'
                            '</oor:items>\n')
            jobs = [
                (os.path.join(HERE, "word97-formatting.odt"), prefix + ".odt", "writer8", ()),
                (csv, prefix + ".ods", "calc8", csv_args),
                (os.path.join(HERE, "office-pptx-sample.odp"), prefix + ".odp", "impress8", ()),
            ]
            if not save_with_password(uno, prop, soffice, "file://" + profile, jobs):
                return 1
    return 0


def save_with_password(uno, prop, soffice, profile, jobs):
    """Starts LibreOffice on `profile` and saves each job with the password."""
    office = subprocess.Popen([soffice, "--headless", "--invisible", "--norestore",
                               "-env:UserInstallation=" + profile,
                               f"--accept=socket,host=localhost,port={PORT};urp;"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        local = uno.getComponentContext()
        resolver = local.ServiceManager.createInstanceWithContext("com.sun.star.bridge.UnoUrlResolver", local)
        ctx = None
        for _ in range(60):
            try:
                ctx = resolver.resolve(f"uno:socket,host=localhost,port={PORT};urp;StarOffice.ComponentContext")
                break
            except Exception:
                time.sleep(1)
        if ctx is None:
            print("LibreOffice did not answer", file=sys.stderr)
            return False
        desktop = ctx.ServiceManager.createInstanceWithContext("com.sun.star.frame.Desktop", ctx)
        for source, name, filter_name, load_args in jobs:
            doc = desktop.loadComponentFromURL(uno.systemPathToFileUrl(source), "_blank", 0,
                                               (prop("Hidden", True),) + load_args)
            doc.storeToURL(uno.systemPathToFileUrl(os.path.join(HERE, name)),
                           (prop("FilterName", filter_name), prop("Password", PASSWORD)))
            doc.close(True)
            print("wrote", name)
        try:
            desktop.terminate()
        except Exception:
            pass   # the bridge goes away with the office
        return True
    finally:
        try:
            office.wait(timeout=30)
        except subprocess.TimeoutExpired:
            office.kill()


if __name__ == "__main__":
    sys.exit(main())
