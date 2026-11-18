- **With "Use system proxy settings", every network takes the same way
  out.** When the system names a web proxy and no SOCKS proxy, Telegram and
  Signal went through it but IRC and XMPP connected directly past it. IRC
  and XMPP now go through it too. If that proxy does not let a chat server
  through (some allow only web addresses), signing in says so and names
  the proxy: add the server under *No proxy for* to reach it directly.
  Framework changelog: UltraNet_ResolveTunnelProxy.
- **Signal says why it stopped.** If signal-cli ended and could not be
  started again - Java removed, for instance - the Signal status stayed at
  "reconnecting" without a reason. It now shows what signal-cli said, and
  comes back by itself once it can be started. Framework changelog: UltraIM
  Signal, a daemon that cannot be started again says why.
