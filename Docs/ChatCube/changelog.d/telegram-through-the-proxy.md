- **Telegram goes through the proxy.** With a proxy chosen under Settings >
  Networks > Proxy - or set in the system - ChatCube reached its relay and
  Matrix through it, but Telegram still connected directly, so where
  Telegram needs a proxy its messages were not delivered. Telegram now
  uses the same proxy (SOCKS or HTTP, with its user name and password), from
  its first connection on, and follows a change of the setting within a few
  seconds without signing in again. Framework changelog: UltraIM Telegram,
  the proxy setting reaches TDLib.
