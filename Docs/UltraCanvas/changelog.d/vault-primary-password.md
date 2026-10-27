- **A device-key vault can take a Primary Password.**
  `DeviceKeyVault::ChangePassphrase(current, new, keepAsDeviceKey)`
  re-encrypts the vault file under a new passphrase - every secret in it
  carried over, whatever module wrote it - and swaps the file in only once it
  is complete; without the device key (`keepAsDeviceKey` false) the vault
  then opens only with the typed passphrase (`NeedsPassphrase`), and with a
  fresh `NewDeviceKey()` it opens silently again. A kept device key is
  written as `device.key.next` before the swap, which `TryAutoUnlock` also
  tries, so an interruption never leaves the vault without a key that opens
  it; `ForgetDeviceKey` drops one left stale. `UltraVaultTests` checks
  setting, changing and removing one, another prefix's secret carried over,
  and an interrupted swap.
