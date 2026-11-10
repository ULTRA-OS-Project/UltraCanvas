// core/UltraCanvasProxySettings.cpp
// Proxy.conf, the remembered password, UltraNet following both, and the
// connection check. See UltraCanvasProxySettings.h.
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "UltraCanvasProxySettings.h"
#include "UltraCanvasSettingsFolder.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasDebug.h"
#include "UltraLocalisation/UltraLocalisation.h"   // Tr, TrF

#ifdef ULTRACANVAS_HAS_NET
#include "UltraNet/UltraNetHttp.h"
#include <UltraVault/UltraVaultDeviceKeyVault.h>
#endif

#include <chrono>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>

namespace UltraCanvas {

    namespace {

        std::string TrimSpace(const std::string& s) {
            std::size_t b = 0, e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
            return s.substr(b, e - b);
        }

        // A whole number from the file; `fallback` for anything else.
        long long ParseWhole(const std::string& text, long long fallback) {
            const std::string t = TrimSpace(text);
            if (t.empty() || t.size() > 18) return fallback;
            long long v = 0;
            for (const char c : t) {
                if (c < '0' || c > '9') return fallback;
                v = v * 10 + (c - '0');
            }
            return v;
        }

        // Newlines would start another key: a value is one line.
        std::string OneLine(const std::string& s) {
            std::string out;
            for (const char c : s) out += (c == '\n' || c == '\r') ? ' ' : c;
            return out;
        }

        std::string ReadFileText(const std::filesystem::path& path) {
            if (path.empty()) return {};
            std::ifstream in(path, std::ios::binary);
            if (!in) return {};
            std::ostringstream text;
            text << in.rdbuf();
            return text.str();
        }

        // The password this program was told (typed into the dialog, whether
        // remembered or not), for this program's own connections.
        std::mutex g_passwordMutex;
        std::string g_sessionPassword;
        bool g_haveSessionPassword = false;

        void SetSessionPassword(const std::string& password, bool have) {
            std::lock_guard<std::mutex> lk(g_passwordMutex);
            g_sessionPassword = password;
            g_haveSessionPassword = have;
        }

        bool SessionPassword(std::string& out) {
            std::lock_guard<std::mutex> lk(g_passwordMutex);
            if (g_haveSessionPassword) out = g_sessionPassword;
            return g_haveSessionPassword;
        }

#ifdef ULTRACANVAS_HAS_NET
        // The vault the remembered password is in: <settings folder>/Proxy/.
        constexpr const char* kVaultAccount = "manual";

        UltraVault::DeviceKeyVault ProxyVault() {
            const std::filesystem::path folder = UltraCanvasSettingsFolder();
            if (folder.empty()) return {};
            return UltraVault::DeviceKeyVault(PathToUtf8(folder / "Proxy"),
                                              UltraVault::DeviceKeyVaultProfile{"proxy.vault", "ultracanvas.proxy."});
        }

        bool RetrieveRememberedPassword(std::string& out) {
            UltraVault::DeviceKeyVault vault = ProxyVault();
            if (vault.GetDirectory().empty() || !vault.Exists()) return false;
            bool found = false;
            vault.Use([&](UltraVault::DeviceKeyVault& v) { found = v.Retrieve(kVaultAccount, out); });
            return found;
        }

        // What UltraNet follows (UseSharedProxySettings): the file, read
        // again when two seconds have passed and loaded again when its text
        // changed - every save writes a new `saved=` line, so a save that
        // changed only the password is seen too.
        struct SharedState {
            std::mutex mutex;
            bool loaded = false;
            std::string fileText;
            UltraNetProxyConfig config = UltraNetProxyConfig::FromSystem();
            std::chrono::steady_clock::time_point checkedAt;
        };

        SharedState& Shared() {
            static SharedState state;
            return state;
        }

        UltraNetProxyConfig SharedProxySource() {
            SharedState& s = Shared();
            std::lock_guard<std::mutex> lk(s.mutex);
            const auto now = std::chrono::steady_clock::now();
            if (s.loaded && now - s.checkedAt < std::chrono::seconds(2)) return s.config;
            s.checkedAt = now;
            const std::string text = ReadFileText(ProxySettings::FilePath());
            if (!s.loaded || text != s.fileText) {
                s.fileText = text;
                s.config = ProxySettings::Load().ToUltraNet();
                s.loaded = true;
            }
            return s.config;
        }

        void ReloadShared() {
            SharedState& s = Shared();
            std::lock_guard<std::mutex> lk(s.mutex);
            s.loaded = false;
        }
#endif

    } // namespace

