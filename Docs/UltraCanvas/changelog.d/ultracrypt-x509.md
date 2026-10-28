- **UltraCrypt: X.509 certificates and CMS** (`UltraCrypt/UltraCryptX509.h`),
  on OpenSSL 3's libcrypto as a second, optional backend - libsodium has no
  public-key infrastructure, and it stays the backend of everything
  UltraCanvas encrypts of its own.
  - Certificates: `UltraCrypt_ReadCertificate` (name, addresses, issuer,
    validity, SHA-256 fingerprint; PEM or DER) and
    `UltraCrypt_CheckCertificate` - a chain to the system's roots and/or roots
    given, through the intermediates given, for a purpose (any, mail signing,
    mail encryption), at a time: Trusted, Untrusted, Expired, NotYetValid,
    Revoked or WrongPurpose, with the reason.
  - CMS: `UltraCrypt_CmsVerifyDetached` / `CmsVerifyAttached` (whether the
    signature holds for exactly these bytes; who signed and the chain it
    carries), `CmsSignDetached` (SHA-256, chain carried), `CmsEncrypt`
    (AES-256-CBC, any number of recipients, RSA or EC) and `CmsDecrypt`
    (into a secure buffer), `CmsTypeOf`.
  - `UltraCrypt_ImportPkcs12`: a certificate, its chain and private key from
    a `.p12` / `.pfx` and its password, older RC2 / 3DES files included.
  - Wrapped completely: no OpenSSL type in a header, libcrypto linked
    privately by file (the installed package names no OpenSSL target), every
    call `BackendUnavailable` without OpenSSL 3 (`UltraCrypt_IsX509Available`).
    The macOS build names `openssl` in `MacOS/deps/vcpkg.json`.
  - `Tests/UltraCryptTests.cpp` checks them against a test authority made for
    the suite: the six verdicts, both PKCS #12 encryptions and a wrong
    password, signatures holding and refused, enveloped data opened and
    refused.
