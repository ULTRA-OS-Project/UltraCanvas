// Apps/UltraAuthenticator/RecoveryCode.h
// The server share of a vault's recovery key (EncryptedFileStore.h) written
// as a code a person can print and type back: the 32 bytes in Base32 without
// padding, 52 characters in thirteen groups of four -
// "ABCD-EFGH-...". Base32 because it has no look-alike characters to misread
// off paper and is case-insensitive to type; groups because nobody copies 52
// characters in one run.
//
// The code is one half of what opens the vault - the other half is in the
// vault file - so it is a secret in the same sense a backup passphrase is:
// shown once, on screen only, never written to disk by the app.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef RECOVERYCODE_H
#define RECOVERYCODE_H

#include "UltraCrypt/UltraCryptCore.h"

#include <string>

namespace UltraCanvas {
namespace Authenticator {

// "ABCD-EFGH-..." for a 32-byte share; empty for anything else.
std::string FormatRecoveryCode(const UltraCryptSecureBuffer& share);

// The share from a code as typed: dashes, spaces and case are ignored. False
// for anything that is not exactly 32 bytes of Base32.
bool ParseRecoveryCode(const std::string& text, UltraCryptSecureBuffer& outShare);

// The share as the account service keeps it: 64 lower-case hex digits; and
// back. The answer of POST /v1/recovery-keys/{id}/release.
std::string ShareToHex(const UltraCryptSecureBuffer& share);
bool ShareFromHex(const std::string& hex, UltraCryptSecureBuffer& outShare);

} // namespace Authenticator
} // namespace UltraCanvas

#endif // RECOVERYCODE_H
