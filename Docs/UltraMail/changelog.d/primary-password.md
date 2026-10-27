- **A Primary Password.** *Settings > Privacy > Primary Password* sets one,
  changes it and removes it (each asks for the current one first). Once set,
  UltraMail asks for it when it starts, before it reads anything stored: a
  wrong one is asked for again in the same dialog, Cancel ends the start. It
  protects the account passwords and sign-ins in the vault, whose device key
  is then gone, so nothing on the computer opens the vault without it.
  - The vault now opens at the start, before the mailbox (about 0.1 s, the
    Argon2id key derivation), instead of after the window is up.
  - A vault from an early version that was made with a master password and
    never got a device key is now treated as having a Primary Password: it
    is asked for at the start, and *Remove* turns it off. Before, typing it
    once turned it into a device key without a word.
  - The password dialog says *Primary Password* throughout; two passwords
    that do not match, or none typed, ask again in place instead of closing
    the dialog.
