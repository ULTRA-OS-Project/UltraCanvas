- **A Telegram sign-in code can be asked for another way.**
  `UltraIMProviderInfo::authResendHint` names the other way the service
  offers ("Send it by SMS instead") and `UltraIM_ResendAuthCode` asks for it
  (TDLib's `resendAuthenticationCode`); the Telegram provider's hint now says
  where the code went - as a message to the Telegram apps the number is
  signed in on, by SMS, in a call - and how many digits it has. A step's
  `NeedsInput` answer now precedes its `AuthStepChange`, so a sign-in made
  in answer to the event is no longer resolved by the step that raised it.
