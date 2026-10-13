- **The signing scripts no longer carry the certificate's password.**
  `SignUltraDemo.ps1` and `SignUltraTexter.ps1` had it written into
  their defaults, readable by anyone who could read the repository. They now
  read it from `ULTRACANVAS_SIGN_PFX_PASSWORD` (or `-PfxPassword`), and they
  stop with a message instead of signing when it is missing.
  - To change the password: delete the `.pfx`, set the new password and run
    `-Mode CreateAndSign`. The certificate already in the store is exported
    again under the new password.
  - `*.pfx` and `*.p12` are now ignored by git, since the scripts export the
    private key next to themselves in the repository root.
  - `-Description` sets the name the signature shows. The demo script had
    signed with "UltraTexter", so the installer would have been signed under
    that name. It now signs as "UltraCanvas", and the installer as
    "UltraCanvas Setup".
