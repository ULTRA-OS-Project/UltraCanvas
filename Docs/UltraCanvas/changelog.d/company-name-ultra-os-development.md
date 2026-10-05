- **The company is now ULTRA OS Development GmbH.** The old name, Cloverleaf
  UG, is replaced wherever the repository names the company: the copyright line
  in `LICENSE`, the licence and "Developed by" lines of `README.md` and
  `Docs/UltraCanvas/README.md`, the footers of the module READMEs, the
  copyright string every macOS app bundle shows in its About box
  (`NSHumanReadableCopyright`, written by `package-macos.sh`), and the
  publisher name `SignUltraTexter.ps1` and `SignUltraDemo.ps1` put in the
  self-signed Windows code-signing certificate they create.
  - A machine that already has the old self-signed certificate keeps signing
    with it while its `.pfx` file is present. Delete the `.pfx` and run the
    script once with `-Mode CreateAndSign` to get one in the new name.
  - Three things keep the old name on purpose. The macOS signing identity in
    `package-macos.sh` must match the name inside the Apple Developer ID
    certificate, which Apple issued to Cloverleaf RISCOS Computer UG; it
    changes when Apple reissues the certificate. The `com.cloverleaf.*`
    bundle identifiers stay, because macOS files each app's preferences,
    keychain items and privacy permissions under them. Test data that
    happens to contain the old name is test data, not a reference to the
    company.
