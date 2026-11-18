# UltraCanvasOdfEncryption — password-protected .odt, .ods and .odp

An OpenDocument file saved *with password* by LibreOffice (or OpenOffice.org
3.2 and later) is still a ZIP package. The `mimetype` entry and
`META-INF/manifest.xml` are in the clear. Every other entry - `content.xml`,
`styles.xml`, the pictures - is deflated and then encrypted on its own, with
its own salt and IV, which the manifest records in a
`manifest:encryption-data` element. This module turns such a package back
into an ordinary one in memory, which the `.odt`, `.ods` and `.odp` readers
then read as usual.

Most code never calls it directly: the readers take the password as one more
argument, and `UltraCanvasFileLoader` asks for it.

Header: `UltraCanvasOdfEncryption.h`. Its statuses are
[UltraCanvasOfficeEncryption](UltraCanvasOfficeEncryption.md)'s
`OfficeDecryptStatus`, so one password prompt serves a protected `.docx` and
a protected `.odt` alike. The cryptography is UltraCrypt's
(`Docs/Modules/UltraCrypt/README.md`): AES-CBC, SHA-256, and PBKDF2 with
HMAC-SHA-1, which UltraCrypt offers only with `allowLegacy` because ODF names
no other.

## Opening one

<!-- doc-check: std::string path; std::string password; UltraCanvas::UltraCanvasWindowBase* window; void ShowError(const std::string& message); -->
```cpp
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasSpreadsheet.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"

using namespace UltraCanvas;

UCRichDocument document;                                         // .odt
std::string error;
if (!UCWordDocumentIO::Load(path, document, error, password)) ShowError(error);

auto sheet = std::make_shared<UltraCanvasSpreadsheet>("Book", 0, 0, 800, 600);
if (!sheet->LoadFromFile(path, password)) ShowError(sheet->GetLastError());   // .ods

PptxPresentation deck;                                           // .odp
if (!ReadOdpPresentation(path, deck, error, password)) ShowError(error);

// Or ask the user, as for a protected .docx: a password field, checked, asked
// again after a wrong one; a file without a password calls back at once.
UltraCanvasFileLoader::RequestDocumentPassword(path, window,
    [sheet, path](bool proceed, const std::string& typed) {
        if (proceed && !sheet->LoadFromFile(path, typed)) ShowError(sheet->GetLastError());
    });
```

`UltraCanvasFileLoader::OpenTextDocument` asks by itself for a protected
`.odt` or `.odp` chosen in its dialog, and `LoadTextDocument` /
`ExtractText` take the password. Without the password, or with a wrong one,
every reader fails with "The file is password-protected; ..." or "The
password is not correct", never that the file is damaged. UltraFiler's
preview has no password to ask for, so a protected file keeps its type
glyph. UltraTexter says that an `.odt` is protected rather than show its
bytes.

## Decrypting directly

<!-- doc-check: std::string path; std::string password; void ReadPackage(const std::vector<uint8_t>& zip); void ShowError(const std::string& message); -->
```cpp
#include "UltraCanvasOdfEncryption.h"

using namespace UltraCanvas;

if (IsEncryptedOdfPackage(path)) {
    std::string error;
    switch (CheckOdfPassword(path, password, error)) {   // one key derivation
        case OfficeDecryptStatus::Decrypted: break;          // the password is right
        default: ShowError(error); return;
    }
    std::vector<uint8_t> zip;
    if (DecryptOdfPackageFile(path, password, zip, error) == OfficeDecryptStatus::Decrypted)
        ReadPackage(zip);   // e.g. UCZipPackageReader::OpenFromMemory
    else
        ShowError(error);
}
```

| Function | Does |
|---|---|
| `IsEncryptedOdfPackage(path)` / `(UCZipPackageReader)` | true when the manifest gives any entry `manifest:encryption-data` (or the package is one whole encrypted entry) |
| `DecryptOdfPackageFile(path, password, zip, error)` | the package rebuilt unencrypted: `mimetype` first and stored, the entries in their order, the manifest without its encryption data |
| `DecryptOdfPackage(reader, password, zip, error)` | the same for a package already open |
| `CheckOdfPassword(path, password, error)` | the password checked against one entry's checksum (`content.xml`'s), nothing else decrypted |

