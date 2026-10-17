- **Office 2010's password-protected files open, and .xlsx / .docx save with
  a password.**
  - Reading: agile-encrypted files keyed with SHA-1 (Office 2010's default,
    with AES-128) were refused; they now open like the SHA-512 ones. SHA-1 is
    asked of UltraCrypt as a legacy digest for this read only.
  - Writing: `UltraCanvasSpreadsheet::SaveToFile` / `SaveXLSX`,
    `UCWordDocumentIO::Save` / `SaveDocx` and
    `UltraCanvasFileLoader::SaveTextDocument` take an optional password and
    write the file encrypted as Office 2013 and later do - AES-256-CBC,
    SHA-512, 100 000 rounds, fresh random salts and keys every save, the
    integrity HMAC and the DataSpaces storage. The unencrypted package never
    reaches the disk. A password for any other format (.ods, .odt, .csv) is
    refused rather than dropped.
  - `EncryptOfficePackage` / `EncryptOfficePackageToFile` do it for any
    package; `UCCompoundFileWriter` (new) builds the compound file, and
    `UCZipPackageWriter::OpenInMemory` / `TakeBytes` the package in memory.
  - `UltraCanvasFileLoader::RequestNewDocumentPassword` asks for a new
    password twice. The demo's spreadsheet page keeps a workbook's password
    when it is saved again as .xlsx and offers one to an unprotected workbook.
  - msoffcrypto-tool decrypts what is written, integrity check included;
    `OfficeEncryptionTest` covers the round trips and an Office 2010-style
    SHA-1 fixture.
