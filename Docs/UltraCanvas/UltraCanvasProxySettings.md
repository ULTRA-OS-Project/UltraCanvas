# UltraCanvas Proxy Settings Documentation

## Overview

**UltraCanvasProxySettings** is the proxy every UltraCanvas application
connects through, chosen once for all of them: the operating system's proxy
(the default), no proxy, or a proxy the person names - HTTP or SOCKS, a
host and a port, the hosts reached directly, and a sign-in.
**ShowProxySettingsDialog** is the one dialog that edits it, and
**MakeProxySettingsPage** the page an application's settings window shows
for it.

UltraNet follows the setting on every connection it makes through libcurl -
HTTP and HTTPS (and so every UltraAI provider), WebSocket, FTP / SFTP, and
the IMAP, SMTP, POP3, WebDAV, LDAP, Telnet and gRPC plug-ins. How UltraNet
decides, and what "the operating system's proxy" is on each platform, is
the UltraNet module's *Proxies* section (`Docs/Modules/UltraNet/README.md`).

**File Location**: `include/UltraCanvasProxySettings.h`,
`include/UltraCanvasProxySettingsDialog.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

## Features

- ✅ **The system's proxy by default** - nothing to set up where the desktop
  already names one (Windows WPAD / PAC included)
- ✅ **One setting for every application**: `Proxy.conf` in the shared
  settings folder; a change saved in one application is used by the others
  from their next connection
- ✅ **HTTP or SOCKS** (SOCKS 5, names looked up by the proxy), an exception
  list (`*.example.com, 192.168.*`), and a sign-in
- ✅ **The password never in the file**: remembered, it is in an encrypted
  device-key vault; otherwise only the application it was typed into uses it
- ✅ **A PAC address** instead of the system's settings, where the platform
  can run one (Windows, macOS)
- ✅ **Check connection**: opens an address with the settings as they stand
  in the dialog and says whether it went through the proxy or directly
- ✅ The machine itself is always reached directly - a local model server
  never goes through the proxy

## The file

`Proxy.conf` in the UltraCanvas settings folder (`UltraCanvasSettingsFolder.h`:
`%APPDATA%\UltraCanvas`, `~/Library/Application Support/UltraCanvas`,
`~/.config/UltraCanvas`), one `key=value` per line:

```
mode=manual
pac-enabled=0
pac=
type=socks
host=localhost
port=10808
no-proxy=*.example.com, 192.168.*
auth=0
user=
remember=0
saved=1762780000000
```

| Key | Values |
|---|---|
| `mode` | `system` (the default), `none`, `manual` |
| `pac-enabled`, `pac` | `system`: run the PAC script at that address instead of the system's settings |
| `type` | `manual`: `http` or `socks` |
| `host`, `port` | `manual`: the proxy; port 0 or missing is the type's own (80, 1080) |
| `no-proxy` | hosts reached directly - commas, semicolons or spaces between them |
| `auth`, `user`, `remember` | sign in to the proxy as `user`; keep the password for the next start |
| `saved` | when it was written (ms since 1970): every save changes the file |

A missing file is the defaults. The remembered password is in
`<settings folder>/Proxy/` (an UltraVault device-key vault).

## API

```cpp
enum class ProxyMode { System, NoProxy, Manual };
enum class ProxyKind { Http, Socks };

struct ProxySettings {
    ProxyMode mode = ProxyMode::System;
    bool useAutoConfigUrl = false;
    std::string autoConfigUrl;
    ProxyKind kind = ProxyKind::Http;
    std::string host;
    int port = 0;
    std::string noProxyFor;
    bool authenticate = false;
    std::string user;
    std::string password;            // never written to the file
    bool rememberPassword = false;
    std::int64_t savedAt = 0;

    int EffectivePort() const;
    static ProxySettings Parse(const std::string& text);
    std::string Serialize() const;
    static std::filesystem::path FilePath();
    static ProxySettings Load(bool withPassword = true);   // the file, and the password
    bool Save();                     // the file, the vault, and every application follows
    static bool ForgetPasswords();   // "Clear passwords"
    static bool CanUseAutoConfigUrl();
    UltraNetProxyConfig ToUltraNet() const;   // with UltraNet in the build
};

void UseSharedProxySettings();       // UltraNet follows Proxy.conf (called by Initialize)
ProxyCheckResult CheckProxyConnection(const ProxySettings& settings, const std::string& url);

