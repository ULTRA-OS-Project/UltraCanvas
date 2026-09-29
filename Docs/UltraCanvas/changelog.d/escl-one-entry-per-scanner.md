- **An eSCL scanner that offers TLS was listed twice.** Such a scanner
  advertises both `_uscan._tcp` and `_uscans._tcp`, and discovery keyed each
  entry on its URL, which differs between the two - so the scanner appeared
  once as `escl:http://...` and again as `escl:https://...`. It is now
  recognised by the `uuid` in its TXT record (compared without regard to case
  or a `urn:uuid:` prefix), or by its host when it gives none, and listed
  once: over plain HTTP, since its certificate is almost always self-signed
  and TLS verification stays on, with the TLS address kept in the
  `escl-tls-url` attribute. Checked against one scanner advertised both ways
  over Avahi: two entries before, one after. `EsclScannerIdentity` is new and
  tested in `IODeviceScannerESCLTest`.
  - A stale comment went with it: `ULTRACANVAS_ESCL_SCANNERS` was described
    as the only way to reach a scanner on Windows, which stopped being true
    when the mDNS plugin learned to resolve there.
