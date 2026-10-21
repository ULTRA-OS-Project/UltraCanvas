- **UltraVault: `DeviceKeyVault::Use` lets several vaults share the process.**
  The process holds one open UltraVault store, so a vault opened directly
  stayed open until another one opened - after which the first read the
  second's secrets. `Use(function)` opens the vault's own file, runs the
  function and closes it again, under a lock every `DeviceKeyVault` in the
  process shares. ChatCube Direct and the Matrix provider, which sit in one
  program, now reach their vaults only this way.
