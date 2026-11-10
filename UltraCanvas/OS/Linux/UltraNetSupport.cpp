// OS/Linux/UltraNetSupport.cpp
// Linux (and Android, which builds these sources) glue for UltraNet: the
// operating system's proxy.
//
//   1. The environment, when any *_proxy variable is set: <scheme>_proxy
//      (http_proxy, https_proxy, ftp_proxy; ws and wss read http's and
//      https'), then all_proxy, and no_proxy - what libcurl, wget and every
//      command-line program read, so a terminal-started program behaves as
//      its shell does. Lower case first, then upper case.
//   2. Otherwise the desktop's proxy settings: GNOME's (GSettings
//      org.gnome.system.proxy - manual hosts per protocol, the exceptions,
//      the HTTP proxy's sign-in; "automatic" asks GIO's proxy resolver, which
//      runs the PAC script when glib-networking can) and KDE's (kioslaverc,
//      manual). KDE first on a KDE desktop. GIO is opened at run time
//      (dlopen), so neither the build nor a machine without it needs it.
//
// HTTP and HTTPS (and WebSocket) use their own entries, FTP its own, and
// every other protocol only a SOCKS proxy or all_proxy - an HTTP proxy set
// for the web is not taken to carry mail (a proxy that allows CONNECT only to
// port 443 would then cut off a mail server reached directly before).
//
// Version: 0.2.0 - per scheme, all_proxy and SOCKS, percent-decoded
//                  credentials; GNOME and KDE settings; a PAC script through
//                  GIO's resolver
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetCore.h"

#ifdef __linux__

#include "UltraCanvasPathUtf8.h"   // GetEnvUtf8, OpenFileUtf8

