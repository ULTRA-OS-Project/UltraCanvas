- **The `claude` CLI connects through the proxy every ULTRA OS application
  shares.** It makes its own connections, so the proxy settings reached it
  only if its environment happened to name the same proxy. Now each run is
  started with them written in: an HTTP proxy chosen in the Proxy settings
  dialog as `HTTPS_PROXY` / `HTTP_PROXY` with its sign-in and `NO_PROXY` with
  the exceptions; *No proxy* removes the proxy variables; the system's
  setting leaves an environment that names a proxy alone and otherwise
  writes in what Windows' or macOS' settings (or a PAC script) give for the
  API's address. The programs Claude's tools run (git, curl) follow the same
  variables. A SOCKS proxy is given to those as `ALL_PROXY`; the CLI itself
  takes only an HTTP proxy, so its own connections then go directly.
  Test: `UltraClaudeProxyEnvironmentTest`.
