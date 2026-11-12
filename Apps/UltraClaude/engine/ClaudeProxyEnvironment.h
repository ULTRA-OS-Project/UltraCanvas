// Apps/UltraClaude/engine/ClaudeProxyEnvironment.h
// The proxy settings every ULTRA OS application shares (UltraCanvasProxySettings.h),
// handed to the `claude` CLI. The CLI makes its own connections, so UltraNet
// following the setting does nothing for it; what reaches it is its
// environment, which it - and every program its tools run (git, curl) - reads
// the proxy from: HTTPS_PROXY / HTTP_PROXY / NO_PROXY, in both cases.
//
//   No proxy        the proxy variables are removed: the person said direct
//   System          the environment is left alone when it names a proxy
//                   already (that is the system's answer on Linux, and what
//                   the CLI follows anywhere); otherwise the proxy the system
//                   names for the API's address - Windows' and macOS'
//                   settings, a PAC script - is written into it
//   Manual HTTP     HTTPS_PROXY / HTTP_PROXY with the sign-in, NO_PROXY with
//                   the exceptions (and the machine itself)
//   Manual SOCKS    ALL_PROXY, for the programs its tools run; the old HTTP
//                   proxy variables are removed. The CLI itself takes only an
//                   HTTP proxy, so its own connections then go directly.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>
#include <utility>
#include <vector>

#ifdef ULTRACANVAS_HAS_NET
#include "UltraNet/UltraNetCore.h"
#endif

namespace UltraClaude {

// Variables to set and to remove in a child's environment.
struct EnvironmentChanges {
    std::vector<std::pair<std::string, std::string>> set;
    std::vector<std::string> unset;
    bool Empty() const { return set.empty() && unset.empty(); }
};

#ifdef ULTRACANVAS_HAS_NET
// The changes for the proxy `setting` (UltraNet's module-wide one); for
// System, `systemResolved` is what UltraNet resolved for the API's address.
// `environmentHasProxy`: this program's environment names a proxy already.
// Pure, for the tests.
EnvironmentChanges ProxyEnvironmentFor(const UltraNetProxyConfig& setting,
                                       const UltraNetProxyConfig& systemResolved,
                                       bool environmentHasProxy);
#endif

// The changes the shared proxy settings ask for now (none in a build
// without UltraNet). Asks the operating system when the setting is System,
// which may take a moment (WPAD): call it off the UI thread.
EnvironmentChanges ClaudeProxyEnvironment();

// `current` ("NAME=value" entries) with `changes` made: every entry of a name
// that is set or unset dropped - case-insensitively when `ignoreCase`, as
// Windows compares names - and the set ones appended. Entries without a name
// ("=C:=C:\\" on Windows) are kept as they are.
std::vector<std::string> ApplyEnvironmentChanges(const std::vector<std::string>& current,
                                                 const EnvironmentChanges& changes,
                                                 bool ignoreCase);

} // namespace UltraClaude
