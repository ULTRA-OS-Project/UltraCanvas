- **VideoFX 0.11.0: proxy media.** Large clips, such as 4K phone footage,
  can now be edited through small stand-ins. A proxy is the clip at 540 lines,
  H.264 with a keyframe every half second, the same sound and the same
  timing. The editor previews and plays from the proxies, and the export
  reads the originals.
  - New: `VideoFXProxySettings`, `VideoFX_CreateProxy`,
    `VideoFX_CreateProxies` (every clip of a timeline once, one progress),
    `VideoFX_UseProxies` (the timeline switched to them), `VideoFX_ProxyPath`,
    `VideoFX_HasProxy`, `VideoFX_ClearProxies` and
    `VideoFX_GetDefaultProxyDirectory`.
  - Proxies go in the user's cache folder (`~/.cache`, `~/Library/Caches`,
    `%LOCALAPPDATA%`, then `VideoFX/Proxies`) or a folder the app names.
    Each is named after the clip and a hash of its path, size and
    modification time, so an edited clip gets a new proxy.
  - A proxy is written under a temporary name and renamed when whole, so a
    crash or cancel never leaves a half one. Clips no taller than a proxy,
    and sound files, get none.
  - Switching keeps trims, speed and overlays and scales a `Crop` to the
    proxy's pixels.
  - New export setting `VideoFXExportSettings::keyframeInterval` (seconds
    between keyframes), also saved in project files.
  - `videofx proxy <clips>|<project>`, `videofx proxy --clear`, and
    `--proxies` on `videofx render` and `videofx preview` for a quick draft.
