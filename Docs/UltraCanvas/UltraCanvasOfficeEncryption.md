# UltraCanvasOfficeEncryption — password-protected .xlsx and .docx

An `.xlsx`, `.docx` or `.pptx` saved with *Encrypt with Password* is not a ZIP
package. It is an OLE2 [compound file](UltraCanvasCompoundFile.md) holding two
streams: `EncryptionInfo`, which says how the key is derived, and
`EncryptedPackage`, the original package encrypted. This module turns the
second back into the ZIP, which the `.xlsx` and `.docx` readers then read as
usual. The spreadsheet, the Word reader and `UltraCanvasFileLoader` use it;
most code never calls it directly, but passes a password to them.

Header: `UltraCanvasOfficeEncryption.h`. The cryptography is UltraCrypt's
(`Docs/Modules/UltraCrypt/README.md`): its constant-time AES-CBC, SHA-2 and
HMAC.

## Opening one

A spreadsheet or a document takes the password as one more argument:

<!-- doc-check: std::string path; std::string password; UltraCanvas::UltraCanvasWindowBase* window; void ShowError(const std::string& message); -->
```cpp
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasFileLoader.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"

using namespace UltraCanvas;

auto sheet = std::make_shared<UltraCanvasSpreadsheet>("Book", 0, 0, 800, 600);
if (!sheet->LoadFromFile(path, password)) {   // also LoadXLSX, LoadXLS
    ShowError(sheet->GetLastError());          // "The password is not correct: ..."
}

UCRichDocument document;
std::string error;
if (!UCWordDocumentIO::Load(path, document, error, password)) ShowError(error);

// Or ask the user: RequestDocumentPassword prompts (a password field), checks
// the password, and asks again after a wrong one. A file without a password
// calls back at once with (true, "").
UltraCanvasFileLoader::RequestDocumentPassword(path, window,
    [sheet, path](bool proceed, const std::string& typed) {
        if (proceed && !sheet->LoadFromFile(path, typed)) ShowError(sheet->GetLastError());
    });
```

`UltraCanvasFileLoader::OpenTextDocument` does the asking itself: a
password-protected `.docx` chosen in its dialog prompts before it loads, and
cancelling the prompt reports `Cancel`, like the dialog.

Without a password, every loader fails with a message saying the file is
password-protected, never that it is damaged or of another format. A file
named `.xls` that is a password-protected `.xlsx` opens through `LoadXLS` too,
as Excel opens it.

## Decrypting directly

<!-- doc-check: std::string path; std::string password; void ReadPackage(const std::vector<uint8_t>& zip); void ShowError(const std::string& message); -->
```cpp
#include "UltraCanvasOfficeEncryption.h"

using namespace UltraCanvas;

std::vector<uint8_t> zip;
std::string error;
switch (DecryptOfficePackageFile(path, password, zip, error)) {
    case OfficeDecryptStatus::Decrypted:    ReadPackage(zip); break;   // UCZipPackageReader::OpenFromMemory
    case OfficeDecryptStatus::NotEncrypted: break;                     // an ordinary file: read it as it is
    default:                                ShowError(error); break;
}
```

| Status | Meaning |
|---|---|
| `Decrypted` | `outPackage` holds the ZIP package |
| `NotEncrypted` | not an encrypted Office package (an ordinary ZIP, an `.xls`, ...) |
| `PasswordRequired` | encrypted, and the password was empty |
| `WrongPassword` | the password verifier does not match |
| `Tampered` | the password is right but the integrity HMAC does not match: the file was changed after it was saved |
| `Unsupported` | an encryption this reader does not implement (below) |
| `Damaged` | the streams are truncated or malformed |
| `NoCryptoBackend` | UltraCrypt was built without libsodium |

`CheckOfficePassword(path, password, error)` runs only the key derivation and
the password verifier — what a prompt needs to ask again after a wrong
password, without decrypting the file. `IsEncryptedOfficePackage(path)` tells
whether a file is one at all. `DecryptOfficePackage` takes the two streams'
contents, or an open `UCCompoundFileReader`.

## What is supported

ECMA-376 **agile** encryption ([MS-OFFCRYPTO] 2.3.4.10-2.3.4.15), with
AES-128, -192 or -256 in CBC mode and SHA-256 or SHA-512: what Office writes by
default since 2013, and what LibreOffice and msoffcrypto-tool write. In order:

1. **The password key.** The hash of the password salt and the password in
   UTF-16LE, rehashed `spinCount` times (100 000 by default), each round
   hashing the round number and the previous hash. A spin count above
   [MS-OFFCRYPTO]'s 10 000 000 is refused before any hashing.
2. **The password verifier.** Two values decrypted with keys derived from the
   password key: the hash of the first must equal the second, which tells a
   wrong password from a right one.
3. **The intermediate key**, which the password key unwraps.
4. **The integrity HMAC**, over the whole `EncryptedPackage` stream, checked
   **before anything is decrypted**: a changed file is refused as `Tampered`,
   never decrypted into something else. A file without one is not opened.
5. **The package**, in 4096-byte segments, each decrypted with its own IV: the
   hash of the key data's salt and the segment's index.

Refused as `Unsupported`, each with the reason: Office 2007's **standard**
encryption (AES-ECB, version 3.2), extensible encryption, a key encryptor for a
certificate rather than a password, and agile files keyed with **SHA-1**
(Office 2010's default) — UltraCrypt offers SHA-1 for HMAC only. Saving such a
file again from a current Office converts it to SHA-512.

Writing encrypted files is not supported: a decrypted workbook or document
saves as an ordinary one.

## Implementation notes

- Element names are matched without their namespace prefix (`p:encryptedKey`):
  writers choose their own prefixes.
- A derived key or IV longer than needed is cut to size, a shorter one padded
  with `0x36`, as [MS-OFFCRYPTO] 2.3.4.11 specifies.
- The password and every key travel in `UltraCryptSecureBuffer`s and are wiped
  when done with; the decrypted package is the document itself and is not.
- Status messages never say which of the verifier values or HMAC fields was
  off — there is no oracle to consult.

## Test

`Tests/OfficeEncryptionTest.cpp` reads a workbook and a document encrypted by
msoffcrypto-tool, an implementation independent of this one
(`Tests/fixtures/office-agile-encrypted.{xlsx,docx}`;
`Tests/fixtures/make-office-encrypted-fixtures.py` rebuilds them). The password
is not ASCII, and the workbook spans five segments. It checks the right, a
wrong and a missing password, a changed byte of the package and of its size
field, a package cut short, standard encryption, a damaged header, an absurd
spin count, SHA-1, a missing integrity check, `CheckOfficePassword`, and the
spreadsheet, the Word reader and `UltraCanvasFileLoader::LoadTextDocument`
opening the files.