    const char* ProxyModeKey(ProxyMode mode) {
        switch (mode) {
            case ProxyMode::NoProxy: return "none";
            case ProxyMode::Manual:  return "manual";
            case ProxyMode::System:  break;
        }
        return "system";
    }

    bool ParseProxyMode(const std::string& key, ProxyMode& out) {
        if (key == "system") { out = ProxyMode::System; return true; }
        if (key == "none")   { out = ProxyMode::NoProxy; return true; }
        if (key == "manual") { out = ProxyMode::Manual; return true; }
        return false;
    }

    int ProxySettings::EffectivePort() const {
        if (port > 0 && port <= 65535) return port;
        return kind == ProxyKind::Socks ? 1080 : 80;
    }

    ProxySettings ProxySettings::Parse(const std::string& text) {
        ProxySettings s;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = TrimSpace(line.substr(0, eq));
            const std::string value = line.substr(eq + 1);
            if (key == "mode") ParseProxyMode(TrimSpace(value), s.mode);
            else if (key == "pac-enabled") s.useAutoConfigUrl = TrimSpace(value) == "1";
            else if (key == "pac") s.autoConfigUrl = TrimSpace(value);
            else if (key == "type") {
                const std::string t = TrimSpace(value);
                if (t == "http") s.kind = ProxyKind::Http;
                else if (t == "socks") s.kind = ProxyKind::Socks;
            }
            else if (key == "host") s.host = TrimSpace(value);
            else if (key == "port") {
                const long long p = ParseWhole(value, 0);
                s.port = p > 0 && p <= 65535 ? static_cast<int>(p) : 0;
            }
            else if (key == "no-proxy") s.noProxyFor = TrimSpace(value);
            else if (key == "auth") s.authenticate = TrimSpace(value) == "1";
            else if (key == "user") s.user = value;
            else if (key == "remember") s.rememberPassword = TrimSpace(value) == "1";
            else if (key == "saved") s.savedAt = ParseWhole(value, 0);
        }
        return s;
    }

    std::string ProxySettings::Serialize() const {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "mode=" << ProxyModeKey(mode) << "\n"
            << "pac-enabled=" << (useAutoConfigUrl ? 1 : 0) << "\n"
            << "pac=" << OneLine(TrimSpace(autoConfigUrl)) << "\n"
            << "type=" << (kind == ProxyKind::Socks ? "socks" : "http") << "\n"
            << "host=" << OneLine(TrimSpace(host)) << "\n"
            << "port=" << (port > 0 && port <= 65535 ? port : 0) << "\n"
            << "no-proxy=" << OneLine(TrimSpace(noProxyFor)) << "\n"
            << "auth=" << (authenticate ? 1 : 0) << "\n"
            << "user=" << OneLine(user) << "\n"
            << "remember=" << (rememberPassword ? 1 : 0) << "\n"
            << "saved=" << savedAt << "\n";
        return out.str();
    }

    std::filesystem::path ProxySettings::FilePath() {
        const std::filesystem::path folder = UltraCanvasSettingsFolder();
        if (folder.empty()) return {};
        return folder / "Proxy.conf";
    }

    ProxySettings ProxySettings::Load(bool withPassword) {
        ProxySettings s = Parse(ReadFileText(FilePath()));
        if (s.authenticate && withPassword) {
            std::string password;
            if (SessionPassword(password)) s.password = password;
#ifdef ULTRACANVAS_HAS_NET
            else if (s.rememberPassword && RetrieveRememberedPassword(password)) s.password = password;
#endif
        }
        return s;
    }

    bool ProxySettings::Save() {
        const std::filesystem::path path = FilePath();
        if (path.empty()) return false;
        savedAt = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch()).count();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::filesystem::path temp = path;
        temp += ".tmp";
        {
            std::ofstream out(temp, std::ios::trunc | std::ios::binary);
            if (!out) return false;
            out << Serialize();
            if (!out) return false;
        }
        std::filesystem::rename(temp, path, ec);
        if (ec) {
            std::filesystem::remove(temp, ec);
            return false;
        }

        const bool signsIn = mode == ProxyMode::Manual && authenticate;
        SetSessionPassword(password, signsIn);
