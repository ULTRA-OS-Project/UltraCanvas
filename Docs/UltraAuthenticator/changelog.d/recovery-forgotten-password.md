- **Recovery from a forgotten master password.** *Recovery* in the top band
  turns it on: the vault gets a second way in, a recovery key in two halves -
  one kept in the vault file, the other left with your **UltraAccount**
  (`Apps/UltraAccount`) and shown once as a **recovery code** to write down.
  On the lock screen, **Forgot password** sends a code to the account's
  verified email - or, with UltraAccount Plus, by text message to its
  verified phone - and with it, or with the recovery code, opens the vault
  and sets a new master password. Neither half opens anything alone: the
  account service never sees the vault file. A recovery spends the key, and
  the account's copy is removed; turn recovery on again afterwards.
  - Turning recovery on or off asks for the master password, like *Show key*.
  - Without an UltraAccount, the recovery code alone works, offline.
- **The vault file is container version 2**: the accounts are sealed with a
  random data key, which the master password - and the recovery key, when
  on - each seal in a slot of the header. Changing the master password
  rewrites only its slot. A version 1 vault still opens, and becomes version
  2 the first time it is written; 0.2 cannot open a version 2 vault.
