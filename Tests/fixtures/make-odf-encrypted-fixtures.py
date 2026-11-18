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
        profile = "file://" + os.path.join(work, "profile")
        office = subprocess.Popen([soffice, "--headless", "--invisible", "--norestore",
                                   "-env:UserInstallation=" + profile,
                                   f"--accept=socket,host=localhost,port={PORT};urp;"],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            local = uno.getComponentContext()
            resolver = local.ServiceManager.createInstanceWithContext(
                "com.sun.star.bridge.UnoUrlResolver", local)
            ctx = None
            for _ in range(60):
                try:
                    ctx = resolver.resolve(
                        f"uno:socket,host=localhost,port={PORT};urp;StarOffice.ComponentContext")
                    break
                except Exception:
                    time.sleep(1)
            if ctx is None:
                print("LibreOffice did not answer", file=sys.stderr)
                return 1
            desktop = ctx.ServiceManager.createInstanceWithContext("com.sun.star.frame.Desktop", ctx)
            jobs = [
                (os.path.join(HERE, "word97-formatting.odt"), "odf-encrypted.odt", "writer8", ()),
                (csv, "odf-encrypted.ods", "calc8",
                 (prop("FilterName", "Text - txt - csv (StarCalc)"), prop("FilterOptions", "44,34,76,1"))),
                (os.path.join(HERE, "office-pptx-sample.odp"), "odf-encrypted.odp", "impress8", ()),
            ]
            for source, name, filter_name, load_args in jobs:
                doc = desktop.loadComponentFromURL(uno.systemPathToFileUrl(source), "_blank", 0,
                                                   (prop("Hidden", True),) + load_args)
                target = os.path.join(HERE, name)
                doc.storeToURL(uno.systemPathToFileUrl(target),
                               (prop("FilterName", filter_name), prop("Password", PASSWORD)))
                doc.close(True)
                print("wrote", name)
            desktop.terminate()
        finally:
            try:
                office.wait(timeout=30)
            except subprocess.TimeoutExpired:
                office.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
