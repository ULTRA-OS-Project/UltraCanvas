- **One-time passwords are UltraCrypt's.** The HOTP / TOTP engine and the
  `otpauth://` parser and builder move from `Apps/UltraAuthenticator/otp/`
  into the UltraCrypt module (`include/UltraCrypt/UltraOtp.h`,
  `OtpAuthUri.h`), with the checking side a sign-in needs:
  `Otp::VerifyTotp` (a time step either side, each code once, every
  candidate compared in constant time) and `Otp::NewTotpSecret`.
  UltraAuthenticator and its tests use them from there.
- **`DeviceKeyVault::UseWithPassphrase`.** `Use()` for a vault behind a
  Primary Password: opened with the typed passphrase, used and closed in one
  turn of the process's lock, so no other vault takes the store in between.
- **UltraIM: the account's profile and its sign-in protection.**
  `UltraIMAccountProfile` (name, e-mail, phone) through
  `UltraIM_FetchAccountProfile` / `UltraIM_UpdateAccountProfile`, and
  `UltraIMSignInProtection` - a password and/or an authenticator code -
  through `UltraIM_GetSignInProtection`, `UltraIM_StartAuthenticatorSetup`
  (an `otpauth://` link for a QR code) and `UltraIM_SetSignInProtection`;
  a protected sign-in answers `NeedsInput` with the step `Password`, then
  `Code`. `credentials.email` goes with a new account. ChatCube Direct
  implements them; its relay (1.1.0) keeps the e-mail and the phone for the
  owner alone (`GET` / `PUT /v1/profile`).
