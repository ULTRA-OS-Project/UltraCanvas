// include/UltraCanvasOdfEncryption.h
// Opens password-protected OpenDocument files - an .odt, .ods or .odp (and
// their templates) saved with "Save with password" by LibreOffice or
// OpenOffice.org 3.2 and later.
//
// Unlike a protected Office file (UltraCanvasOfficeEncryption.h) such a file
// is still a ZIP package: the mimetype entry and the manifest are in the
// clear, and each other entry - content.xml, styles.xml, the pictures - is
// deflated and then encrypted on its own, with its own salt and IV, which
// META-INF/manifest.xml records in a manifest:encryption-data element.
// DecryptOdfPackage turns the package back into an ordinary one, which the
// .odt, .ods and .odp readers then read as usual.
//
// Read: what ODF 1.2 / 1.3 (Part 3, §3.8 and §4) defines and LibreOffice writes
// by default - per entry, in order:
//   1. the start key: SHA-256 (or SHA-1, OpenOffice.org 3.2's) of the
//      password's UTF-8;
//   2. the key: PBKDF2 with HMAC-SHA-1 over the start key, the entry's salt
//      and its iteration count (100000 in LibreOffice), 32 bytes;
//   3. AES-256-CBC with the entry's IV; the last byte says how many bytes of
//      padding to drop (W3C XML Encryption padding);
//   4. the checksum - SHA-256 ("sha256-1k") or SHA-1 ("SHA1/1K") of the first
//      1024 bytes - which tells a wrong password from a right one;
//   5. the raw deflate stream inflated to the entry's size.
// Not supported, each refused as Unsupported naming what the file uses:
// Blowfish (OpenOffice.org 1 to 3.1), AES-GCM and Argon2id (ODF 1.4), and the
// whole-package encryption that puts every entry into one encrypted one.
// ODF has no integrity check beyond the checksum, so a changed byte past the
// first kilobyte shows only as a damaged entry.
//
// Cryptography is UltraCrypt's: its AES-CBC, SHA-2 and PBKDF2 - with SHA-1
// asked for as a legacy algorithm, since ODF names no other PRF. There is no
// writer: a document saved with a password is saved as .docx / .xlsx
// (UltraCanvasOfficeEncryption.h).
//
// Version: 1.0.0 - IsEncryptedOdfPackage, DecryptOdfPackage, DecryptOdfPackageFile, CheckOdfPassword
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasOfficeEncryption.h"   // OfficeDecryptStatus

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

class UCZipPackageReader;

// True for an OpenDocument package whose manifest gives any entry
// manifest:encryption-data (or that is a whole encrypted package). A file
// that is no ZIP, or no ODF package, answers false.
bool IsEncryptedOdfPackage(const UCZipPackageReader& package);
bool IsEncryptedOdfPackage(const std::string& filePath);

// Decrypts every encrypted entry and builds the package again - mimetype
// first and stored, the manifest without its encryption data - into
// `outPackage`. `password` is UTF-8. The status says what happened, as for an
// Office file: Decrypted, NotEncrypted (read the file as it is),
// PasswordRequired (the password was empty), WrongPassword, Unsupported,
// Damaged, NoCryptoBackend; `error` holds a sentence a user can be shown.
OfficeDecryptStatus DecryptOdfPackage(const UCZipPackageReader& package, const std::string& password,
                                      std::vector<uint8_t>& outPackage, std::string& error);
// The same for a file on disk (UTF-8 path). NotEncrypted for a file that is
// no encrypted ODF package - an ordinary .odt, a .docx, a text file.
OfficeDecryptStatus DecryptOdfPackageFile(const std::string& filePath, const std::string& password,
                                          std::vector<uint8_t>& outPackage, std::string& error);

// Checks a password by one entry's checksum, without decrypting the rest -
// for a prompt that asks again after a wrong one. Decrypted means "the
// password is right"; every other status is what DecryptOdfPackageFile would
// return. UltraCanvasOfficeEncryption's CheckOfficePassword answers through it
// for an OpenDocument file, so UltraCanvasFileLoader::RequestDocumentPassword
// asks for an .odt's password as for a .docx's.
OfficeDecryptStatus CheckOdfPassword(const std::string& filePath, const std::string& password,
                                     std::string& error);

} // namespace UltraCanvas
