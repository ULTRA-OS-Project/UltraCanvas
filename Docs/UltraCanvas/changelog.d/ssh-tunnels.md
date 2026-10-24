- **UltraNet: SSH tunnels.** `UltraNet_OpenSshTunnel` (`UltraNetSsh.h`)
  forwards a free port of 127.0.0.1, through an SSH server, to a host and
  port that server reaches - each connection on a channel of its own - and
  `UltraNet_CloseSshTunnel` ends it. The `ssh` plug-in (libssh) serves it
  and is loaded on first use.
- **UltraNet's SSH plug-in checks host keys.** The shell and the tunnel
  alike refuse a server whose key is unknown (`SshHostKeyUnknown`, with the
  key's fingerprint for the user to compare and trust) or differs from the
  one known (`SshHostKeyChanged`); `UltraNetRemoteOptions` takes the trusted
  fingerprint or a known_hosts file. Before, any key was accepted. A private
  key is also read as the text libssh expects (it was passed without its
  terminator).
- **UltraDatabase 0.6.0: connections through an SSH bastion**
  (`UltraDatabaseTunnel.h`). A connection whose options name an `ssh_host`
  opens its tunnel first, with the password or key and passphrase read from
  UltraVault, and closes it with the connection; every session of the
  connection goes through it. An unknown or changed bastion key fails with
  `HostKeyUnknown` / `HostKeyChanged` and `UltraDbResult::hostKeyFingerprint`.
  PostgreSQL still checks the database's certificate against its host name
  through the tunnel; MySQL, which can only check the address it connects to,
  refuses `VerifyFull` there with that reason. The module opens no socket
  itself: `UltraDb_UseUltraNetSshTunnels()` (`UltraDatabaseSshTunnel.h`,
  header-only) opens the tunnels through UltraNet.
