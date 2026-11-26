- **The SDK server names its newest version in `pub/LATEST`.** After it has
  uploaded a release's archives and `SHA256SUMS`,
  `scripts/upload-sdk-website.sh` writes `<ULTRAOS_SFTP_DIR>/LATEST`, one line
  with the version. It writes `LATEST.part` and renames it, the way the
  archives go up, and leaves the file alone when it already names a newer
  version (`sort -V`), so a re-run of an old release does not move it back.
  `--latest <version>` sets it by hand once that version's `SHA256SUMS` is on
  the server; the *SDK website check* workflow runs that when it is started
  with a version in its new `latest` input - the way back after a bad
  release, and the way to start `LATEST` for the releases uploaded before.
  UltraCanvasStart reads the file to offer a newer SDK (UltraCanvasStart
  changelog: `update-to-the-newest-sdk`; `Docs/UltraCanvasSDK.md` 1.4.0).
