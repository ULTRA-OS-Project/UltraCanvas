// core/UltraNet/UltraNetCore.cpp
// Lifecycle, global config, version reporting. Per-platform UltraNetSupport.*
// supplies UltraNet_DetectSystemProxy.
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetCore.h"

#include <curl/curl.h>

#include <mutex>
#include <sstream>

namespace ultranet_internal {
    // Defined in UltraNetHttpAsync.cpp. Joins the worker thread (if any) so
    // it can't outlive curl_global_cleanup below.
    void StopAsyncWorker();
}

namespace {
    std::mutex          g_mutex;
    bool                g_initialized = false;
    UltraNetConfig      g_config = UltraNetConfig::Default();
}

UltraNetResult UltraNet_Initialize(const UltraNetConfig& config) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_initialized) {
        g_config = config;
        return UltraNetResult::Ok();
    }
    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK) {
        return UltraNetResult::Error(UltraNetResultCode::Unknown,
                                     curl_easy_strerror(rc));
    }
    g_config = config;
    g_initialized = true;
    return UltraNetResult::Ok();
}

void UltraNet_Shutdown() {
    // Joining the worker outside g_mutex avoids deadlocks if a worker
    // callback re-enters into a UltraNet_* getter that takes g_mutex.
    ultranet_internal::StopAsyncWorker();
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_initialized) return;
    curl_global_cleanup();
    g_initialized = false;
}

bool UltraNet_IsInitialized() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_initialized;
}

UltraNetConfig UltraNet_GetConfig() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_config;
}

void UltraNet_SetConfig(const UltraNetConfig& config) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_config = config;
}

std::string UltraNet_GetVersion() {
    return "0.1.0";
}

std::string UltraNet_GetBackendInfo() {
    const curl_version_info_data* v = curl_version_info(CURLVERSION_NOW);
    std::ostringstream os;
    os << "libcurl/" << (v && v->version ? v->version : "unknown");
    if (v && v->ssl_version) {
        os << ' ' << v->ssl_version;
    }
    return os.str();
}

void UltraNet_SetGlobalProxy(const UltraNetProxyConfig& proxy) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_config.proxy = proxy;
}

UltraNetProxyConfig UltraNet_GetGlobalProxy() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_config.proxy;
}

void UltraNet_DisableProxy() {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_config.proxy = UltraNetProxyConfig{};
}