#include <dlfcn.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

    std::string Lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    std::string Trim(const std::string& s) {
        std::size_t b = 0, e = s.size();
        while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
        return s.substr(b, e - b);
    }

    // The first of the two that is set and not empty. Environment values here
    // are proxy addresses, not paths: ASCII in practice.
    std::string GetEnvEither(const char* lower, const char* upper) {
        std::string v = UltraCanvas::GetEnvUtf8(lower);
        if (!v.empty()) return v;
        return UltraCanvas::GetEnvUtf8(upper);
    }

    std::string PercentDecode(const std::string& s) {
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size() &&
                std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
                out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
                i += 2;
            } else {
                out += s[i];
            }
        }
        return out;
    }

    int ParsePort(const std::string& text) {
        if (text.empty() || text.size() > 5) return 0;
        int v = 0;
        for (const char c : text) {
            if (c < '0' || c > '9') return 0;
            v = v * 10 + (c - '0');
        }
        return v <= 65535 ? v : 0;
    }

    // scheme://[user:pass@]host[:port][/] - or a bare host[:port], which is an
    // HTTP proxy - into `out`. False for an empty or unusable value.
    bool ParseProxyUrl(const std::string& value, UltraNetProxyConfig& out) {
        std::string rest = Trim(value);
        if (rest.empty()) return false;
        std::string scheme = "http";
        const std::size_t schemeEnd = rest.find("://");
        if (schemeEnd != std::string::npos) {
            scheme = Lower(rest.substr(0, schemeEnd));
            rest = rest.substr(schemeEnd + 3);
        }
        out = UltraNetProxyConfig{};
        if (scheme == "socks4" || scheme == "socks4a") {
            out.type = UltraNetProxyType::Socks4;
            out.remoteDns = scheme == "socks4a";
        } else if (scheme == "socks5" || scheme == "socks5h" || scheme == "socks") {
            out.type = UltraNetProxyType::Socks5;
            out.remoteDns = scheme != "socks5";
        } else if (scheme == "https") {
            out.type = UltraNetProxyType::Https;
        } else if (scheme == "http") {
            out.type = UltraNetProxyType::Http;
        } else {
            return false;   // "direct://", a PAC "pac+http://" and the like
        }
        const std::size_t pathStart = rest.find('/');
        if (pathStart != std::string::npos) rest = rest.substr(0, pathStart);
        const std::size_t at = rest.rfind('@');
        if (at != std::string::npos) {
            const std::string userinfo = rest.substr(0, at);
            rest = rest.substr(at + 1);
            const std::size_t colon = userinfo.find(':');
            out.credentials.username = PercentDecode(userinfo.substr(0, colon));
            if (colon != std::string::npos) out.credentials.password = PercentDecode(userinfo.substr(colon + 1));
            out.credentials.type = UltraNetAuthType::Basic;
        }
        if (!rest.empty() && rest[0] == '[') {
            const std::size_t close = rest.find(']');
            if (close == std::string::npos) return false;
            out.host = rest.substr(1, close - 1);
            if (close + 1 < rest.size() && rest[close + 1] == ':') out.port = ParsePort(rest.substr(close + 2));
        } else {
            const std::size_t portColon = rest.find(':');
            out.host = rest.substr(0, portColon);
            if (portColon != std::string::npos) out.port = ParsePort(rest.substr(portColon + 1));
        }
        if (out.host.empty()) return false;
        if (out.port <= 0) {
            out.port = out.type == UltraNetProxyType::Https ? 443
                     : (out.type == UltraNetProxyType::Http ? 80 : 1080);
        }
        return true;
    }

    std::string SchemeOf(const std::string& url) {
        const std::size_t sep = url.find("://");
        return sep == std::string::npos ? std::string() : Lower(url.substr(0, sep));
    }

    // Which entry of a per-protocol setting a scheme uses.
    enum class Kind { Http, Https, Ftp, Other };
    Kind KindOf(const std::string& scheme) {
        if (scheme == "http" || scheme == "ws") return Kind::Http;
        if (scheme == "https" || scheme == "wss") return Kind::Https;
        if (scheme == "ftp" || scheme == "ftps" || scheme == "sftp") return Kind::Ftp;
        return Kind::Other;
    }

    // ---- 1. the environment -------------------------------------------------

    bool AnyProxyVariable() {
        for (const char* name : {"http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY", "ftp_proxy",
                                 "FTP_PROXY", "all_proxy", "ALL_PROXY"})
            if (!UltraCanvas::GetEnvUtf8(name).empty()) return true;
        return false;
    }

    void FromEnvironment(Kind kind, UltraNetProxyConfig& out) {
        std::string value;
        switch (kind) {
            case Kind::Http:  value = GetEnvEither("http_proxy", "HTTP_PROXY"); break;
            case Kind::Https: value = GetEnvEither("https_proxy", "HTTPS_PROXY"); break;
            case Kind::Ftp:   value = GetEnvEither("ftp_proxy", "FTP_PROXY"); break;
            case Kind::Other: break;
        }
        if (value.empty()) value = GetEnvEither("all_proxy", "ALL_PROXY");
        if (value.empty() || !ParseProxyUrl(value, out)) {
            out = UltraNetProxyConfig{};
            return;
        }
        out.noProxyHosts = UltraNet_ParseNoProxyList(GetEnvEither("no_proxy", "NO_PROXY"));
    }

    // ---- 2a. GNOME, through GIO opened at run time ----------------------------

    struct Gio {
        using SchemaSourceGetDefault = void* (*)();
        using SchemaSourceLookup = void* (*)(void*, const char*, int);
        using SchemaUnref = void (*)(void*);
        using SettingsNew = void* (*)(const char*);
        using GetString = char* (*)(void*, const char*);
        using GetInt = int (*)(void*, const char*);
        using GetBoolean = int (*)(void*, const char*);
        using GetStrv = char** (*)(void*, const char*);
        using ObjectUnref = void (*)(void*);
        using Free = void (*)(void*);
        using Strfreev = void (*)(char**);
        using ResolverGetDefault = void* (*)();
        using ResolverLookup = char** (*)(void*, const char*, void*, void**);
        using ErrorFree = void (*)(void*);

        SchemaSourceGetDefault schemaSourceGetDefault = nullptr;
        SchemaSourceLookup schemaSourceLookup = nullptr;
        SchemaUnref schemaUnref = nullptr;
        SettingsNew settingsNew = nullptr;
        GetString getString = nullptr;
        GetInt getInt = nullptr;
        GetBoolean getBoolean = nullptr;
        GetStrv getStrv = nullptr;
        ObjectUnref objectUnref = nullptr;
        Free free = nullptr;
        Strfreev strfreev = nullptr;
        ResolverGetDefault resolverGetDefault = nullptr;
        ResolverLookup resolverLookup = nullptr;
        ErrorFree errorFree = nullptr;
        bool ok = false;
    };

    const Gio& LoadGio() {
        static Gio gio;
        static std::once_flag once;
        std::call_once(once, []() {
            void* lib = dlopen("libgio-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
            if (!lib) lib = dlopen("libgio-2.0.so", RTLD_NOW | RTLD_LOCAL);
            if (!lib) return;   // kept open for good once found: GIO is not unloadable
            auto sym = [lib](const char* name) { return dlsym(lib, name); };
            gio.schemaSourceGetDefault = reinterpret_cast<Gio::SchemaSourceGetDefault>(sym("g_settings_schema_source_get_default"));
            gio.schemaSourceLookup = reinterpret_cast<Gio::SchemaSourceLookup>(sym("g_settings_schema_source_lookup"));
            gio.schemaUnref = reinterpret_cast<Gio::SchemaUnref>(sym("g_settings_schema_unref"));
            gio.settingsNew = reinterpret_cast<Gio::SettingsNew>(sym("g_settings_new"));
            gio.getString = reinterpret_cast<Gio::GetString>(sym("g_settings_get_string"));
            gio.getInt = reinterpret_cast<Gio::GetInt>(sym("g_settings_get_int"));
            gio.getBoolean = reinterpret_cast<Gio::GetBoolean>(sym("g_settings_get_boolean"));
            gio.getStrv = reinterpret_cast<Gio::GetStrv>(sym("g_settings_get_strv"));
            gio.objectUnref = reinterpret_cast<Gio::ObjectUnref>(sym("g_object_unref"));
            gio.free = reinterpret_cast<Gio::Free>(sym("g_free"));
            gio.strfreev = reinterpret_cast<Gio::Strfreev>(sym("g_strfreev"));
            gio.resolverGetDefault = reinterpret_cast<Gio::ResolverGetDefault>(sym("g_proxy_resolver_get_default"));
            gio.resolverLookup = reinterpret_cast<Gio::ResolverLookup>(sym("g_proxy_resolver_lookup"));
            gio.errorFree = reinterpret_cast<Gio::ErrorFree>(sym("g_error_free"));
            gio.ok = gio.schemaSourceGetDefault && gio.schemaSourceLookup && gio.schemaUnref &&
                     gio.settingsNew && gio.getString && gio.getInt && gio.getBoolean && gio.getStrv &&
                     gio.objectUnref && gio.free && gio.strfreev;
        });
        return gio;
    }

    // One GSettings object, for a schema that is installed (g_settings_new
    // aborts the process on one that is not).
    class GSettingsReader {
    public:
        GSettingsReader(const Gio& gio, const char* schema) : gio_(gio) {
            void* source = gio_.schemaSourceGetDefault();
            if (!source) return;
            void* found = gio_.schemaSourceLookup(source, schema, 1);
            if (!found) return;
            gio_.schemaUnref(found);
            settings_ = gio_.settingsNew(schema);
        }
        ~GSettingsReader() { if (settings_) gio_.objectUnref(settings_); }
        GSettingsReader(const GSettingsReader&) = delete;
        GSettingsReader& operator=(const GSettingsReader&) = delete;

        bool Valid() const { return settings_ != nullptr; }
        std::string String(const char* key) const {
            if (!settings_) return {};
            char* v = gio_.getString(settings_, key);
            std::string out = v ? v : "";
            if (v) gio_.free(v);
            return out;
        }
        int Int(const char* key) const { return settings_ ? gio_.getInt(settings_, key) : 0; }
        bool Bool(const char* key) const { return settings_ && gio_.getBoolean(settings_, key) != 0; }
        std::vector<std::string> List(const char* key) const {
            std::vector<std::string> out;
            if (!settings_) return out;
            char** v = gio_.getStrv(settings_, key);
            for (char** p = v; p && *p; ++p) out.emplace_back(*p);
            if (v) gio_.strfreev(v);
            return out;
        }

    private:
        const Gio& gio_;
        void* settings_ = nullptr;
    };

    // GIO's own answer for `url` (glib-networking: the desktop's settings, a
    // PAC script through libproxy or pacrunner). False when GIO is missing or
    // says nothing usable.
    bool FromGioResolver(const Gio& gio, const std::string& url, UltraNetProxyConfig& out) {
        if (!gio.resolverGetDefault || !gio.resolverLookup || url.empty()) return false;
        void* resolver = gio.resolverGetDefault();
        if (!resolver) return false;
        void* error = nullptr;
        char** uris = gio.resolverLookup(resolver, url.c_str(), nullptr, &error);
        if (error && gio.errorFree) gio.errorFree(error);
        bool found = false;
        for (char** p = uris; p && *p && !found; ++p) {
            const std::string uri = *p;
            if (uri.rfind("direct://", 0) == 0) break;   // the first answer is direct
            found = ParseProxyUrl(uri, out);
        }
        if (uris) gio.strfreev(uris);
        return found;
    }

    bool HostPort(const GSettingsReader& s, UltraNetProxyType type, UltraNetProxyConfig& out) {
        const std::string host = Trim(s.String("host"));
        const int port = s.Int("port");
        if (host.empty() || port <= 0) return false;
        out = UltraNetProxyConfig{};
        out.type = type;
        out.host = host;
        out.port = port;
        return true;
    }

    bool FromGnome(const std::string& url, Kind kind, UltraNetProxyConfig& out) {
        const Gio& gio = LoadGio();
        if (!gio.ok) return false;
        GSettingsReader proxy(gio, "org.gnome.system.proxy");
        if (!proxy.Valid()) return false;
        const std::string mode = proxy.String("mode");
        if (mode == "auto") {
            // The PAC script, or the network's WPAD: GIO's resolver runs it
            // where glib-networking can; otherwise there is nothing to go on.
            return FromGioResolver(gio, url, out);
        }
        if (mode != "manual") return false;
        bool found = false;
        if (kind == Kind::Http) {
            GSettingsReader http(gio, "org.gnome.system.proxy.http");
            found = http.Valid() && HostPort(http, UltraNetProxyType::Http, out);
        } else if (kind == Kind::Https) {
            GSettingsReader https(gio, "org.gnome.system.proxy.https");
            found = https.Valid() && HostPort(https, UltraNetProxyType::Http, out);
            // GNOME's "use the same proxy for all protocols" leaves https empty
            if (!found && proxy.Bool("use-same-proxy")) {
                GSettingsReader http(gio, "org.gnome.system.proxy.http");
                found = http.Valid() && HostPort(http, UltraNetProxyType::Http, out);
            }
        } else if (kind == Kind::Ftp) {
            GSettingsReader ftp(gio, "org.gnome.system.proxy.ftp");
            found = ftp.Valid() && HostPort(ftp, UltraNetProxyType::Http, out);
        }
        if (!found) {
            GSettingsReader socks(gio, "org.gnome.system.proxy.socks");
            found = socks.Valid() && HostPort(socks, UltraNetProxyType::Socks5, out);
        }
        if (!found) return false;
        if (out.type == UltraNetProxyType::Http) {
            // The sign-in GNOME keeps for its HTTP proxy
            GSettingsReader http(gio, "org.gnome.system.proxy.http");
            if (http.Valid() && http.Bool("use-authentication")) {
                out.credentials.type = UltraNetAuthType::Basic;
                out.credentials.username = http.String("authentication-user");
                out.credentials.password = http.String("authentication-password");
            }
        }
        out.noProxyHosts = proxy.List("ignore-hosts");
        return true;
    }

    // ---- 2b. KDE: ~/.config/kioslaverc --------------------------------------

    // "http://host:8080", "host:8080" or KDE's older "host 8080".
    bool ParseKdeEntry(std::string value, UltraNetProxyType defaultType, UltraNetProxyConfig& out) {
        value = Trim(value);
        if (value.empty()) return false;
        const std::size_t space = value.rfind(' ');
        if (space != std::string::npos)
            value = Trim(value.substr(0, space)) + ":" + Trim(value.substr(space + 1));
        if (value.find("://") == std::string::npos)
            value = std::string(defaultType == UltraNetProxyType::Socks5 ? "socks5h://" : "http://") + value;
        else if (defaultType == UltraNetProxyType::Socks5 && value.rfind("socks", 0) != 0)
            value = "socks5h://" + value.substr(value.find("://") + 3);
        return ParseProxyUrl(value, out);
    }

    bool FromKde(Kind kind, UltraNetProxyConfig& out) {
        std::string dir = UltraCanvas::GetEnvUtf8("XDG_CONFIG_HOME");
        if (dir.empty()) {
            const std::string home = UltraCanvas::GetEnvUtf8("HOME");
            if (home.empty()) return false;
            dir = home + "/.config";
        }
        std::FILE* f = UltraCanvas::OpenFileUtf8(dir + "/kioslaverc", "rb");
        if (!f) return false;
        std::string text;
        char buf[4096];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        std::fclose(f);

        std::string section, type, http, https, ftp, socks, noProxy;
        bool reversed = false;
        std::size_t pos = 0;
        while (pos <= text.size()) {
            std::size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            const std::string line = Trim(text.substr(pos, end - pos));
            pos = end + 1;
            if (line.empty() || line[0] == '#') continue;
            if (line[0] == '[') { section = line; continue; }
            if (section != "[Proxy Settings]") continue;
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = Trim(line.substr(0, eq));
            const std::string value = Trim(line.substr(eq + 1));
            if (key == "ProxyType") type = value;
            else if (key == "httpProxy") http = value;
            else if (key == "httpsProxy") https = value;
            else if (key == "ftpProxy") ftp = value;
            else if (key == "socksProxy") socks = value;
            else if (key == "NoProxyFor") noProxy = value;
            else if (key == "ReversedException") reversed = value == "true";
        }
        if (type != "1") return false;   // 1 = manual; 0 none, 2 PAC, 3 WPAD, 4 environment
        bool found = false;
        if (kind == Kind::Http) found = ParseKdeEntry(http, UltraNetProxyType::Http, out);
        else if (kind == Kind::Https) found = ParseKdeEntry(https, UltraNetProxyType::Http, out);
        else if (kind == Kind::Ftp) found = ParseKdeEntry(ftp, UltraNetProxyType::Http, out);
        if (!found) found = ParseKdeEntry(socks, UltraNetProxyType::Socks5, out);
        if (!found) return false;
        // A reversed list names the hosts that DO use the proxy; that is not
        // a list of exceptions, so it is left out rather than misread.
        if (!reversed) out.noProxyHosts = UltraNet_ParseNoProxyList(noProxy);
        return true;
    }

    bool OnKde() {
        const std::string desktop = Lower(UltraCanvas::GetEnvUtf8("XDG_CURRENT_DESKTOP"));
        return desktop.find("kde") != std::string::npos;
    }

} // namespace

