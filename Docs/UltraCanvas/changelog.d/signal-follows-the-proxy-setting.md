- **UltraIM Signal: the proxy setting reaches signal-cli.** The Signal
  provider started signal-cli with nothing said about a proxy, and
  signal-cli connects to Signal by itself, past UltraNet - so, as with
  Telegram, ChatCube's proxy setting did not apply to it. The provider's own
  daemon is now started for the setting (`UltraNet_ResolveProxy` for
  `https://chat.signal.org:443/`): an HTTP or a SOCKS 5 proxy as Java's
  proxy properties (`https.proxyHost` / `socksProxyHost`), which signal-cli
  0.13.13 and later also passes to its network library, "no proxy" as
  options that take out one the environment names, and nothing when the
  operating system's settings name none. They are appended to
  `JAVA_TOOL_OPTIONS` in the daemon's environment (`SignalDaemon::Start`
  takes variables for it, on POSIX and on Windows). When the setting changes
  while signed in the daemon is started again for it, on the link it kept.
  A proxy signal-cli has no means for (one that asks for a user name and
  password, SOCKS 4, one reached over TLS) and a signal-cli that no Java
  runtime starts (its native build: the runtime's acknowledgement of the
  options is missing from the daemon's log) fail the sign-in with
  `Unsupported`, or stop the daemon with the reason as the `Error` state,
  rather than connect directly. A daemon the user runs is not touched.
  Tested in `SignalProviderTests` with the stand-in daemon, and by a Java
  runtime started through the same path selecting the proxy; signal-cli
  itself against Signal's servers was not run. See
  `Docs/Modules/UltraIM/README.md`, *Provider: Signal*.
- **The ChatCube handoff no longer says every provider inherits the
  proxy.** `Docs/ChatCube/HANDOFF_UltraIM_ChatCube.md` (6.4) said "Proxy
  ... inherited from UltraNet - set nothing"; that holds only for what
  connects through UltraNet, and it now says so and names the two providers
  that hand the proxy over themselves.
