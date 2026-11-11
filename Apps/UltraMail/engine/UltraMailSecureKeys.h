// Apps/UltraMail/engine/UltraMailSecureKeys.h
// The user's S/MIME identity for an account - the certificate (with its
// chain) and the private key imported from a PKCS #12 file - kept in
// UltraMail's vault beside the account's password: encrypted at rest, and
// behind the Primary Password when one is set. Two entries per account,
// "<account>.smime.cert" and "<account>.smime.key".
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailCredentialVault.h"
#include "UltraMailSecureMime.h"

#include <string>

namespace UltraMail {

bool StoreSmimeIdentity(CredentialVault& vault, const std::string& accountId, const SmimeIdentity& identity);
bool LoadSmimeIdentity(const CredentialVault& vault, const std::string& accountId, SmimeIdentity& out);
bool RemoveSmimeIdentity(CredentialVault& vault, const std::string& accountId);

} // namespace UltraMail
