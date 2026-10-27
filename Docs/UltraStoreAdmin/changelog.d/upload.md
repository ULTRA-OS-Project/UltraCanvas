- **Upload to the host.** `ultrastoreadmin upload <outdir>` and the
  Publish page's *Upload* send the publish folder to the server over what
  UltraNet speaks - SFTP with a password or a key file, FTPS, FTP, or
  HTTPS with one PUT per file - what changed since the last upload to that
  server only (`upload-state.json` keeps each file's hash per target), the
  packages and the media first, the signature, and `catalog.json` last, so
  a store fetching in between sees the old catalog whole or the new one
  whole; an upload that broke off goes on from where it stopped. Settings
  `upload.url`, `upload.user`, `upload.key_file`; the password is never
  kept.
