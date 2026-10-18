#!/usr/bin/env python3
"""Rebuilds the password-protected Office fixtures OfficeEncryptionTest reads.

    python3 Tests/fixtures/make-office-encrypted-fixtures.py [fixture-name ...]

With names, only those are rebuilt: every run draws new salts and keys, so a
rebuilt fixture differs in every byte from the one checked in.

Needs msoffcrypto-tool (pip install msoffcrypto-tool), an encryptor written
independently of UltraCanvas, so the test does not only check the reader
against its own idea of the format. It writes ECMA-376 agile encryption, which
is what Excel and Word 2010 and later write: AES-256-CBC, SHA-512, a spin
count of 100000, and the dataIntegrity HMAC.

    office-agile-encrypted.xlsx  a workbook: "Name" / "Total" in row 1, the
                                 text "Widget" and the number 42 in row 2, and
                                 =B2*2 in B3, then A4:A403 = 7 * the row
                                 number - stored uncompressed, so the
                                 encrypted package spans several 4096-byte
                                 segments, each with its own IV
    office-agile-encrypted.docx  a document: "Secret text", then "Paragraph 2"
                                 to "Paragraph 201" - stored uncompressed too
    office-agile-sha1-encrypted.xlsx
                                 the same workbook as Office 2010 encrypts by
                                 default: SHA-1 and AES-128. msoffcrypto-tool
                                 writes only SHA-512 / AES-256, so its cipher
                                 parameters are switched for this one, and its
                                 AES helper pads to the block (the 20-byte
                                 SHA-1 HMAC key is not a whole block). The key
                                 derivation, the verifier and the HMAC are
                                 still its code, and its decryptor checks the
                                 password and the content before the file is
                                 written. Its integrity check cannot be used
                                 here: it compares the whole decrypted HMAC
                                 block, padding included, where
                                 [MS-OFFCRYPTO] 2.3.4.14 compares hashSize
                                 bytes, so it fails every SHA-1 file. The
                                 script checks the HMAC itself instead
                                 (check_hmac).
    office-standard-encrypted.xlsx
                                 the same workbook in Office 2007's
                                 "standard" encryption: a binary header,
                                 AES-128 in ECB mode, a SHA-1 key of 50000
                                 rounds, no integrity check. msoffcrypto-tool
                                 only reads this one, so the script writes
                                 the header from [MS-OFFCRYPTO] 2.3.4.5-2.3.4.6
                                 (encrypt_standard), takes the key from
                                 msoffcrypto's makekey_from_password, encrypts
                                 with the cryptography package's AES-ECB,
                                 wraps it in msoffcrypto's container writer,
                                 and checks it with msoffcrypto's decryptor.

Both packages are kept above 4096 bytes on purpose. A smaller EncryptedPackage
stream goes into the compound file's mini stream, and msoffcrypto-tool 6.0.0
writes that case wrongly: its StreamSize field comes out as garbage and its
own integrity check rejects the file.

Both are encrypted with the password PASSWORD below - not ASCII, so the
UTF-16LE conversion the key derivation starts from is covered too. The plain
packages are written here by hand, as the smallest parts each format needs.
"""

import io
import os
import sys
import zipfile

try:
    import msoffcrypto
    import msoffcrypto.method.ecma376_agile as agile
    from msoffcrypto.format.ooxml import OOXMLFile
except ImportError:
    sys.exit("msoffcrypto-tool is not installed: pip install msoffcrypto-tool")

PASSWORD = "Ültra-Pässwört 1"
HERE = os.path.dirname(os.path.abspath(__file__))

CONTENT_TYPES_XLSX = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>
</Types>"""

ROOT_RELS_XLSX = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
</Relationships>"""

WORKBOOK = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<sheets><sheet name="Secret" sheetId="1" r:id="rId1"/></sheets>
</workbook>"""

WORKBOOK_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>
</Relationships>"""

SHARED_STRINGS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="3" uniqueCount="3">
<si><t>Name</t></si><si><t>Total</t></si><si><t>Widget</t></si>
</sst>"""

SHEET = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
<sheetData>
<row r="1"><c r="A1" t="s"><v>0</v></c><c r="B1" t="s"><v>1</v></c></row>
<row r="2"><c r="A2" t="s"><v>2</v></c><c r="B2"><v>42</v></c></row>
<row r="3"><c r="B3"><f>B2*2</f><v>84</v></c></row>
%s
</sheetData>
</worksheet>""" % "\n".join(
    '<row r="%d"><c r="A%d"><v>%d</v></c></row>' % (r, r, r * 7) for r in range(4, 404))

CONTENT_TYPES_DOCX = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
</Types>"""

ROOT_RELS_DOCX = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>"""