The status is `Decrypted`; `NotEncrypted` for an ordinary package or a file
that is no ZIP (read it as it is); `PasswordRequired` for an empty password;
`WrongPassword`; `Unsupported`, naming what the file uses; `Damaged`; or
`NoCryptoBackend` in a build without UltraCrypt's backend.
`UltraCanvasOfficeEncryption`'s `CheckOfficePassword` answers through
`CheckOdfPassword` for a ZIP file, which is how `RequestDocumentPassword`
prompts for both kinds.

## What is supported

Read, per entry - ODF 1.2 / 1.3 Part 3 §3.8 and §4, what LibreOffice writes
by default:

1. the start key: SHA-256 of the password's UTF-8
   (`start-key-generation-name` `…xmldsig#sha256`), or SHA-1 (OpenOffice.org
   3.2's, and the default when the element is absent);
2. the key: PBKDF2-HMAC-SHA-1 over the start key with the entry's `salt` and
   `iteration-count` (100 000 in LibreOffice; more than 10 000 000 is refused),
   `key-size` bytes;
3. AES-256-CBC (or AES-128/192) with the entry's `initialisation-vector`; the
   last byte gives the padding to drop (W3C XML Encryption padding);
4. the checksum: SHA-256 (`…#sha256-1k`) or SHA-1 (`SHA1/1K`) of the first
   1024 bytes, compared in constant time. A mismatch is `WrongPassword`;
5. the raw deflate stream inflated to the entry's `manifest:size`. An entry
   stored as it is, at that size, is taken too.

Refused as `Unsupported`, each naming what it is:

- Blowfish CFB, as OpenOffice.org 1 to 3.1 saved;
- AES-GCM and the Argon2id key derivation of ODF 1.4;
- the whole-package encryption of ODF 1.4, which puts every entry into one
  encrypted `encrypted-package`.

ODF carries no integrity check beyond the checksum. A changed byte in the
first kilobyte of an entry looks like a wrong password; one past it shows as
a damaged entry ("The encrypted entry content.xml is damaged"), since the
deflate stream then fails.

There is no writer. A document or a workbook saved with a password is saved
as `.docx` or `.xlsx` ([UltraCanvasOfficeEncryption](UltraCanvasOfficeEncryption.md)),
and `UCWordDocumentIO::Save` / `SaveToFile` still refuse a password for
`.odt` and `.ods` rather than save the file unprotected.

## Implementation notes

- Every entry has its own salt, so every entry costs one key derivation:
  LibreOffice's six-entry `.odt` takes about half a second at 100 000 rounds.
  `CheckOdfPassword` derives one key, for the password prompt.
- PBKDF2-HMAC-SHA-1 is UltraCrypt's (`UltraCrypt_DeriveKeyPbkdf2` with
  `UltraCryptHashOptions::allowLegacy`), on its own SHA-1. libsodium has
  none. Each round after the first is two SHA-1 compressions of a block
  padded once.
- The inflate is `UCZipInflateRaw` (`UltraCanvasZipPackage.h`), the ZIP
  wrapper's miniz.

## Test

`Tests/OdfEncryptionTest.cpp` reads three files LibreOffice 24.2 saved with
the Office fixtures' non-ASCII password ("Ültra-Pässwört 1") through its UNO
API (`Tests/fixtures/make-odf-encrypted-fixtures.py`):
`odf-encrypted.odt` (`word97-formatting.odt`), `odf-encrypted.ods` (a small
sheet) and `odf-encrypted.odp` (`office-pptx-sample.odp`). It checks the
following:

- the encrypted packages are told from ordinary ones;
- no, a wrong and a nearly right password are refused;
- the decrypted package is laid out as an ordinary one;
- `CheckOdfPassword` and `CheckOfficePassword` answer for these files;
- each reader with the password gives what the unencrypted original gives;
- `LoadTextDocument` and `ExtractText` take the password;
- UltraFiler's preview keeps the glyph;
- a changed byte past the checksummed kilobyte is damaged;
- Blowfish, AES-GCM, Argon2id and a whole encrypted package are refused by
  name.

`UltraCryptTests` checks PBKDF2-HMAC-SHA-1 against RFC 6070.
