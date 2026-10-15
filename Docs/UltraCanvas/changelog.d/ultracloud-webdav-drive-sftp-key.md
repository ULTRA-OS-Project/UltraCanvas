- **UltraNet FTP: SFTP with a private key.** `UltraNetFtpOptions` carries
  `sshPrivateKeyFile`, `sshPublicKeyFile` and `sshKeyPassphrase`; with a key
  set an `sftp://` call offers the key first and the password, when there
  is one, after it (libcurl's `CURLOPT_SSH_PRIVATE_KEYFILE` and
  `CURLOPT_SSH_AUTH_TYPES`); the failure diagnostics name the key. Without
  a key nothing changes (`UltraNet/UltraNetFtp.h`).
- **UltraCloud: a WebDAV server is a drive, and SFTP signs in with a key.**
  `WebDavProvider` - and Nextcloud with it - implements `Delete` (DELETE;
  a folder's URL ends with `/`, the root is refused) and `Rename` (MOVE to
  the same folder, `Overwrite: F`), so `Capabilities().modify` is set and
  UltraFiler can carry one as a drive, and a publisher can prune.
  `Credentials::privateKeyFile` is the SFTP key's path, kept in the vault
  as `cloud.<id>.keyfile`; the FTP provider hands it to UltraNet for
  `sftp://` accounts with the password as the key's passphrase. The shared
  add-account dialog has a *Private key* row for the FTP provider. Tests
  for the DAV verbs, the key in the options and the vault round trip
  (`Docs/Modules/UltraCloud/README.md`).
