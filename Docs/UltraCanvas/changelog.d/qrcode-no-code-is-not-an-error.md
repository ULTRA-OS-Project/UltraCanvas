- **QRCode: an image with no code in it is no longer reported as an error.**
  `ScanQRCodeFile` and `ScanQRCodeImage` set `errorMessage` to "No QR codes
  detected" whenever they found nothing, the same channel as an unreadable
  file or a missing decoder, so a caller wanting to say "nothing found"
  rather than "could not scan" had to match that text. Now an empty result
  with an empty error means no code, and the error is filled only when the
  scan could not run. The demo app already read it that way; UltraAuthenticator's
  scan dialog stops matching the string.
