// include/UltraCanvasProxySettings.h
// The proxy every UltraCanvas application connects through, set once for all
// of them: the operating system's proxy (the default), no proxy, or a proxy
// the person names - HTTP or SOCKS, a host and a port, the hosts reached
// directly, and a sign-in. ShowProxySettingsDialog
// (UltraCanvasProxySettingsDialog.h) edits it; UltraNet follows it.
//
// The file is Proxy.conf in the UltraCanvas settings folder (beside
// Emoji.conf and Language.conf):
//   Windows  %APPDATA%\UltraCanvas
//   macOS    ~/Library/Application Support/UltraCanvas
//   others   $XDG_CONFIG_HOME/UltraCanvas, else ~/.config/UltraCanvas
// one `key=value` per line:
//   mode=system          system | none | manual
//   pac-enabled=0        system: run the PAC script at `pac` instead of the
//   pac=                 system's own settings
//   type=http            manual: http | socks (SOCKS 5, names looked up by the proxy)
//   host=localhost
//   port=10808
//   no-proxy=*.example.com, 192.168.*
//   auth=0               manual: sign in to the proxy as `user`
//   user=
//   remember=0           the password is kept (encrypted) for the next start
//   saved=1762780000000  when it was written - any save changes the file
// The password is never in the file: remembered, it is in an encrypted
// device-key vault in <settings folder>/Proxy/ (UltraVault); otherwise it is
// kept in memory for the program that was told it, until it ends.
//
// UseSharedProxySettings() makes UltraNet follow the file: before a
// connection it reads Proxy.conf again when two seconds have passed, and a
// change another application saved applies from then on.
// UltraCanvasApplicationBase::Initialize calls it, so every UltraCanvas
// application follows the setting; a program without an application object
// (a command-line tool) calls it itself.
//
// doc-check: example
//   UltraCanvas::ProxySettings proxy = UltraCanvas::ProxySettings::Load();
//   proxy.mode = UltraCanvas::ProxyMode::Manual;
//   proxy.kind = UltraCanvas::ProxyKind::Socks;
//   proxy.host = "localhost";
//   proxy.port = 10808;
//   proxy.Save();   // every UltraCanvas application connects through it from now on
//
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#ifdef ULTRACANVAS_HAS_NET
#include "UltraNet/UltraNetCore.h"
#endif

namespace UltraCanvas {

    enum class ProxyMode {
        System,     // the operating system's proxy (the default)
        NoProxy,    // every connection direct
        Manual      // the proxy the settings name
    };

    enum class ProxyKind {
        Http,       // an HTTP proxy; anything that is not HTTP goes in a CONNECT tunnel
        Socks       // SOCKS 5; the proxy looks the names up
    };

    struct ProxySettings {
        ProxyMode mode = ProxyMode::System;

        // System: run the proxy auto-configuration (PAC) script at this
        // address instead of the system's own settings. Only where this
        // build can run one (CanUseAutoConfigUrl).
        bool useAutoConfigUrl = false;
        std::string autoConfigUrl;

        // Manual
        ProxyKind kind = ProxyKind::Http;
        std::string host;
        int port = 0;                    // 0: the kind's own (HTTP 80, SOCKS 1080)
        std::string noProxyFor;          // "*.example.com, 192.168.*" - commas, semicolons or spaces
        bool authenticate = false;
        std::string user;
        std::string password;            // never written to the file
        bool rememberPassword = false;   // kept in the vault for the next start

        // When the file was saved (milliseconds since 1970), 0 when never.
        std::int64_t savedAt = 0;

        bool operator==(const ProxySettings&) const = default;

        // The port the connection uses: `port`, else the kind's own.
        int EffectivePort() const;

        // The file's text and back. Parse ignores lines it does not know and
        // keeps the default for a value it does not know. Serialize writes no
        // password.
        static ProxySettings Parse(const std::string& text);
        std::string Serialize() const;

        // <settings folder>/Proxy.conf; empty when the environment names no
        // settings folder.
        static std::filesystem::path FilePath();
        // The file, and the remembered password (or the one this program was
        // told) - unless `withPassword` is false, which leaves the vault
        // closed (a page that only says which proxy is in use). A missing or
        // unreadable file is the defaults: the system's proxy.
        static ProxySettings Load(bool withPassword = true);
        // Writes the file (beside it, then renamed over it) with savedAt set
        // to now; keeps the password in the vault when it is to be
        // remembered, removes it from there when not, and keeps it in memory
        // for this program either way. Every application following the shared
        // settings uses the new ones from its next connection on.
        bool Save();
        // Removes every remembered proxy password, and the one in memory
        // ("Clear passwords"). True when the vault could be opened.
        static bool ForgetPasswords();

        // Whether this build can run a PAC script (Windows, macOS).
        static bool CanUseAutoConfigUrl();

#ifdef ULTRACANVAS_HAS_NET
        // As UltraNet's module-wide setting.
        UltraNetProxyConfig ToUltraNet() const;
#endif
    };

    // "mode" values as the file writes them, and back.
    const char* ProxyModeKey(ProxyMode mode);
    bool ParseProxyMode(const std::string& key, ProxyMode& out);

    // UltraNet follows Proxy.conf from now on (see the top of this file).
    // Safe to call more than once; a no-op in a build without UltraNet.
    void UseSharedProxySettings();

    // The outcome of CheckProxyConnection.
    struct ProxyCheckResult {
        bool success = false;
        int httpStatus = 0;
        std::string route;      // "socks5h://localhost:10808", or empty when it went directly
        std::string message;    // in the person's language: what happened
        double seconds = 0;
    };

    // Opens `url` with `settings` (not the saved ones) and says how it went:
    // through which proxy, or directly, and the error when it failed.
    // Blocking: run it off the UI thread.
    ProxyCheckResult CheckProxyConnection(const ProxySettings& settings, const std::string& url);

} // namespace UltraCanvas
