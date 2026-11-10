- **Every connection goes through the system's proxy by default, and one
  dialog sets the proxy for every application.** UltraNet had a proxy
  setting that nothing used: only its HTTP client read it, nothing set it,
  and an FTP, WebSocket or mail connection never went through a proxy at all.
  - `UltraNet_ResolveProxy(url)` decides per address and every libcurl
    connection asks it - the HTTP verbs (sync, async, SSE), WebSocket, FTP /
    FTPS / SFTP, and the IMAP, SMTP, POP3, WebDAV, LDAP, Telnet and gRPC
    plug-ins, which reach it through the plug-in host table (ABI 4, so a
    plug-in built from this tree needs a host from it) and put the answer on
    the handle with `UltraNet/UltraNetCurlProxy.h`. An HTTP proxy carries
    anything that is not HTTP as a CONNECT tunnel. A direct connection now
    really is direct: libcurl no longer reads `*_proxy` from the environment
    behind the setting's back.
  - The default (`UltraNetProxyType::System`) is the operating system's
    proxy: on Linux the `*_proxy` variables, else GNOME's settings (through
    GIO, opened at run time - "automatic" runs the PAC script where
    glib-networking can) or KDE's `kioslaverc`; on Windows the Internet
    settings through WinHTTP, with automatic detection (WPAD) and a setup
    script (PAC) evaluated per address, and a company proxy answered as the
    signed-in user; on macOS System Settings through CFNetwork, PAC
    included. The web's proxy is used for the web; mail uses the system's
    SOCKS proxy or `all_proxy`, so a proxy that only lets port 443 through
    no longer cuts off a mail server reached directly before.
  - Windows' `https=` entry and macOS' "Secure web proxy" were taken for a
    proxy reached over TLS; they are a plain HTTP proxy that carries https://
    traffic.
  - The machine itself (`localhost`, `127.0.0.0/8`, `::1`) is never reached
    through a proxy - a local model server stays local - and exceptions take
    patterns (`*.example.com`, `192.168.*`), IPv4 ranges and `<local>`
    (`UltraNet_HostMatchesNoProxy`, `UltraNet_ParseNoProxyList`).
    `UltraNetProxyType::Direct` connects one request directly whatever the
    setting, `remoteDns` lets a SOCKS proxy look the name up (socks5h, the
    default), `autoConfigUrl` names a PAC script, and
    `UltraNet_SetGlobalProxySource` supplies the setting from outside.
- **`UltraCanvasProxySettings` and the Proxy settings dialog.** The choice -
  the system's proxy, no proxy, or HTTP / SOCKS with a host, a port, the
  hosts reached directly and a sign-in - is `Proxy.conf` in the shared
  settings folder, with a remembered password in an encrypted device-key
  vault and never in the file. `UltraCanvasApplicationBase::Initialize`
  makes UltraNet follow the file (`UseSharedProxySettings`), so every
  application uses one setting and a change saved in one is used by the
  others from their next connection. `ShowProxySettingsDialog` edits it -
  with *Check connection*, which opens an address with the settings as they
  stand and says whether it went through the proxy - and
  `MakeProxySettingsPage` is its page for a settings window. Docs:
  `Docs/UltraCanvas/UltraCanvasProxySettings.md`, the UltraNet module's
  *Proxies*. Tests: `ProxySettingsTest`, `UltraNetTests` (`test_proxy.cpp`).
