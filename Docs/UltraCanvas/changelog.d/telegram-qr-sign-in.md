- **UltraIM: Telegram signs in by a QR code.** Telegram has, for stretches
  since early 2026, answered a third-party application's sign-in with "the
  code was sent to your Telegram app" and then shown no code in any app, with
  no SMS or call on offer - a sign-in with nowhere to go. A QR code needs no
  code: `UltraIM_SignIn` with `credentials.loginType = QRCode` and no number
  asks TDLib for a sign-in link (`requestQrCodeAuthentication`), the new
  step `UltraIMAuthStep::OtherDevice` carries the `tg://login` link as its
  hint for the application to show as a QR code, Telegram's refreshes of
  the link arrive as `AuthStepChange`, and once the phone's Telegram app
  scans it (Settings, Devices, Link Desktop Device) the sign-in goes on by
  itself - the two-step password when the account has one, else signed in.
  It can be asked for in place of a code that never came; a number given
  while the QR code waits starts over with the number. The link holds the
  sign-in token and is never logged. `FakeTdJson` plays the other device
  (`fakeScanLoginToken`, `fakeRefreshLoginToken`), `TelegramProviderTests`
  walks the steps, and `TelegramTdlibTests` hands the request to the real
  TDLib.
