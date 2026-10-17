- **The signing scripts no longer sign with the password published in the
  repository's history** (`SignUltraDemo.ps1`, `SignUltraTexter.ps1`).
  - Until now only `-Mode RotatePassword` refused it, as the new password.
    `-Mode Sign` and `-Mode CreateAndSign` signed and exported under it
    without a word, so a signing machine that was never rotated went on using
    it.
  - Every mode that uses the password now refuses it before `signtool` runs
    or anything is exported. The message names the steps:
    `ULTRACANVAS_SIGN_OLD_PFX_PASSWORD` set to the old password,
    `ULTRACANVAS_SIGN_PFX_PASSWORD` to a new one of 12 characters or more,
    then `-Mode RotatePassword` once.
  - `package-win.sh` stops at signing the same way; `--no-sign` still
    packages. `-Mode Verify` needs no password and is unaffected.