#ifdef ULTRACANVAS_HAS_NET
        UltraVault::DeviceKeyVault vault = ProxyVault();
        if (!vault.GetDirectory().empty() && (signsIn && rememberPassword ? true : vault.Exists())) {
            const bool keep = signsIn && rememberPassword && !password.empty();
            const bool opened = vault.Use([&](UltraVault::DeviceKeyVault& v) {
                if (keep) v.Store(kVaultAccount, password);
                else v.Remove(kVaultAccount);
            });
            if (!opened && keep)
                debugOutput << "UltraCanvas: the proxy password could not be remembered - "
                            << UltraVault::DeviceKeyVault::DescribeUnlockStatus(vault.GetLastUnlockStatus())
                            << std::endl;
        }
        ReloadShared();
        UltraNet_ForgetSystemProxy();
#endif
        return true;
    }

    bool ProxySettings::ForgetPasswords() {
        SetSessionPassword(std::string(), false);
#ifdef ULTRACANVAS_HAS_NET
        ReloadShared();
        UltraVault::DeviceKeyVault vault = ProxyVault();
        if (vault.GetDirectory().empty() || !vault.Exists()) return true;
        return vault.Use([](UltraVault::DeviceKeyVault& v) { v.Remove(kVaultAccount); });
#else
        return true;
#endif
    }

    bool ProxySettings::CanUseAutoConfigUrl() {
#ifdef ULTRACANVAS_HAS_NET
        return UltraNet_CanEvaluateProxyScript();
#else
        return false;
#endif
    }

#ifdef ULTRACANVAS_HAS_NET
    UltraNetProxyConfig ProxySettings::ToUltraNet() const {
        switch (mode) {
            case ProxyMode::NoProxy:
                return UltraNetProxyConfig{};   // every connection direct
            case ProxyMode::System: {
                UltraNetProxyConfig p = UltraNetProxyConfig::FromSystem();
                if (useAutoConfigUrl) p.autoConfigUrl = TrimSpace(autoConfigUrl);
                return p;
            }
            case ProxyMode::Manual:
                break;
        }
        UltraNetProxyConfig p;
        const std::string h = TrimSpace(host);
        if (h.empty()) return p;   // nothing named: direct
        p.type = kind == ProxyKind::Socks ? UltraNetProxyType::Socks5 : UltraNetProxyType::Http;
        p.remoteDns = true;
        p.host = h;
        p.port = EffectivePort();
        p.noProxyHosts = UltraNet_ParseNoProxyList(noProxyFor);
        if (authenticate && !user.empty()) {
            p.credentials.type = UltraNetAuthType::Basic;
            p.credentials.username = user;
            p.credentials.password = password;
        }
        return p;
    }
#endif

    void UseSharedProxySettings() {
#ifdef ULTRACANVAS_HAS_NET
        static std::once_flag once;
        std::call_once(once, []() { UltraNet_SetGlobalProxySource(&SharedProxySource); });
#endif
    }

    ProxyCheckResult CheckProxyConnection(const ProxySettings& settings, const std::string& url) {
        ProxyCheckResult r;
#ifdef ULTRACANVAS_HAS_NET
        UltraNetProxyConfig proxy = settings.ToUltraNet();
        // NoProxy on a request means "the global setting"; the check means
        // what it says.
        if (!proxy.IsEnabled()) proxy = UltraNetProxyConfig::DirectConnection();
        r.route = UltraNet_DescribeProxy(UltraNet_ResolveProxy(url, proxy));

        UltraNetHttpRequest request;
        request.url = url;
        request.method = UltraNetHttpMethod::Head;
        request.options.proxy = proxy;
        request.options.timeoutMs = 20000;
        request.options.connectTimeoutMs = 15000;
        UltraNetResponse response;
        const auto start = std::chrono::steady_clock::now();
        const UltraNetResult result = UltraNet_HttpRequest(request, response);
        r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        r.httpStatus = response.statusCode > 0 ? response.statusCode : result.httpStatus;

        if (r.httpStatus == 407) {
            r.message = Tr("The proxy asks for a user name and password (HTTP 407).");
            return r;
        }
        if (r.httpStatus > 0) {
            // Any answer from the server: the way there works, whatever it said.
            r.success = true;
            r.message = r.route.empty()
                ? TrF("Connected directly: the server answered with HTTP {0}.", r.httpStatus)
                : TrF("Connected through {0}: the server answered with HTTP {1}.", r.route, r.httpStatus);
            return r;
        }
        r.message = TrF("Could not connect: {0}", result.message.empty() ? Tr("no answer") : result.message);
#else
        (void)settings;
        (void)url;
        r.message = Tr("This build has no networking.");
#endif
        return r;
    }

} // namespace UltraCanvas
