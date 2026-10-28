- **UltraCrypt: the certificate check trusts what the operating system
  trusts.** `UltraCrypt_CheckCertificate`'s system roots now include Windows'
  ROOT and AuthRoot stores (less Disallowed) and macOS's system anchors plus
  the roots marked trusted in Keychain Access (`UltraCryptSystemRoots.cpp`,
  crypt32 / Security). OpenSSL's default store, the system's on Linux, held
  on Windows and macOS only what the OpenSSL build carried, so a real
  authority's certificate was not trusted there. `SSL_CERT_FILE` /
  `SSL_CERT_DIR`, when set, still replace them.
  - `ULTRACANVAS_BUILD_CRYPT_TESTS` builds UltraCryptTests without
    `BUILD_TESTS` (`Tests/CryptTests.cmake`); the Windows and macOS CI rows
    run it, and it checks that a root shipped with the system - Microsoft's
    on Windows, ISRG Root X1 on macOS and Linux - is trusted.
