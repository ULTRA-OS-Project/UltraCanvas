// Tests/ProxySettingsTest.cpp
// UltraCanvasProxySettings on a temp settings folder: Proxy.conf written and
// read back without the password, the defaults (the system's proxy), the
// settings as UltraNet's, the remembered password in the vault and
// forgotten again, UltraNet following the file (UseSharedProxySettings) -
// also a change written by "another application" - and the connection check
// through a one-shot proxy on 127.0.0.1. Headless.
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasProxySettings.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace UltraCanvas;
namespace fs = std::filesystem;

static int g_checks = 0;
static int g_failures = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

static std::string ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// The settings folder moved into `dir`, on every platform: the variable each
// one reads it from.
static void PointSettingsFolderAt(const fs::path& dir) {
#ifdef _WIN32
    SetEnvironmentVariableW(L"APPDATA", dir.wstring().c_str());   // path-string-ok: a wide path
#else
    setenv("XDG_CONFIG_HOME", PathToUtf8(dir).c_str(), 1);
    setenv("HOME", PathToUtf8(dir).c_str(), 1);   // macOS: ~/Library/Application Support
#endif
}

#if defined(ULTRACANVAS_HAS_NET) && !defined(_WIN32)
// A proxy that answers one request with 200 and records its request line.
struct OneShotProxy {
    int listener = -1;
    int port = 0;
    std::string requestLine;
    std::thread worker;

    bool Start() {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) return false;
        socklen_t len = sizeof addr;
        if (::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
        port = ntohs(addr.sin_port);
        if (::listen(listener, 1) != 0) return false;
        worker = std::thread([this]() {
            const int c = ::accept(listener, nullptr, nullptr);
            if (c < 0) return;
            std::string got;
            char buf[2048];
            while (got.find("\r\n\r\n") == std::string::npos) {
                const ssize_t n = ::recv(c, buf, sizeof buf, 0);
                if (n <= 0) break;
                got.append(buf, static_cast<std::size_t>(n));
            }
            requestLine = got.substr(0, got.find("\r\n"));
            const char reply[] = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            ::send(c, reply, sizeof reply - 1, 0);
            ::close(c);
        });
        return true;
    }
    ~OneShotProxy() {
        if (listener >= 0) ::shutdown(listener, SHUT_RDWR);
        if (worker.joinable()) worker.join();
        if (listener >= 0) ::close(listener);
    }
};
#endif

