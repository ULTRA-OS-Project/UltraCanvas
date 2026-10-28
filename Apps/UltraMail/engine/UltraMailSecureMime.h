// Apps/UltraMail/engine/UltraMailSecureMime.h
// Signed and encrypted mail: S/MIME (RFC 8551, on UltraCrypt's CMS) and
// OpenPGP (RFC 3156 PGP/MIME and inline PGP, with GnuPG through GPGME).
//
// Reading: OpenSecureMessage takes a raw message as it was received and
// returns the message as it reads - decrypted where a key opens it, the
// signature wrapping taken off - with what was checked: who signed it, whether
// the signature holds for exactly these bytes, whether the signer is vouched
// for (an S/MIME certificate that chains to a trusted root, an OpenPGP key
// the user's keyring trusts), and whether the signer is the From address.
// Nested forms are taken apart in turn (encrypted, then signed inside).
//
// Sending: SecureOutgoingMessage takes the message UltraNet built for sending
// and returns it signed (multipart/signed) and/or encrypted
// (application/pkcs7-mime enveloped-data, or PGP/MIME multipart/encrypted) -
// encrypted to every recipient and to the sender, so the copy in Sent can be
// read again.
//
// Keys: an S/MIME identity (the user's certificate and private key, imported
// from a PKCS #12 file) is kept in UltraMail's vault; correspondents'
// certificates are learnt from the signed mail they send (SecureKeys says
// where they are kept). OpenPGP keys are GnuPG's: the user's own keyring and
// gpg-agent, which asks for a key's passphrase itself.
//
// Each method works where its library is: S/MIME where UltraCrypt has X.509
// (UltraCrypt_IsX509Available: OpenSSL 3), OpenPGP where GPGME is built in
// (ULTRAMAIL_HAVE_OPENPGP). Without one, a message in that form is reported
// as such ("cannot be checked in this build") and is never sent in it.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraMail {

enum class SecureMethod { Plain, Smime, OpenPgp };   // Plain: neither

// "S/MIME", "OpenPGP", "".
const char* SecureMethodName(SecureMethod method);

// Whether this build can do it.
bool SmimeAvailable();
bool OpenPgpAvailable();

// ---- raw MIME, as the secure forms need it ---------------------------------

// The header block (up to, not including, the empty line) and the body.
// False when there is no empty line.
bool SplitEntity(const std::string& entity, std::string& headers, std::string& body);
// A header's value, unfolded; "" when absent. Case-insensitive name.
std::string EntityHeader(const std::string& headers, const std::string& name);
// "multipart/signed" and its parameters (names lowercased, values unquoted).
std::string MediaType(const std::string& contentType);
std::string MediaTypeParameter(const std::string& contentType, const std::string& name);
// The parts of a multipart body, each exactly as it stands between its
// boundary lines (the CRLF before a boundary belongs to the boundary).
std::vector<std::string> MultipartParts(const std::string& body, const std::string& boundary);
// Bare LF line ends made CRLF - the canonical form signatures are made over.
std::string CanonicalCrlf(const std::string& text);
// A part's body decoded from its Content-Transfer-Encoding (base64,
// quoted-printable, or as it is).
std::string DecodedBody(const std::string& part);
// `outer` with its content - the Content-* headers and the body - replaced
// by `entity` (a MIME entity: its own Content-* headers, an empty line, its
// body). The other headers (From, Subject, Date, ...) stay.
std::string ReplaceContent(const std::string& outer, const std::string& entity);

// ---- reading ---------------------------------------------------------------

enum class SignatureVerdict {
    Valid,          // holds, and the signer is vouched for
    Untrusted,      // holds, but nothing vouches for the signer
    Expired,        // holds, but the certificate or key has expired
    Revoked,        // the certificate or key was revoked
    UnknownSigner,  // the signer's key is not known here (OpenPGP), so it cannot be checked
    Invalid,        // does not hold: the message was changed after it was signed
    Unchecked       // could not be checked (no library, a damaged signature)
};

