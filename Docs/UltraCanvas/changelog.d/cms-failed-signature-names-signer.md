- **A signature that does not hold names its signer on every OpenSSL build.**
  `UltraCrypt_CmsVerifyDetached` / `UltraCrypt_CmsVerifyAttached` took the
  signer from `CMS_get0_signers`, which lists only the certificates
  `CMS_verify` attached - and the Windows CI row's OpenSSL attaches none to a
  signature whose content was changed, so `signerCertificatePem` came back
  empty there and `UltraCryptTests` failed ("the signer is still named"). The
  signer's certificate is now also looked up among the ones the signature
  carries, by its signer info's identifier.
