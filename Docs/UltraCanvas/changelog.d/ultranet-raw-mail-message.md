- **UltraNet: a mail message can be sent as it stands.**
  `UltraNetMailMessage::rawMessage` (UltraNetPlugins.h 0.5.3): when set,
  `UltraNet_MimeBuildMailMessage` returns it unchanged instead of building the
  message from its fields, so the SMTP and JMAP plug-ins send exactly those
  bytes - a message signed or encrypted (S/MIME, OpenPGP) after it was built.
  The other fields still give the envelope (sender, recipients).