struct SignatureCheck {
    SecureMethod     method = SecureMethod::Plain;
    SignatureVerdict verdict = SignatureVerdict::Unchecked;
    std::string      signerName;     // the certificate's or key's name
    std::string      signerEmail;    // lowercased; "" when it names none
    std::string      fingerprint;    // SHA-256 of the certificate, or the key's fingerprint
    std::string      issuer;         // S/MIME: who issued the certificate
    int64_t          validUntil = 0; // S/MIME: the certificate's end, seconds since 1970
    bool             matchesFrom = false;   // signerEmail is the message's From address
    std::string      detail;         // a sentence on what was found
    std::string      certificatePem; // S/MIME: the signer's certificate, to learn it
};

struct SecureMessage {
    bool        encrypted = false;   // it came encrypted
    bool        decrypted = false;   // ... and a key here opened it
    SecureMethod encryption = SecureMethod::Plain;
    std::vector<SignatureCheck> signatures;   // outermost first
    std::string raw;                 // the message as it reads (the input when nothing was done)
    std::string problem;             // why it could not be opened or checked; "" when nothing went wrong
};

// The user's S/MIME identity: their certificate (and the chain up to its
// issuer, PEM, the certificate first) and private key (PEM).
struct SmimeIdentity {
    std::string certificatePem;
    std::string privateKeyPem;
};

// Where the keys are. Every hook may be empty.
struct SecureKeys {
    // The user's S/MIME identities - all of them, to try each on an
    // encrypted message - and the one for an address (sending).
    std::function<std::vector<SmimeIdentity>()> smimeIdentities;
    std::function<bool(const std::string& address, SmimeIdentity& out)> smimeIdentityFor;
    // A correspondent's S/MIME certificate (PEM), learnt earlier.
    std::function<bool(const std::string& address, std::string& certificatePem)> smimeCertificateFor;
};

// What form a raw message is in, from its headers and body alone: the
// method it is signed or encrypted with, and whether encrypted.
SecureMethod SecureFormOf(const std::string& raw, bool* encrypted = nullptr);

// The message decrypted and its signatures checked, as far as this build and
// the keys allow. Never throws; a message in no secure form comes back as it is.
SecureMessage OpenSecureMessage(const std::string& raw, const SecureKeys& keys);

// ---- sending ---------------------------------------------------------------

struct SecureSendOptions {
    SecureMethod method = SecureMethod::Plain;
    bool        sign = false;
    bool        encrypt = false;
    std::string from;                        // the sender's address (its identity or key)
    std::vector<std::string> recipients;     // every To, Cc and Bcc address
};

// The recipients nothing can be encrypted to: no certificate learnt (S/MIME),
// no usable public key in the keyring (OpenPGP).
std::vector<std::string> MissingRecipientKeys(const SecureSendOptions& options, const SecureKeys& keys);
// Whether the sender can sign: an S/MIME identity for the address, an
// OpenPGP secret key for it.
bool CanSignAs(SecureMethod method, const std::string& address, const SecureKeys& keys);

// `raw` (a whole message, as UltraNet built it) signed and/or encrypted as
// `options` say. False, with `error` naming what is missing, when it cannot
// be - nothing half-done is returned.
bool SecureOutgoingMessage(const std::string& raw, const SecureSendOptions& options,
                           const SecureKeys& keys, std::string& out, std::string& error);

// ---- keys ------------------------------------------------------------------

// An S/MIME identity from a PKCS #12 file (.p12 / .pfx) and its password.
// False, with `error`, for a wrong password or a file without a key.
bool ImportPkcs12(const std::vector<uint8_t>& p12, const std::string& password,
                  SmimeIdentity& out, std::string& error);

// What a certificate (PEM) says: its name, the addresses it is for (the
// subject's emailAddress and the rfc822 alternative names, lowercased), its
// issuer, its end, its SHA-256 fingerprint. False for no certificate.
struct CertificateInfo {
    std::string name;
    std::vector<std::string> emails;
    std::string issuer;
    int64_t     validUntil = 0;
    std::string fingerprint;
};
bool ReadCertificate(const std::string& certificatePem, CertificateInfo& out);

// OpenPGP keys in an armored block (an application/pgp-keys attachment)
// added to the user's GnuPG keyring; the number imported or already there.
int ImportOpenPgpKeys(const std::string& armored, std::string& error);

} // namespace UltraMail
