- **CI: each release's SDKs go to the UltraOS website too, not only to the
  GitHub release.** UltraCanvasStart's Framework step opens
  https://ultraos.eu/download-file/ and names the archive for its version,
  but nothing put that archive on the website's server: until someone copied
  the six files over by hand, the page did not have the version the
  application asked for. A new `publish-sdk-website` job in
  `.github/workflows/build.yml` runs after `publish-sdk`. It downloads the
  same six `UltraCanvas-SDK-*` archives and uploads them over SFTP with
  `scripts/upload-sdk-website.sh`, as
  `<ULTRAOS_SFTP_DIR>/<version>/<archive>` plus a `SHA256SUMS`.
  - SFTP only, in batch mode, so a hosting account with no shell and no rsync
    is enough. Each file goes up as `<name>.part` and is renamed once
    complete, so the page never serves half an archive under its real name.
    A re-run of the same version replaces the files; older versions stay.
  - The server's host key is checked against the `ULTRAOS_SFTP_KNOWN_HOSTS`
    secret, never accepted on first sight. The private key is the
    `ULTRAOS_SFTP_KEY` secret. Host, user, directory and port are
    `ULTRAOS_SFTP_HOST`, `ULTRAOS_SFTP_USER`, `ULTRAOS_SFTP_DIR` and
    `ULTRAOS_SFTP_PORT` (22 when empty), each a repository variable or, when
    no variable has it, a secret.
  - The job uploads nothing, and says so, until `ULTRAOS_SFTP_HOST` is set.
    Once it is set, a missing setting fails the job instead of skipping it
    quietly. A step makes that decision, not the job's `if`: a job condition
    cannot read secrets, so a host kept in a secret would have skipped the
    job on every release. The job is separate from `publish-sdk`, so an
    unreachable server turns only this job red, and it can be re-run on its
    own.
  - `scripts/upload-sdk-website.sh --check` tests the settings without a
    release: it logs in, writes a probe file, renames it, lists the
    directory and removes the probe. It names the likely cause of a failure
    (key, host key or directory). The new *SDK website check* workflow runs
    it on a push that changes the script and from the Actions tab, since the
    upload itself runs only on a release build of `main`.
  - `publish-sdk` now has the version as a job output, so the two jobs read
    it from the same place and cannot disagree about which archives to send.
  - The comment above `publish-sdk` still said UltraCanvasStart downloads the
    SDK from the release by a URL it builds; UltraCanvasStart 0.2.0 stopped
    doing that. The comment now says what it does: it links the release page
    beside the download page.
  - Tested against a local OpenSSH server: a first upload (all six archives
    arrived byte-identical, and `SHA256SUMS` verifies them), a re-run that
    replaces a changed archive and leaves no `.part` files, five archives
    (uploaded, with a warning), a missing server directory, a wrong host key
    and an unset setting (each fails). `--check` was tested the same way: it
    passes and leaves no probe behind, and it fails, naming the cause, on a
    wrong key, a wrong host key, a missing directory and a read-only one.
