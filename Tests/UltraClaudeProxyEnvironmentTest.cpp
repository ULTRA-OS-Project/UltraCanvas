// Tests/UltraClaudeProxyEnvironmentTest.cpp
// UltraClaude hands the shared proxy settings to the `claude` CLI through its
// environment (ClaudeProxyEnvironment.h): the variables for each setting, the
// sign-in and the exceptions written as the CLI reads them, the merge into an
// environment (case ignored on Windows, the "=C:" entries kept) - and, on
// POSIX, a child started by ClaudeCliProcess that prints what it was given.
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ClaudeCliProcess.h"
#include "ClaudeProxyEnvironment.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

using namespace UltraClaude;

static int g_checks = 0;
static int g_failures = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

static std::string ValueOf(const EnvironmentChanges& c, const std::string& name) {
    for (const auto& kv : c.set)
        if (kv.first == name) return kv.second;
    return {};
}

static bool Unsets(const EnvironmentChanges& c, const std::string& name) {
    return std::find(c.unset.begin(), c.unset.end(), name) != c.unset.end();
}

int main() {
#ifdef ULTRACANVAS_HAS_NET
    std::printf("The variables for each setting\n");
    {
        const EnvironmentChanges none = ProxyEnvironmentFor(UltraNetProxyConfig(), UltraNetProxyConfig(), true);
        Check(none.set.empty() && Unsets(none, "HTTPS_PROXY") && Unsets(none, "http_proxy") &&
              Unsets(none, "ALL_PROXY"), "no proxy: the proxy variables are removed");

        UltraNetProxyConfig http;
        http.type = UltraNetProxyType::Http;
        http.host = "proxy.test";
        http.port = 3128;
        http.credentials.username = "erika";
        http.credentials.password = "p@ss word";
        http.noProxyHosts = {"*.corp.test", "192.168.*", "<local>", "intranet"};

        const UltraNetProxyConfig system = UltraNetProxyConfig::FromSystem();
        Check(ProxyEnvironmentFor(system, http, /*environmentHasProxy=*/true).Empty(),
              "the system's setting leaves an environment that names a proxy alone");
        Check(ProxyEnvironmentFor(system, UltraNetProxyConfig(), false).Empty(),
              "the system's setting with no proxy for the API changes nothing");

        const EnvironmentChanges fromSystem = ProxyEnvironmentFor(system, http, false);
        Check(ValueOf(fromSystem, "HTTPS_PROXY") == "http://erika:p%40ss%20word@proxy.test:3128",
              "the system's HTTP proxy with its sign-in: " + ValueOf(fromSystem, "HTTPS_PROXY"));
        Check(ValueOf(fromSystem, "https_proxy") == ValueOf(fromSystem, "HTTPS_PROXY") &&
              ValueOf(fromSystem, "HTTP_PROXY") == ValueOf(fromSystem, "HTTPS_PROXY"),
              "in every name the CLI and its tools read");
        Check(ValueOf(fromSystem, "NO_PROXY") == "localhost,127.0.0.1,::1,.corp.test,intranet",
              "the exceptions it can say, the machine itself first: " + ValueOf(fromSystem, "NO_PROXY"));

        Check(ValueOf(ProxyEnvironmentFor(http, UltraNetProxyConfig(), true), "HTTPS_PROXY") ==
              "http://erika:p%40ss%20word@proxy.test:3128",
              "a proxy chosen in the dialog wins over the environment's");

        UltraNetProxyConfig socks;
        socks.type = UltraNetProxyType::Socks5;
        socks.host = "localhost";
        socks.port = 10808;
        const EnvironmentChanges s = ProxyEnvironmentFor(socks, UltraNetProxyConfig(), true);
        Check(ValueOf(s, "ALL_PROXY") == "socks5h://localhost:10808" && ValueOf(s, "HTTPS_PROXY").empty() &&
              Unsets(s, "HTTPS_PROXY"), "SOCKS: ALL_PROXY for the tools, no stale HTTP proxy");
    }
#endif

    std::printf("Merged into an environment\n");
    {
        EnvironmentChanges c;
        c.set = {{"HTTPS_PROXY", "http://p:1"}, {"https_proxy", "http://p:1"}};
        c.unset = {"ALL_PROXY"};
        const std::vector<std::string> posix = ApplyEnvironmentChanges(
            {"PATH=/bin", "https_proxy=http://old:2", "ALL_PROXY=socks5://old:3", "HOME=/h"}, c, false);
        Check(posix == std::vector<std::string>{"PATH=/bin", "HOME=/h", "HTTPS_PROXY=http://p:1",
                                                "https_proxy=http://p:1"},
              "POSIX: the old values replaced, the removed one gone, the rest kept in order");

        const std::vector<std::string> windows = ApplyEnvironmentChanges(
            {"=C:=C:\\Users", "Path=C:\\Windows", "Https_Proxy=http://old:2"}, c, true);
        Check(windows == std::vector<std::string>{"=C:=C:\\Users", "Path=C:\\Windows", "HTTPS_PROXY=http://p:1"},
              "Windows: names compared without case, written once, the drive entries kept");
    }

#if defined(ULTRACANVAS_HAS_NET) && !defined(_WIN32)
    std::printf("A child started by ClaudeCliProcess\n");
    {
        const UltraNetProxyConfig saved = UltraNet_GetGlobalProxy();
        UltraNetProxyConfig http;
        http.type = UltraNetProxyType::Http;
        http.host = "proxy.test";
        http.port = 8080;
        UltraNet_SetGlobalProxy(http);

        std::mutex m;
        std::condition_variable cv;
        bool done = false;
        std::vector<std::string> lines;
        ClaudeCliProcess child;
        std::string error;
        const bool started = child.Start(
            {"/bin/sh", "-c", "printf '%s\\n' \"$HTTPS_PROXY\" \"$NO_PROXY\""}, std::string(), std::string(),
            [&](const std::string& line) {
                std::lock_guard<std::mutex> lk(m);
                lines.push_back(line);
            },
            [&](int, const std::string&) {
                std::lock_guard<std::mutex> lk(m);
                done = true;
                cv.notify_all();
            },
            error);
        Check(started, "the child started: " + error);
        {
            std::unique_lock<std::mutex> lk(m);
            cv.wait_for(lk, std::chrono::seconds(10), [&] { return done; });
        }
        child.StopAndWait();
        UltraNet_SetGlobalProxy(saved);
        Check(lines.size() == 2 && lines[0] == "http://proxy.test:8080" && lines[1] == "localhost,127.0.0.1,::1",
              "it was given the proxy: " + (lines.empty() ? std::string("(nothing)") : lines[0]));
    }
#endif

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
