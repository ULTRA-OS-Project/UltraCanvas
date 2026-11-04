- **UltraIM: confirming the e-mail and phone, and account recovery** (module
  ABI 3). `UltraIMAccountProfile` says whether the e-mail and phone are
  confirmed and whether recovery is offered and on;
  `UltraIM_VerifyContact`, `UltraIM_ResendContactCode`,
  `UltraIM_SetRecoveryEnabled`, `UltraIM_StartRecovery` and
  `UltraIM_FinishRecovery` (the `AccountRecovery` capability) do the rest -
  finishing a recovery asks whether the owner approved the link e-mailed to
  them (`NeedsInput` until then). ChatCube Direct implements them on its
  relay (1.2.0: a confirmation link and a recovery link e-mailed over SMTP,
  each opening a page of its own; the phone's code by Twilio or an SMS
  webhook; a copy of the key encrypted with the relay's recovery key, handed
  back once only to the computer that asked; the owner e-mailed a notice).
- **ChatCube relay: a versioned database.** The relay's SQLite schema records
  its version (`PRAGMA user_version`) and every start brings it forward a
  step at a time, each step in a transaction of its own; databases from
  relays 1.0 and 1.1, which recorded none, are recognised by their tables
  and keep their data, and one from a newer relay is refused.
