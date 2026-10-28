- **Mail security per account.** *Account Settings > Security > Mail
  security…* - and, with *Skip*, right after an account is added - sets how
  the account signs and encrypts: its S/MIME certificate (Import / Remove,
  moved here from their own row), the OpenPGP key it signs with (the
  keyring's secret keys by name, address and fingerprint; by default the one
  whose user id is the account's address), and how its new messages, replies
  and forwards start - *Off*, *S/MIME* or *OpenPGP*, *Sign*, *Encrypt*. The
  compose window's Security row starts from that and stays changeable per
  message; a reply to encrypted mail still starts encrypted.
  - Stored with the account (`AccountSecurity`, mail.db schema 13,
    `LocalStore::SetAccountSecurity`); `OpenPgpSecretKeys`, and
    `SecureKeys::openPgpSigningKeyFor` signing with the chosen key.
  - `test_securemime`: the defaults stored and kept across a re-added
    account; the secret keys listed, a chosen key signing, an unknown
    fingerprint refused.