void UltraNet_DetectSystemProxyFor(const std::string& url, const std::string& autoConfigUrl,
                                   UltraNetProxyConfig& outProxy) {
    (void)autoConfigUrl;   // no PAC engine of its own here (UltraNet_CanEvaluateProxyScript)
    outProxy = UltraNetProxyConfig{};
    const Kind kind = KindOf(SchemeOf(url));
    if (AnyProxyVariable()) {
        FromEnvironment(kind, outProxy);
        return;
    }
    UltraNetProxyConfig found;
    const bool kdeFirst = OnKde();
    if ((kdeFirst && FromKde(kind, found)) || FromGnome(url, kind, found) ||
        (!kdeFirst && FromKde(kind, found))) {
        outProxy = found;
    }
}

void UltraNet_DetectSystemProxy(UltraNetProxyConfig& outProxy) {
    // What a settings page shows: the web's proxy, as before - https_proxy,
    // else http_proxy, else the desktop's.
    UltraNet_DetectSystemProxyFor("https://example.com/", std::string(), outProxy);
    if (!outProxy.IsEnabled())
        UltraNet_DetectSystemProxyFor("http://example.com/", std::string(), outProxy);
}

bool UltraNet_CanEvaluateProxyScript() {
    return false;
}

#endif // __linux__
