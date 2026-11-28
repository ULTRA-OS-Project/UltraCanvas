#!/usr/bin/env python3
"""Rebuilds the OpenDocument presentation fixtures PresentationOdpTest reads.

    python3 Tests/fixtures/make-odp-fixtures.py

    office-pptx-sample.odp   office-pptx-sample.pptx (the deck python-pptx
                             builds in make-office-encrypted-fixtures.py) as
                             LibreOffice Impress converts it: the same five
                             slides - titles, a subtitle, bullets on two
                             levels with a bold run, a numbered pair, a text
                             box, speaker notes, a table, a described picture,
                             a hidden slide and sixty bullets - written by an
                             ODF producer independent of UltraCanvas
    pptx-features.odp        pptx-features.pptx (make-pptx-features-
                             fixture.py) the same way: the look as drawn -
                             typefaces, strike, superscript, capitals, a
                             highlight, a link, a Wingdings bullet, numbering
                             from c), vertical text, a turned and mirrored
                             star with a gradient and a dashed outline, an
                             arrowed connector, a freeform, a table with
                             merged and coloured cells, a chart and a picture

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
    for name in ("office-pptx-sample", "pptx-features"):
        source = os.path.join(HERE, name + ".pptx")
        with tempfile.TemporaryDirectory() as work:
            subprocess.run([soffice, "--headless", "--convert-to", "odp", "--outdir", work, source],
                           check=True, stdout=subprocess.DEVNULL)
            made = os.path.join(work, name + ".odp")
            if not os.path.exists(made):
                print("LibreOffice wrote no .odp for " + name, file=sys.stderr)
                return 1
            shutil.copyfile(made, os.path.join(HERE, name + ".odp"))
        print("wrote " + name + ".odp")
    return 0


if __name__ == "__main__":
    sys.exit(main())
