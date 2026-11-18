#!/usr/bin/env python3
"""Rebuilds the PDF fixtures MediaViewerToolsTest and FilePreviewsTest read.

    python3 Tests/fixtures/make-pdf-fixtures.py

Needs pypdf with its AES support (pip install pypdf cryptography). Two fixtures:

    pdf-aes256-locked.pdf  one 200 x 200 pt page (a blue square), encrypted
                           with AES-256 (PDF 2.0, revision 6) under the user
                           password "tulip" (owner password "owner"): nothing
                           in it opens without a password, and "tulip" opens it.
    pdf-blue-square.pdf    the same page, not encrypted: a blue square on paper
                           the page does not paint, as nearly every PDF leaves it
                           (FilePreviewsTest: its thumbnail is on white paper).
"""

import os
import sys

from pypdf import PdfWriter
from pypdf.generic import DecodedStreamObject, NameObject

HERE = os.path.dirname(os.path.abspath(__file__))


def blue_square_page(writer):
    page = writer.add_blank_page(width=200, height=200)
    content = DecodedStreamObject()
    content.set_data(b"0.19 0.5 0.88 rg 40 40 120 120 re f\n")
    page[NameObject("/Contents")] = writer._add_object(content)
    return page


def write_plain():
    writer = PdfWriter()
    blue_square_page(writer)
    out = os.path.join(HERE, "pdf-blue-square.pdf")
    with open(out, "wb") as f:
        writer.write(f)
    print("wrote", out, os.path.getsize(out), "bytes")


def main():
    write_plain()
    writer = PdfWriter()
    page = writer.add_blank_page(width=200, height=200)
    content = DecodedStreamObject()
    content.set_data(b"0.19 0.5 0.88 rg 40 40 120 120 re f\n")
    page[NameObject("/Contents")] = writer._add_object(content)
    writer.encrypt(user_password="tulip", owner_password="owner", algorithm="AES-256")
    out = os.path.join(HERE, "pdf-aes256-locked.pdf")
    with open(out, "wb") as f:
        writer.write(f)
    print("wrote", out, os.path.getsize(out), "bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
