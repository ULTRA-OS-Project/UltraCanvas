- **`UltraVault::DeviceKeyVault` opens in a profile folder of any name, and
  says why when it does not open.**
  - The device key's path was handed to `fs::exists`, `std::ifstream`,
    `std::ofstream`, `fs::remove` and `fs::permissions` as a `std::string`,
    and the directory to `fs::create_directories` the same way. On Windows
    that is read in the ANSI code page, so a profile folder the code page
    cannot spell - a Thai or Cyrillic user name under code page 1252 - could
    not find, write or protect its `device.key`, and the vault of UltraMail,
    UltraFiler, UltraSocial or EmailCleaner did not open. Every path now goes
    through `PathFromUtf8`. `scripts/check_path_string.py` could not see
    these: the path came from a function's result and a member declared in
    the header. The vault core's clean-up of a failed write
    (`std::remove(tmpPath.c_str())`) goes through `std::filesystem` too.
  - `TryAutoUnlock()` returned a bare `false` whatever the reason. The new
    `GetLastUnlockStatus()` says why the last `Unlock()` / `TryAutoUnlock()`
    left the vault closed - `Unavailable` (no crypto backend: UltraCrypt
    built without libsodium), `IoError` (the folder or the key cannot be
    written or read), `Locked` (a vault made with a master password, no
    device key), or what `Unlock()` reported - and
    `DescribeUnlockStatus(status)` puts it in words for an error message.
    `DeviceKeyVault` 0.2.0, `UltraVaultCore` 0.1.2.
  - `Tests/UltraVaultTests.cpp` checks the status on each path, including a
    build without libsodium, and a vault in a Thai-and-emoji folder.