int main() {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() /
        ("ProxySettingsTest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir, ec);
    PointSettingsFolderAt(dir);
    const fs::path folder = UltraCanvasSettingsFolder();
    Check(!folder.empty(), "the settings folder is named");
    Check(ProxySettings::FilePath() == folder / "Proxy.conf", "the file is <settings folder>/Proxy.conf");

    std::printf("The defaults\n");
    {
        const ProxySettings none = ProxySettings::Load();
        Check(none.mode == ProxyMode::System, "no file: the system's proxy");
        Check(none.EffectivePort() == 80, "an HTTP proxy's own port is 80");
        ProxySettings socks;
        socks.kind = ProxyKind::Socks;
        Check(socks.EffectivePort() == 1080, "a SOCKS proxy's own port is 1080");
    }

    std::printf("The file's text\n");
    {
        ProxySettings s;
        s.mode = ProxyMode::Manual;
        s.kind = ProxyKind::Socks;
        s.host = "localhost";
        s.port = 10808;
        s.noProxyFor = "*.example.com, 192.168.*";
        s.authenticate = true;
        s.user = "erika";
        s.password = "secret word";
        s.rememberPassword = true;
        s.useAutoConfigUrl = true;
        s.autoConfigUrl = "https://example.com/wpad.pac";
        s.savedAt = 1762780000000LL;
        const std::string text = s.Serialize();
        Check(text.find("secret") == std::string::npos, "the password is never in the file");
        ProxySettings back = ProxySettings::Parse(text);
        back.password = s.password;
        Check(back == s, "every other field reads back");
        const ProxySettings odd = ProxySettings::Parse("mode=sideways\nport=99999\ntype=gopher\nwhatever=1\nhost= h \n");
        Check(odd.mode == ProxyMode::System && odd.port == 0 && odd.kind == ProxyKind::Http && odd.host == "h",
              "unknown values keep the defaults, unknown keys are ignored");
    }

#ifdef ULTRACANVAS_HAS_NET
    std::printf("As UltraNet's setting\n");
    {
        ProxySettings s;
        Check(s.ToUltraNet().type == UltraNetProxyType::System, "system");
        s.useAutoConfigUrl = true;
        s.autoConfigUrl = " https://example.com/wpad.pac ";
        Check(s.ToUltraNet().autoConfigUrl == "https://example.com/wpad.pac", "system with a PAC address");
        s.mode = ProxyMode::NoProxy;
        Check(s.ToUltraNet().type == UltraNetProxyType::NoProxy, "no proxy: every connection direct");
        s.mode = ProxyMode::Manual;
        Check(s.ToUltraNet().type == UltraNetProxyType::NoProxy, "manual without a host: direct");
        s.host = "proxy.test";
        s.noProxyFor = "a.test; b.test";
        UltraNetProxyConfig p = s.ToUltraNet();
        Check(p.type == UltraNetProxyType::Http && p.host == "proxy.test" && p.port == 80 &&
              p.noProxyHosts.size() == 2, "manual HTTP with its exceptions");
        s.kind = ProxyKind::Socks;
        s.port = 10808;
        s.authenticate = true;
        s.user = "erika";
        s.password = "pw";
        p = s.ToUltraNet();
        Check(p.type == UltraNetProxyType::Socks5 && p.remoteDns && p.port == 10808 &&
              p.credentials.username == "erika" && p.credentials.password == "pw",
              "manual SOCKS, names looked up by the proxy, with a sign-in");
    }

    std::printf("Saved, remembered, forgotten\n");
    {
        ProxySettings s;
        s.mode = ProxyMode::Manual;
        s.kind = ProxyKind::Http;
        s.host = "proxy.test";
        s.port = 3128;
        s.authenticate = true;
        s.user = "erika";
        s.password = "remember me";
        s.rememberPassword = true;
        Check(s.Save(), "saved");
        Check(s.savedAt > 0, "with the time it was saved");
        const std::string text = ReadAll(folder / "Proxy.conf");
        Check(text.find("host=proxy.test") != std::string::npos, "the file holds the host");
        Check(text.find("remember me") == std::string::npos, "and not the password");
        Check(ProxySettings::Load().password == "remember me", "the password comes back with the settings");

        UseSharedProxySettings();
        UltraNetProxyConfig now = UltraNet_GetGlobalProxy();
        Check(now.type == UltraNetProxyType::Http && now.host == "proxy.test" && now.port == 3128 &&
              now.credentials.password == "remember me", "UltraNet follows the saved settings at once");

        // Another application writes the file: picked up within two seconds
        {
            std::ofstream other(folder / "Proxy.conf", std::ios::trunc | std::ios::binary);
            other << "mode=none\nsaved=1\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        Check(UltraNet_GetGlobalProxy().type == UltraNetProxyType::NoProxy,
              "a change another application saved is followed");

        Check(ProxySettings::ForgetPasswords(), "the remembered passwords are removed");
        ProxySettings again = ProxySettings::Parse(ReadAll(folder / "Proxy.conf"));
        again.mode = ProxyMode::Manual;
        again.host = "proxy.test";
        again.authenticate = true;
        again.rememberPassword = true;
        Check(again.Save(), "saved again, without a password typed");
        Check(ProxySettings::Load().password.empty(), "no password comes back after Clear passwords");
    }

#ifndef _WIN32
    std::printf("Check connection\n");
    {
        OneShotProxy proxy;
        Check(proxy.Start(), "a proxy on 127.0.0.1");
        ProxySettings s;
        s.mode = ProxyMode::Manual;
        s.host = "127.0.0.1";
        s.port = proxy.port;
        const ProxyCheckResult r = CheckProxyConnection(s, "http://only-the-proxy-knows.invalid/");
        Check(r.success && r.httpStatus == 200, "the check went through the proxy: " + r.message);
        Check(r.route == "http://127.0.0.1:" + std::to_string(proxy.port), "and says which: " + r.route);
        if (proxy.worker.joinable()) proxy.worker.join();
        Check(proxy.requestLine == "HEAD http://only-the-proxy-knows.invalid/ HTTP/1.1",
              "the proxy was asked for the address: " + proxy.requestLine);

        ProxySettings direct;
        direct.mode = ProxyMode::NoProxy;
        const ProxyCheckResult failed = CheckProxyConnection(direct, "http://127.0.0.1:1/");
        Check(!failed.success && failed.route.empty() && !failed.message.empty(),
              "a direct check that cannot connect says so: " + failed.message);
    }
#endif
#endif

    fs::remove_all(dir, ec);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
