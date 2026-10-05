- **A scanner or printer that speaks only HTTPS is now reachable.** Such a
  device presents a certificate it signed itself, which ordinary verification
  refuses, so an eSCL scanner offering only `https://` or a printer offering
  only `ipps://` could not be used. Both backends now trust the device on
  first use, as SSH does a host key: a certificate that verifies is used as
  it is; the first time one fails, the device's public key is read over a
  connection that sends nothing but `HEAD /` and remembered, and every later
  connection is pinned to it. A device that presents a different key is
  refused - never relearned - with a message saying how to forget the old
  one: UOS-Settings' *Devices > Trusted certificates* page lists every
  trusted device with a *Forget* button (`IODeviceForgetCertificate()` in
  code). Keys are kept in
  `DeviceCertificates.conf` in the UltraCanvas settings folder;
  `ULTRACANVAS_DEVICE_TLS_TOFU=0` stops new ones being learned.
- **UltraNet: public-key pinning.** `UltraNetHttpOptions::pinnedPublicKey`
  accepts a server only when its certificate carries that key, checked even
  with `acceptInvalidCert`; a mismatch is the new `TlsPublicKeyMismatch`, and
  a TLS backend that cannot pin turns verification back on rather than drop
  the pin. `capturePeerCertificate` fills `UltraNetResponse::tlsInfo`, which
  was never filled before, and `UltraNet_PublicKeyPinOf()` computes a pin
  from a certificate - the same value `openssl` prints for it.
- **`UltraCanvasSettingsFolder()`** names the per-user folder every
  application keeps settings in; the file dialog's settings now use it too.
