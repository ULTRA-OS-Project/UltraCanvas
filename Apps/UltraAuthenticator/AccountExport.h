// Apps/UltraAuthenticator/AccountExport.h
// The encrypted backup file: every account in one portable, password-protected
// blob (UltraAuthenticator-Investigation.md §3.5).
//
// This is the answer to the one question the app could not previously answer:
// *what happens when the machine dies?* Showing one account's setup key
// (RevealSecretDialog) moves a single login to a new phone. It is no use at
// all for restoring forty accounts onto a replacement laptop, and a user who
// has no backup path invents one — screenshots of QR codes in a photo album,
// a text file of Base32 keys — every option worse than this.
//
// Two rules shape the format:
//
//  - **Its own passphrase, never the master password.** The investigation is
//    explicit about this: a backup is the file most likely to end up somewhere
//    the vault never would — a USB stick, a cloud drive, an email to oneself.
//    If it opened with the device password, finding the backup would be as
//    good as having the machine. Exporting refuses a passphrase equal to the
//    master password rather than merely advising against it.
//  - **The same envelope as everything else.** Argon2id + XChaCha20-Poly1305
//    via UCDCrypto::Seal, the framework's vetted password envelope, rather
//    than a third hand-rolled container. Cost parameters travel with the file,
//    so raising them later cannot orphan an old backup.
//
// File layout:
//
//   offset size field
//   0      8    magic "UCAEXPRT"
//   8      1    format version (1)
//   9      1    reserved (0)
//   10     ...  UCDCrypto envelope (its own header, then ciphertext || tag)
//
// The 10-byte header is passed to Seal as associated data, so editing it
// invalidates the tag instead of being obeyed.
//
// Decrypted payload: a 4-byte count, then per account a 2-byte length and that
// account's `otpauth://` URI. The URI is the unit deliberately — it is what
// the vault already stores, what every other authenticator understands, and
// what this app's tested parser already validates. A backup written in some
// bespoke shape would need its own validator and could drift from the one that
// guards the live vault.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ACCOUNTEXPORT_H
#define ACCOUNTEXPORT_H

#include "store/ISecretStore.h"

#include <string>
#include <vector>

namespace UltraCanvas {
namespace Authenticator {

// Bounds on a file that has not been authenticated yet, so a malformed or
// hostile backup cannot make the reader allocate wildly before the tag check
// rejects it.
constexpr size_t kMaxExportAccounts = 4096;
constexpr size_t kMaxExportUriBytes = 4096;
constexpr size_t kMaxExportFileBytes = 16 * 1024 * 1024;

// Seals `uris` into `path`. Each entry is a complete otpauth:// URI and is
// therefore secret; the caller keeps them in secure buffers and this function
// wipes every copy it makes.
//
// Writes atomically (temp file, then rename) at mode 0600, for the same reason
// the vault does: a half-written backup that looks complete is worse than no
// backup, and a backup is as sensitive as the vault itself.
StoreResult SealAccountExport(const std::vector<UltraCryptSecureBuffer>& uris,
                              const UltraCryptSecureBuffer& passphrase,
                              const std::string& path);

// Reads `path` back. A wrong passphrase and a tampered file are reported
// identically, inherited from the envelope.
StoreResult OpenAccountExport(const std::string& path,
                              const UltraCryptSecureBuffer& passphrase,
                              std::vector<UltraCryptSecureBuffer>& outUris);

} // namespace Authenticator
} // namespace UltraCanvas

#endif // ACCOUNTEXPORT_H