DOCUMENT = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body><w:p><w:r><w:t>Secret text</w:t></w:r></w:p>
%s</w:body>
</w:document>""" % "\n".join(
    "<w:p><w:r><w:t>Paragraph %d</w:t></w:r></w:p>" % n for n in range(2, 202))


def package(parts, compression=zipfile.ZIP_DEFLATED):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression) as z:
        for name, text in parts:
            z.writestr(name, text)
    return buffer.getvalue()


def check_hmac(office_file):
    """[MS-OFFCRYPTO] 2.3.4.14, comparing hashSize bytes."""
    import hashlib
    import hmac
    info = office_file.info
    algorithm = getattr(hashlib, info["keyDataHashAlgorithm"].lower())
    size = algorithm().digest_size
    ivs = [algorithm(info["keyDataSalt"] + block).digest()[:16]
           for block in (agile.blkKey_dataIntegrity1, agile.blkKey_dataIntegrity2)]
    key = agile._decrypt_aes_cbc(info["encryptedHmacKey"], office_file.secret_key, ivs[0])[:size]
    value = agile._decrypt_aes_cbc(info["encryptedHmacValue"], office_file.secret_key, ivs[1])[:size]
    with office_file.file.openstream("EncryptedPackage") as stream:
        assert hmac.new(key, stream.read(), algorithm).digest() == value, "HMAC mismatch"


WANTED = set(sys.argv[1:])


def encrypt_standard(plain, name):
    """Office 2007's standard encryption, AES-128 ([MS-OFFCRYPTO] 2.3.4.5)."""
    if WANTED and name not in WANTED:
        return
    import struct
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from msoffcrypto.method.container.ecma376_encrypted import ECMA376Encrypted
    from msoffcrypto.method.ecma376_standard import ECMA376Standard

    salt, verifier = os.urandom(16), os.urandom(16)
    key = ECMA376Standard.makekey_from_password(PASSWORD, 0x660E, 0x8004, 0x18, 128, 16, salt)

    def ecb(data):
        encryptor = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
        return encryptor.update(data) + encryptor.finalize()

    import hashlib
    csp = "Microsoft Enhanced RSA and AES Cryptographic Provider\0".encode("utf-16le")
    # EncryptionHeader: Flags (fCryptoAPI | fAES), SizeExtra, AlgID (AES-128),
    # AlgIDHash (SHA-1), KeySize, ProviderType (PROV_RSA_AES), 2 reserved, CSP.
    header = struct.pack("<8I", 0x24, 0, 0x660E, 0x8004, 128, 0x18, 0, 0) + csp
    verifier_hash = hashlib.sha1(verifier).digest() + b"\0" * 12
    info = (struct.pack("<HHI", 4, 2, 0x24) + struct.pack("<I", len(header)) + header
            + struct.pack("<I", 16) + salt + ecb(verifier)
            + struct.pack("<I", 20) + ecb(verifier_hash))
    padded = plain + b"\0" * (-len(plain) % 16)
    package = struct.pack("<Q", len(plain)) + ecb(padded)
    out = io.BytesIO()
    ECMA376Encrypted(package, info).write_to(out)

    check = msoffcrypto.OfficeFile(io.BytesIO(out.getvalue()))
    check.load_key(password=PASSWORD)
    decrypted = io.BytesIO()
    check.decrypt(decrypted)
    assert decrypted.getvalue() == plain, name + " does not decrypt back"
    path = os.path.join(HERE, name)
    with open(path, "wb") as f:
        f.write(out.getvalue())
    print("wrote", path, len(out.getvalue()), "bytes")


def encrypt(plain, name, sha1=False):
    if WANTED and name not in WANTED:
        return
    out = io.BytesIO()
    if sha1:
        params_init = agile.ECMA376AgileCipherParams.__init__
        aes = agile._encrypt_aes_cbc

        def office2010_params(self):
            params_init(self)
            self.hashName, self.hashSize, self.keyBits = "SHA1", 20, 128

        def padded_aes(data, key, iv):
            return aes(data + b"\0" * (-len(data) % 16), key, iv)

        agile.ECMA376AgileCipherParams.__init__ = office2010_params
        agile._encrypt_aes_cbc = padded_aes
        try:
            OOXMLFile(io.BytesIO(plain)).encrypt(PASSWORD, out)
        finally:
            agile.ECMA376AgileCipherParams.__init__ = params_init
            agile._encrypt_aes_cbc = aes
    else:
        OOXMLFile(io.BytesIO(plain)).encrypt(PASSWORD, out)
    check = msoffcrypto.OfficeFile(io.BytesIO(out.getvalue()))
    check.load_key(password=PASSWORD)
    decrypted = io.BytesIO()
    check.decrypt(decrypted, verify_integrity=not sha1)
    assert decrypted.getvalue() == plain, name + " does not decrypt back"
    if sha1:
        check_hmac(check)
    path = os.path.join(HERE, name)
    with open(path, "wb") as f:
        f.write(out.getvalue())
    print("wrote", path, len(out.getvalue()), "bytes")


WORKBOOK_PACKAGE = package([
    ("[Content_Types].xml", CONTENT_TYPES_XLSX),
    ("_rels/.rels", ROOT_RELS_XLSX),
    ("xl/workbook.xml", WORKBOOK),
    ("xl/_rels/workbook.xml.rels", WORKBOOK_RELS),
    ("xl/sharedStrings.xml", SHARED_STRINGS),
    ("xl/worksheets/sheet1.xml", SHEET),
], zipfile.ZIP_STORED)
encrypt(WORKBOOK_PACKAGE, "office-agile-encrypted.xlsx")
encrypt(WORKBOOK_PACKAGE, "office-agile-sha1-encrypted.xlsx", sha1=True)
encrypt_standard(WORKBOOK_PACKAGE, "office-standard-encrypted.xlsx")

encrypt(package([
    ("[Content_Types].xml", CONTENT_TYPES_DOCX),
    ("_rels/.rels", ROOT_RELS_DOCX),
    ("word/document.xml", DOCUMENT),
], zipfile.ZIP_STORED), "office-agile-encrypted.docx")
