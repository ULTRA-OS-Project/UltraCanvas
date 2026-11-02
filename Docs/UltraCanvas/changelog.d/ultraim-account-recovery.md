- **UltraIM: confirming the e-mail and phone, and account recovery** (module
  ABI 3). `UltraIMAccountProfile` says whether the e-mail and phone are
  confirmed and whether recovery is offered and on;
  `UltraIM_VerifyContact`, `UltraIM_ResendContactCode`,
  `UltraIM_SetRecoveryEnabled`, `UltraIM_StartRecovery` and
  `UltraIM_FinishRecovery` (the `AccountRecovery` capability) do the rest.
  ChatCube Direct implements them on its relay (1.2.0: codes over SMTP and
  Twilio or an SMS webhook, a copy of the key encrypted with the relay's
  recovery key, both codes to get it back, the owner told on both).
