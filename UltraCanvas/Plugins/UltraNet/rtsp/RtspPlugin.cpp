// UltraCanvas/Plugins/UltraNet/rtsp/RtspPlugin.cpp
// RTSP plug-in. Implements IStreamingProtocolPlugin on libcurl's native
// RTSP support — DESCRIBE / OPTIONS / SETUP / PLAY / PAUSE / TEARDOWN
// control-channel commands. The actual media stream (RTP payload over
// UDP/TCP) is out of scope for v0.1 — ReadFrame reports clearly that
// frame demuxing needs a separate library (ffmpeg / live555 / GStreamer).
//
// What IS useful from this v0.1 plug-in: DESCRIBE-based stream probing.
// Apps can OpenStream to learn what tracks the server offers (the SDP
// is left in the response body), pick a transport, and hand the actual
// playback off to a media library.
// Version: 0.1.1
// Last Modified: 2026-07-05
// Author: UltraCanvas Framework / ULTRA OS

#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetPlugins.h>

#include <curl/curl.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::size_t WriteString(char* p, std::size_t s, std::size_t n, void* ud) {
    static_cast<std::string*>(ud)->append(p, s * n);
    return s * n;
}
// MapCurlError-style helper deliberately omitted — Stage-0 RTSP only
// surfaces DESCRIBE success/failure via OpenStream; ReadFrame returns a
// fixed UnsupportedScheme until a media-library backend lands.

// Per-stream session state. We hold the URL + the SDP body returned by
// DESCRIBE so apps can pull the announcement out via a follow-up
// (the IStreamingProtocolPlugin interface doesn't expose it directly,
// but the SDP lives at the libcurl response and we cache it here for
// future use by ReadFrame / GetMetadata-style extensions).
struct Session {
    std::string url;
    UltraNetStreamOptions opts;
    std::string sdp;        // populated by OpenStream
};

std::mutex g_sessionsMutex;
std::unordered_map<UltraNetHandle, std::shared_ptr<Session>> g_sessions;
std::atomic<UltraNetHandle> g_nextHandle{1};

class RtspPlugin : public IStreamingProtocolPlugin {
public:
    std::string GetName() const override { return "UltraNet-RTSP"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"rtsp", "rtsps"};
    }
    UltraNetResult Initialize(const UltraNetConfig&) override {
        return UltraNetResult::Ok();
    }
    void Shutdown() override {
        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        g_sessions.clear();
    }

    UltraNetHandle OpenStream(const std::string& url,
                              const UltraNetStreamOptions& options) override {
        if (url.empty() || (url.rfind("rtsp://",  0) != 0 &&
                             url.rfind("rtsps://", 0) != 0)) {
            return UltraNetInvalidHandle;
        }
        // Run DESCRIBE to verify the URL is reachable and capture the SDP.
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> h(
            curl_easy_init(), curl_easy_cleanup);
        if (!h) return UltraNetInvalidHandle;

        std::string sdp;
        curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(h.get(), CURLOPT_RTSP_REQUEST, (long)CURL_RTSPREQ_DESCRIBE);
        curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, &WriteString);
        curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &sdp);
        curl_easy_setopt(h.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT_MS,
                         static_cast<long>(options.connectTimeoutMs));
        if (!options.credentials.username.empty()) {
            curl_easy_setopt(h.get(), CURLOPT_USERNAME,
                             options.credentials.username.c_str());
            curl_easy_setopt(h.get(), CURLOPT_PASSWORD,
                             options.credentials.password.c_str());
        }
        if (curl_easy_perform(h.get()) != CURLE_OK) return UltraNetInvalidHandle;

        auto s = std::make_shared<Session>();
        s->url  = url;
        s->opts = options;
        s->sdp  = std::move(sdp);

        const UltraNetHandle handle =
            g_nextHandle.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        g_sessions[handle] = std::move(s);
        return handle;
    }

    UltraNetResult ReadFrame(UltraNetHandle handle,
                             std::vector<uint8_t>& outFrame) override {
        outFrame.clear();
        std::lock_guard<std::mutex> lk(g_sessionsMutex);
        auto it = g_sessions.find(handle);
        if (it == g_sessions.end()) {
            return UltraNetResult::Error(UltraNetResultCode::InvalidHandle,
                                         "no such RTSP session");
        }
        // ReadFrame would deliver actual RTP payload bytes — which means
        // opening a separate UDP socket (or TCP-interleaved channel) and
        // depacketising H.264 / AAC / etc. That belongs in a media
        // library, not this plug-in. Return UnsupportedScheme with a clear
        // pointer so apps don't silently get empty frames.
        return UltraNetResult::Error(UltraNetResultCode::UnsupportedScheme,
            "RTSP plug-in provides control-channel only (DESCRIBE/OPTIONS); "
            "RTP frame demux requires a media library — ffmpeg / live555 / "
            "GStreamer / VLC are good choices");
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<RtspPlugin>());
}
#if !defined(_WIN32) && !defined(_WIN64)  // v1 resolves UltraNet_RegisterPlugin from the host at dlopen(); POSIX-only, Windows uses the v2 UltraNet_PluginInit vtable above
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<RtspPlugin>());
}
#endif
