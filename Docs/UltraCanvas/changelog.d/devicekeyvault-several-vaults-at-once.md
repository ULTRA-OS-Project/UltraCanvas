- **Several credential vaults in one program no longer write into each
  other.** UltraVault holds one open store per process, and a
  `DeviceKeyVault` unlocked once and used directly (UltraMail's way) had
  its store silently replaced whenever another vault took a `Use()` turn -
  ChatCube's three messenger providers do, from their own threads. Its next
  `Store()` then went into the other vault's file, its own file lost the
  secret, and its `Lock()` closed the other vault under its user. Every
  call now takes one process-wide lock, the vault that holds the store is
  remembered, and a vault re-opens its own file before every read or write;
  `UltraVaultTests` drives four vaults from four threads to hold this.
