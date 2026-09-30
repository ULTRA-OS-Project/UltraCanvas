// UltraCanvas/Plugins/UltraNet/common/UltraNetPluginHostShim.cpp
// The core functions an UltraNet plug-in calls, defined inside the plug-in
// and forwarded to the host's table - see UltraNetPluginHostShim.h.
//
// Built with hidden visibility (the plug-in CMakeLists set it on this file):
// the definitions are the plug-in's own and never interpose on the host's.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraNetPluginHostShim.h"

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>
#include <UltraNet/UltraNetMime.h>
#include <UltraNet/UltraNetPlugins.h>
#include <UltraNet/UltraNetUrl.h>

#include <string>

namespace {
const UltraNetPluginHost* g_host = nullptr;

UltraNetResult NoHost() {
    return UltraNetResult::Error(UltraNetResultCode::NotInitialized,
                                 "the UltraNet plug-in was not initialised by its host");
}
} // namespace

bool UltraNetPlugin_AttachHost(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 2 || !host->RegisterPlugin) return false;
    g_host = host;
    return true;
}

// Every function below is reached only after UltraNetPlugin_AttachHost()
// succeeded (the plug-in registers nothing otherwise); the null checks only
// keep a misuse from crashing the host.

UltraNetResult UltraNet_ParseUrl(const std::string& url, UltraNetUrlComponents& out) {
    return g_host ? g_host->ParseUrl(url, out) : NoHost();
}

std::string UltraNet_UrlEncode(const std::string& input) {
    return g_host ? g_host->UrlEncode(input) : std::string();
}

std::string UltraNet_UrlDecode(const std::string& input) {
    return g_host ? g_host->UrlDecode(input) : std::string();
}

std::string UltraNet_ResolveCaBundlePath() {
    return g_host ? g_host->ResolveCaBundlePath() : std::string();
}

std::string UltraNet_DescribeTrustRoots() {
    return g_host ? g_host->DescribeTrustRoots() : std::string();
}

std::string UltraNet_DescribePlatform() {
    return g_host ? g_host->DescribePlatform() : std::string();
}

std::string UltraNet_MimeBuild(const UltraNetMimeBuildInput& input) {
    return g_host ? g_host->MimeBuild(input) : std::string();
}

UltraNetResult UltraNet_HttpGet(const std::string& url, UltraNetResponse& out,
                                const UltraNetHttpOptions& options) {
    return g_host ? g_host->HttpGet(url, out, options) : NoHost();
}

UltraNetResult UltraNet_HttpRequest(const UltraNetHttpRequest& request,
                                    UltraNetResponse& out) {
    return g_host ? g_host->HttpRequest(request, out) : NoHost();
}

void UltraNetHttpHeaders::Set(const std::string& name, const std::string& value) {
    if (g_host) g_host->HttpHeadersSet(*this, name, value);
}
