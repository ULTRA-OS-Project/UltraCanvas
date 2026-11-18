- **UltraIM Telegram: the proxy setting reaches TDLib.** The Telegram
  provider connected directly whatever the proxy settings said: TDLib does
  its own networking, past UltraNet, and was never told a proxy - so in
  ChatCube everything went through the proxy but Telegram, whose messages
  then stayed undelivered where Telegram is only reachable through one. The
  provider now tells TDLib what the shared setting says for Telegram
  (`UltraNet_ResolveProxy` for `https://telegram.org:443/`: the proxy
  chosen, the operating system's, or none): a SOCKS 5 or an HTTP proxy as
  `addProxy` with its sign-in, none as `disableProxy`, both before
  `setTdlibParameters`, so the first connection is already the right one. It
  looks at the setting again every three seconds while a session is open and
  tells TDLib when it changed, and removes from TDLib's database every proxy
  no longer in use. A proxy TDLib has no means for (SOCKS 4, one reached
  over TLS) fails the sign-in with `Unsupported` and starts nothing; set
  while signed in, or refused by TDLib, it takes Telegram off the network
  with the reason as its `Error` state instead of connecting directly, until
  the setting names one it can use. While TDLib connects to the proxy the
  state says so and names it. `UltraIMTelegramCore` links UltraNet for the
  setting. Tested in `TelegramProviderTests` (FakeTdJson now keeps the proxy
  requests) and, against the real TDLib 1.8.69, in `TelegramTdlibTests`. See
  `Docs/Modules/UltraIM/README.md`, *Provider: Telegram*.