void ShowProxySettingsDialog(UltraCanvasWindowBase* parent,
                             std::function<void(const ProxySettings&)> onSaved = {});
std::string DescribeProxySettings(const ProxySettings& settings);
std::shared_ptr<UltraCanvasContainer> MakeProxySettingsPage(const std::string& id,
                                                            UltraCanvasWindowBase* parent);
```

## Usage

An application does nothing to follow the setting:
`UltraCanvasApplicationBase::Initialize` calls `UseSharedProxySettings()`,
and from then on UltraNet reads `Proxy.conf` again whenever two seconds have
passed since it last looked. A command-line tool without an application
object calls it itself.

The dialog, from a menu or a button:

<!-- doc-check: UltraCanvasWindowBase* window; -->
```cpp
ShowProxySettingsDialog(window, [](const ProxySettings& saved) {
    // already saved and in force; saved.mode, saved.host ...
});
```

A settings window built on `UltraCanvasSettingsPageKit` (or one with the
same look) gives it a page: what the setting does now, and a *Change*
button opening the dialog. UltraFiler, UltraMail, UltraAI, ChatCube and
UOS-Settings show it that way:

<!-- doc-check: SettingsPageKit::UltraCanvasSettingsWindow d; -->
```cpp
d.AddTreeNode("extras", "extras/proxy", "Proxy");
d.AddPage("extras/proxy", MakeProxySettingsPage("app-set-proxy", d.window.get()));
```

Set from code - the same file the dialog writes, so every application
follows:

```cpp
ProxySettings proxy = ProxySettings::Load();
proxy.mode = ProxyMode::Manual;
proxy.kind = ProxyKind::Socks;
proxy.host = "localhost";
proxy.port = 10808;
proxy.noProxyFor = "*.example.com, 192.168.*";
proxy.Save();
```

Checking a connection with settings that are not saved (blocking - the
dialog runs it on a worker thread):

```cpp
ProxySettings trial;
trial.mode = ProxyMode::Manual;
trial.host = "proxy.example.com";
trial.port = 3128;
ProxyCheckResult r = CheckProxyConnection(trial, "https://www.example.com/");
// r.success, r.route ("http://proxy.example.com:3128"), r.httpStatus, r.message
```

## The dialog

```
( ) No proxy
(o) Use system proxy settings
      The system's proxy now: http://proxy.corp.example:3128
      [ ] Automatic proxy configuration URL  [                    ]
          Example: https://example.com/wpad.pac
( ) Manual proxy configuration
      (o) HTTP  ( ) SOCKS
      Host           [ localhost              ]
      Port           [ 10808 ^v ]
      No proxy for   [                        ]
                     Example: *.example.com, 192.168.*
      [ ] Proxy authentication
          User       [                        ]
          Password   [                        ]
                     [ ] Remember   [ Clear passwords ]
[ Check connection ]                         [ OK ] [ Cancel ]
```

One form grid holds every row, so the captions share one column and the
fields start at one x in any language. What the chosen mode does not use is
greyed out, not hidden, so the dialog does not jump while choosing. Choosing
SOCKS changes the port from 8080 to 1080 unless one was typed. OK refuses a
manual proxy without a host and a PAC choice without an address, and says
why in the dialog. *Check connection* asks for an address (the last one
offered again), opens it with a HEAD request through the settings as they
stand, and reports the route and the server's HTTP status - any answer
counts, since it proves the way there works - or the error; a proxy asking
for a sign-in (HTTP 407) is named as such.

## Notes

- The PAC row is disabled where the platform cannot run a script
  (`CanUseAutoConfigUrl`, Linux and the BSDs); the system's own automatic
  setting still applies there through GIO.
- A password that is not remembered is used only by the application it was
  typed into, until it ends: another application connects without it.
- `ProxySettingsTest` (`Tests/ProxySettingsTest.cpp`) covers the file, the
  vault, UltraNet following a change another application saved, and the
  check through a proxy on 127.0.0.1; `UltraNetTests` (`test_proxy.cpp`)
  covers the resolver.

## Related

- `Docs/Modules/UltraNet/README.md` - *Proxies*: the resolver, the
  exceptions, the operating system's proxy per platform
- `UltraCanvasSettingsPageKit.h` - the settings window the page is made for
- `UltraCanvasSettingsFolder.h` - the folder `Proxy.conf` is in
- `UltraVault/UltraVaultDeviceKeyVault.h` - where a remembered password is kept
