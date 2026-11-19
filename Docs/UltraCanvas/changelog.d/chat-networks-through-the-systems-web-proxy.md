- **UltraNet: `UltraNet_ResolveTunnelProxy`; IRC, XMPP and Telegram take
  the system's web proxy where it names no other.** With "the system's
  proxy" chosen and nothing but a web proxy set in the system, ChatCube's
  networks did not agree: Telegram and Signal went through it, IRC and XMPP
  over TCP connected directly past it (a raw connection took only the
  system's SOCKS proxy). `UltraNet_ResolveTunnelProxy(host, port)` answers
  for a connection that is not the web's what a program following the
  setting in everything wants: the proxy `tcp://host:port` resolves to (the
  one chosen, the system's SOCKS proxy), else the one `https://host:port`
  resolves to - the web's proxy, as a CONNECT tunnel. IRC (`IrcConnection`)
  and XMPP over TCP (`XmppTcpConnection`) hand it to
  `UltraNetSocketOptions::proxy`, and Telegram takes its proxy by the same
  rule. A proxy that refuses the tunnel - one that lets only port 443
  through - fails the sign-in naming the proxy; an exception for the
  server connects directly. `useProxySetting` is unchanged (SOCKS alone
  under the system's settings), for mail and devices. Tested in
  `UltraNetTests` (the order, the exceptions, a connection opened through
  the system's web proxy) and, signing in through a web proxy that carries
  the connection to the test server, in `IrcProviderTests` and
  `XmppProviderTests` (`Tests/UltraIM/ConnectProxy.h`).
