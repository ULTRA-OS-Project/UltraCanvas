- **Signal goes through the proxy.** Signal, like Telegram, connected
  directly whatever Settings > Networks > Proxy said. ChatCube now starts
  signal-cli for the proxy chosen there (HTTP or SOCKS) and starts it again
  when you change the setting - you stay linked, no new QR code. signal-cli
  cannot use a proxy that asks for a user name and password, and ChatCube
  can hand a proxy only to signal-cli's Java version, not to its native
  build: in both cases it says so on the Signal status line instead of
  connecting directly. If you run the
  signal-cli daemon yourself, its proxy is yours to set. Framework
  changelog: UltraIM Signal, the proxy setting reaches signal-cli.
