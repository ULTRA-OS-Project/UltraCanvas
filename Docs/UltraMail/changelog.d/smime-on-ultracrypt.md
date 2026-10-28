- **S/MIME runs on UltraCrypt.** Certificates, their trust check, signing,
  checking, encrypting, decrypting and PKCS #12 import are UltraCrypt's X.509
  and CMS (`UltraCryptX509.h`); `UltraMailSmime.cpp` keeps only what each
  finding means to the reader, and UltraMail links no OpenSSL of its own. A
  certificate that holds but is not one for signing mail now says so.
