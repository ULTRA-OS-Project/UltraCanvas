- **Password-protected .xlsx and .docx files open.** A workbook or document
  saved with *Encrypt with Password* (ECMA-376 agile encryption, what Office
  2010 and later write) is a compound file, not a ZIP, and was refused as
  password-protected (`.xlsx`) or as a `.doc` that could not be read (`.docx`).
  `UltraCanvasOfficeEncryption.h` decrypts it with UltraCrypt: the SHA-512
  password key (100 000 rounds), the password verifier, the dataIntegrity HMAC
  over the whole encrypted package - checked before a byte is decrypted, so a
  changed file is refused - and the package in 4096-byte segments, each with
  its own IV.
  - `UltraCanvasSpreadsheet::LoadFromFile`, `LoadXLSX` and `LoadXLS` (a
    protected `.xlsx` under the `.xls` name), and `UCWordDocumentIO::Load` /
    `LoadDocx`, take the password as a last, optional argument. Without it the
    message says the file is password-protected; with a wrong one, that the
    password is not correct.
  - `UltraCanvasFileLoader::RequestDocumentPassword` asks for the password,
    checks it (`CheckOfficePassword`, the verifier only) and asks again after
    a wrong one; `OpenTextDocument` and the demo's spreadsheet Open button use
    it. `LoadTextDocument` takes the password too.
  - `UCZipPackageReader::OpenFromMemory` reads the decrypted package.
  - Refused with the reason: Office 2007's standard encryption, SHA-1-keyed
    agile files (Office 2010's default), certificate-only encryption, a file
    without an integrity check.
  - The core library now uses UltraCrypt: a shared core already absorbs it, a
    static one links it `PUBLIC`.
  - `Tests/OfficeEncryptionTest.cpp` reads a workbook and a document encrypted
    by msoffcrypto-tool (`Tests/fixtures/make-office-encrypted-fixtures.py`).
