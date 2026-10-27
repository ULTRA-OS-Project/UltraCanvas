// Apps/UltraMail/engine/UltraMailSecureBackends.h
// The two libraries behind UltraMailSecureMime.h, each in a file of its own:
// S/MIME on UltraCrypt's X.509 and CMS (UltraMailSmime.cpp; UltraCrypt says
// whether it has them) and OpenPGP on GPGME (UltraMailOpenPgp.cpp, stubs
// without ULTRAMAIL_HAVE_OPENPGP). Only the byte work is here; the MIME
// around it is UltraMailSecureMime.cpp's. Not a public header.
// Version: 0.2.0 - S/MIME on UltraCrypt
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailSecureMime.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraMail::secure {

// ---- S/MIME ----------------------------------------------------------------
bool SmimeBuilt();
// A detached signature (DER) over `content` (canonical, CRLF).
SignatureCheck SmimeVerifyDetached(const std::string& content, const std::vector<uint8_t>& signature);
// Signed data that carries its content: the content, and the check.
bool SmimeVerifyOpaque(const std::vector<uint8_t>& der, std::string& content, SignatureCheck& check);
// Is this CMS structure signed data (as opposed to enveloped)?
bool SmimeIsSignedData(const std::vector<uint8_t>& der);
// Enveloped data opened with whichever identity it was encrypted to.
bool SmimeDecrypt(const std::vector<uint8_t>& der, const std::vector<SmimeIdentity>& identities,
                  std::string& content, std::string& error);
bool SmimeSignDetached(const std::string& content, const SmimeIdentity& identity,
                       std::vector<uint8_t>& signature, std::string& error);
bool SmimeEncrypt(const std::string& content, const std::vector<std::string>& certificatesPem,
                  std::vector<uint8_t>& der, std::string& error);
bool SmimeImportPkcs12(const std::vector<uint8_t>& p12, const std::string& password,
                       SmimeIdentity& out, std::string& error);
bool SmimeReadCertificate(const std::string& pem, CertificateInfo& out);

// ---- OpenPGP ---------------------------------------------------------------
bool OpenPgpBuilt();
// A detached, armored signature over `content` (canonical, CRLF).
SignatureCheck PgpVerifyDetached(const std::string& content, const std::string& signature);
// An encrypted (and perhaps signed) armored message: the plaintext and the
// signatures found in it.
bool PgpDecrypt(const std::string& armored, std::string& plain, std::vector<SignatureCheck>& signatures,
                std::string& error);
// A clear-signed text ("-----BEGIN PGP SIGNED MESSAGE-----"): its text and the check.
bool PgpVerifyClearSigned(const std::string& clearSigned, std::string& text, SignatureCheck& check);
// `content` signed by the secret key for `signer`: an armored detached
// signature and the micalg it was made with ("pgp-sha256").
bool PgpSignDetached(const std::string& content, const std::string& signer, std::string& signature,
                     std::string& micalg, std::string& error);
// `content` encrypted to `recipients` (and signed by `signer` unless empty), armored.
bool PgpEncrypt(const std::string& content, const std::vector<std::string>& recipients,
                const std::string& signer, std::string& armored, std::string& error);
bool PgpHasSecretKey(const std::string& email);
// The addresses with no usable public key in the keyring.
std::vector<std::string> PgpMissingKeys(const std::vector<std::string>& emails);
int PgpImportKeys(const std::string& armored, std::string& error);

} // namespace UltraMail::secure
