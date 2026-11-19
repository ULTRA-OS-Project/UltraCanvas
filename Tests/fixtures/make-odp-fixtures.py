#!/usr/bin/env python3
"""Rebuilds the OpenDocument presentation fixture PresentationOdpTest reads.

    python3 Tests/fixtures/make-odp-fixtures.py

    office-pptx-sample.odp   office-pptx-sample.pptx (the deck python-pptx
                             builds in make-office-encrypted-fixtures.py) as
                             LibreOffice Impress converts it: the same five
                             slides - titles, a subtitle, bullets on two
                             levels with a bold run, a numbered pair, a text
                             box, speaker notes, a table, a described picture,
                             a hidden slide and sixty bullets - written by an
                             ODF producer independent of UltraCanvas

Needs LibreOffice (soffice) on the PATH; the checked-in file was written by
LibreOffice 24.2. A rebuilt file differs in its timestamps and generated
style names, not in what the test checks.

presentation-features.fodp, the flat deck beside it, is written by hand (its
head says what each slide holds) and is not rebuilt here.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def main() -> int:
    soffice = shutil.which("soffice") or shutil.which("libreoffice")
    if not soffice:
        print("soffice (LibreOffice) is not on the PATH", file=sys.stderr)
        return 1
    source = os.path.join(HERE, "office-pptx-sample.pptx")
    with tempfile.TemporaryDirectory() as work:
        subprocess.run([soffice, "--headless", "--convert-to", "odp", "--outdir", work, source],
                       check=True, stdout=subprocess.DEVNULL)
        made = os.path.join(work, "office-pptx-sample.odp")
        if not os.path.exists(made):
            print("LibreOffice wrote no .odp", file=sys.stderr)
            return 1
        shutil.copyfile(made, os.path.join(HERE, "office-pptx-sample.odp"))
    print("wrote office-pptx-sample.odp")
    return 0


if __name__ == "__main__":
    sys.exit(main())
