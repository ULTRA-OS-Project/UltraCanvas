- **Download fetches the SDK itself again, from start.ultraos.eu, checked
  against SHA256SUMS.** Step 4's *Download page* opened
  ultraos.eu/download-file in the browser, and you had to find the archive and
  pick it with *Unpack...*. Every release build of `main` now uploads its six
  SDK archives, with a `SHA256SUMS`, to
  `https://start.ultraos.eu/pub/<version>/` (framework changelog:
  `sdk-website-upload`). So *Download* asks for a folder, downloads this
  version's archive for this platform and architecture into it, and unpacks
  it there. It fills the SDK prefix in, as *Unpack...* does.
  - Steps:
    1. `SHA256SUMS` first: a 404 says the version is not on the server
       (yet), and names the GitHub release that has the same archive.
    2. The archive, into `<archive>.part`, with progress in the status line.
    3. Its SHA-256, which must be the one `SHA256SUMS` lists.
    4. Only then is it renamed to its own name.
  - A transfer that fails, an archive the sums do not list and a checksum
    that differs each end with a message saying which, and no file left
    behind.
  - The row under the matching SDK is *Download from*, the archive's own
    address, where *Download page* was.
  - A build without the network library opens that address in the browser
    instead.
  - The guide, the plan's SDK step and the step's explanation name the
    address and the button.
  - UltraNet is part of the core library now, so this adds nothing to what
    the application links. The download is `StartSdk` 0.3.0: `DownloadSdk`,
    `SdkDownloadBase`, `SdkDownloadUrl`, `SdkChecksumsUrl`, `ChecksumFor`
    and `SdkDownloadAvailable`; `SdkDownloadPage` is gone.
  - Tests: the addresses, `ChecksumFor` on `sha256sum` output, and
    `DownloadSdk` failing cleanly without a server.
  - Checked by hand against a local copy of the server's `/pub` layout, with
    the real 0.9.465 Linux SDK:
    - the archive arrived byte-identical and its SHA-256 matched;
    - a second download replaces it;
    - a wrong checksum, an unlisted archive, an unknown version and a listed
      but missing archive each fail with no file left.
