- **UltraCloud's account store can be encrypted at rest.**
  `AccountStore::Open` takes an optional key, handed to UltraDatabase as the
  connection's `encryptionKey`; UltraMail opens its `cloud.db` with one.
