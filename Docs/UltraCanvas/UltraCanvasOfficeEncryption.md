# UltraCanvasOfficeEncryption — password-protected .xlsx and .docx

An `.xlsx`, `.docx` or `.pptx` saved with *Encrypt with Password* is not a ZIP
package. It is an OLE2 [compound file](UltraCanvasCompoundFile.md) holding two
streams: `EncryptionInfo`, which says how the key is derived, and
`EncryptedPackage`, the original package encrypted. This module turns the
second back into the ZIP, which the `.xlsx` and `.docx` readers then read as
usual, and the other way round for the writers. The spreadsheet, the Word
reader and writer, the [PowerPoint reader](UltraCanvasPresentationPptx.md)
and `UltraCanvasFileLoader` use it; most code never calls it
directly, but passes a password to them.

Header: `UltraCanvasOfficeEncryption.h`. The cryptography is UltraCrypt's
(`Docs/Modules/UltraCrypt/README.md`): its constant-time AES-CBC, SHA-2, HMAC
and random numbers, and SHA-1 to read Office 2010's files.

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

## Saving one

The writers take the password the same way, and write the file encrypted -
the unencrypted package never reaches the disk:

<!-- doc-check: std::string path; std::string password; UltraCanvas::UltraCanvasWindowBase* window; UltraCanvas::UCRichDocument document; void ShowError(const std::string& message); -->
```cpp
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasFileLoader.h"

using namespace UltraCanvas;

auto sheet = std::make_shared<UltraCanvasSpreadsheet>("Book", 0, 0, 800, 600);
if (!sheet->SaveToFile(path, password)) ShowError(sheet->GetLastError());   // or SaveXLSX

std::string error;
if (!UltraCanvasFileLoader::SaveTextDocument(document, path, error, password)) ShowError(error);
// (UCWordDocumentIO::Save / SaveDocx take it too.)

// Ask for a new one: typed twice, asked again until the two match.
UltraCanvasFileLoader::RequestNewDocumentPassword(window,
    [sheet, path](bool proceed, const std::string& typed) {
        if (proceed && !sheet->SaveToFile(path, typed)) ShowError(sheet->GetLastError());
    });
```

Only `.xlsx` and `.docx` carry a password. `SaveToFile` and
`UCWordDocumentIO::Save` refuse one for `.ods`, `.odt`, `.csv` or any other
format, rather than save the file unprotected. Without a password the writers
write an ordinary package, as before. The demo's spreadsheet page shows the
round: a workbook opened with a password keeps it when saved again as `.xlsx`,
and an unprotected one saved as `.xlsx` is offered one.

`EncryptOfficePackage(zip, password, file, error)` encrypts a package already
in memory; `EncryptOfficePackageToFile` writes the result.

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

**Read:** ECMA-376 **agile** encryption ([MS-OFFCRYPTO] 2.3.4.10-2.3.4.15),
with AES-128, -192 or -256 in CBC mode and SHA-512, SHA-256 or SHA-1: what
Office writes by default since 2010 (SHA-1 and AES-128 in Office 2010, SHA-512
and AES-256 since 2013), and what LibreOffice and msoffcrypto-tool write. In
order:

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

**Also read:** Office 2007's **standard** encryption ([MS-OFFCRYPTO]
2.3.4.5-2.3.4.9, versions 2.2 / 3.2 / 4.2 with the CryptoAPI and AES flags): a
binary header instead of XML, the password key as SHA-1 over the salt and
password rehashed 50 000 times and then derived as 2.3.4.7 says, the same kind
of password verifier, and the package in AES-128, -192 or -256 **ECB** mode in
one piece. UltraCrypt has no ECB; it is CBC with a zero IV, each block then
XORed with the ciphertext block before it.

The standard format carries **no integrity check**. A wrong password is still
told apart by its verifier, but a changed file decrypts into changed content
rather than being refused as `Tampered` - the ZIP reader's CRCs catch most of
that, not all. Such files are opened anyway, as Office and every other reader
opens them: refusing them would refuse every Office 2007 file. (An agile file
that lacks its HMAC is still refused - Office always writes one there, so a
missing one means it was removed.)

Refused as `Unsupported`, each with the reason: RC4 standard encryption (the
CryptoAPI flag without AES), extensible encryption (version 4.3), a key
encryptor for a certificate rather than a password, and a hash UltraCrypt does
not have (SHA-384).

SHA-1 is UltraCrypt's legacy digest, which `UltraCrypt_Hash` refuses unless
asked. This module asks, and only for reading: Office 2010 keys its files with
it, and SHA-1's collision weakness does not reach a password hash or a key
derivation.

**Written:** what Office 2013 and later write - AES-256-CBC, SHA-512, a spin
count of 100 000, random 16-byte salts, a random intermediate key and HMAC key
drawn from the CSPRNG for every save, the integrity HMAC, and the
`\x06DataSpaces` storage ([MS-OFFCRYPTO] 2.1.5-2.1.8) Office looks for before
it will call a file encrypted. The compound file around them is built by
`UCCompoundFileWriter` ([UltraCanvasCompoundFile](UltraCanvasCompoundFile.md)).

## Implementation notes

- Element names are matched without their namespace prefix (`p:encryptedKey`):
  writers choose their own prefixes.
- A derived key or IV longer than needed is cut to size, a shorter one padded
  with `0x36`, as [MS-OFFCRYPTO] 2.3.4.11 specifies.
- The password and every key travel in `UltraCryptSecureBuffer`s and are wiped
  when done with; the decrypted package is the document itself and is not.
- Status messages never say which of the verifier values or HMAC fields was
  off — there is no oracle to consult.
- The HMAC key and value are a whole number of AES blocks when encrypted
  (SHA-1's 20 bytes are padded to 32) and compared over the hash's size, as
  2.3.4.14 says. msoffcrypto-tool 6.0.0 compares the padded block, so it
  rejects every SHA-1 file's integrity check, Office 2010's included.

## Test

`Tests/OfficeEncryptionTest.cpp` reads a workbook and a document encrypted by
msoffcrypto-tool, an implementation independent of this one
(`Tests/fixtures/office-agile-encrypted.{xlsx,docx}`;
`Tests/fixtures/make-office-encrypted-fixtures.py` rebuilds them). The password
is not ASCII, and the workbook spans five segments. It checks the right, a
wrong and a missing password, a changed byte of the package and of its size
field, a package cut short, extensible encryption, a damaged header, an absurd
spin count, an unknown hash, a missing integrity check, `CheckOfficePassword`,
and the spreadsheet, the Word reader and `UltraCanvasFileLoader::LoadTextDocument`
opening the files. `office-agile-sha1-encrypted.xlsx` is the workbook keyed as
Office 2010 keys it (SHA-1, AES-128), and `office-standard-encrypted.xlsx` the
workbook in Office 2007's standard encryption (AES-128-ECB); for the latter it
also checks RC4 and a cipher other than AES refused, and a truncated header
and package.

For writing it reads back what `UCCompoundFileWriter` builds (a mini-stream
stream, a sector stream, nested storages), round-trips packages of 200 bytes,
21 KB and 9 MB (past the header's 109 FAT sectors, so DIFAT sectors are
written), checks that two saves differ, that a wrong password and a changed
byte are caught, and saves and reopens through `SaveToFile`,
`UCWordDocumentIO::Save` and `UltraCanvasFileLoader::SaveTextDocument`. When
the writer was made, msoffcrypto-tool decrypted its output - integrity check
included - for packages in the mini stream, in sectors and past the DIFAT
limit, byte for byte.
