- **The signed macOS apps could not open the camera or the microphone.** Every
  app is signed with the hardened runtime, which refuses a device the app has
  no entitlement for, and `MacOS/entitlements.plist` had neither
  `com.apple.security.device.camera` nor
  `com.apple.security.device.audio-input`. The generated `Info.plist` had no
  `NSCameraUsageDescription` or `NSMicrophoneUsageDescription` either, and
  macOS terminates an app that opens the device without one. So in the
  notarized suite, UltraAuthenticator's QR scan and the demo's video and audio
  recorders could not open a camera or a microphone. `package-macos.sh` now
  takes both from one line per app (`camera_usage`, `microphone_usage`). The
  reason goes into the app's `Info.plist` and the entitlement into a per-app
  copy of `MacOS/entitlements.plist` that the app is signed with, so the two
  cannot disagree. Apps that open neither device get no device entitlement.
  - UltraAuthenticator: camera. UltraCanvas Demo: camera and microphone.
- **Every macOS app now states why it uses the local network.** Since macOS 15
  the system asks before an app reaches the local network. Any app that prints
  browses Bonjour for IPP printers, and DeviceExplorer browses for eSCL
  scanners as well, so every app's `Info.plist` now has
  `NSLocalNetworkUsageDescription` and an `NSBonjourServices` list of the four
  service types the framework browses (`_ipp._tcp`, `_ipps._tcp`,
  `_uscan._tcp`, `_uscans._tcp`).
- `package-macos.sh` checks each generated `Info.plist` and entitlements file
  with `plutil -lint` while it packages, so the unsigned pull-request build
  catches a malformed one. A release build logs the entitlements each app was
  signed with.
