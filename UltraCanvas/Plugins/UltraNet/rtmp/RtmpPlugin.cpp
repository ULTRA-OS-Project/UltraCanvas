// UltraCanvas/Plugins/UltraNet/rtmp/RtmpPlugin.cpp
// RTMP plug-in. Implements IStreamingProtocolPlugin using libcurl's
// built-in RTMP support (linked against librtmp at libcurl build time;
// most distros ship libcurl with RTMP enabled — Ubuntu / brew / MSYS2
// all do).
//
// OpenStream(url):
//   - Issues a libcurl rtmp:// request in CONNECT_ONLY mode so we keep
//     the live stream socket and pull bytes via curl_easy_recv().
//   - The handle binds the CURL* easy plus an accumulator buffer to
//     ReadFrame.
//
// ReadFrame(handle, outFrame):
//   - Pulls one chunk of bytes from the live RTMP stream via
//     curl_easy_recv. The buffer is whatever libcurl had pending; the
//     caller is expected to be a media library that knows how to find
//     frame boundaries in the FLV/RTMP payload stream.
//
// For "true" frame-accurate demuxing, hand the bytes off to ffmpeg /
// GStreamer — same caveat as the RTSP plug-in.
// Version: 0.1.0
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

struct Session {
    UltraNetHandle handle = UltraNetInvalidHandle;
    CURL*          easy   = nullptr;
    std::string    url;

    ~Session() {
        if (easy) curl_easy_cleanup(easy);
    }
};

std::mutex g_sessionsMutex;
std::unordered_map<UltraNetHandle, std::shared_ptr<Session>> g_sessions;
std::atomic<UltraNetHandle> g_nextHandle{1};

std::shared_ptr<Session> Find(UltraNetHandle h) {
    std::lock_guard<std::mutex> lk(g_sessionsMutex);
    auto it = g_sessions.find(h);
    return it == g_sessions.end() ? nullptr : it->second;
}

bool LibcurlHasRtmp() {
    const curl_version_info_data* v = curl_version_info(CURLVERSION_NOW);
    if (!v || !v->protocols) return false;
    for (const char* const* p = v->protocols; *p; ++p) {
        if (std::strcmp(*p, "rtmp")  == 0 ||
            std::strcmp(*p, "rtmps") == 0) return true;
    }
    return false;
}

class RtmpPlugin : public IStreamingProtocolPlugin {
public:
    std::string GetName() const override { return "UltraNet-RTMP"; }
    std::string GetVersion() const override { return "0.1.0"; }
    std::vector<std::string> GetSupportedSchemes() const override {
        return {"rtmp", "rtmps", "rtmpe", "rtmpt", "rtmpte", "rtmpts"};
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
        if (!LibcurlHasRtmp()) return UltraNetInvalidHandle;
        if (url.empty()) return UltraNetInvalidHandle;
        // libcurl recognises the rtmp* schemes natively — no rewrite needed.

        auto s = std::make_shared<Session>();
        s->url  = url;
        s->easy = curl_easy_init();
        if (!s->easy) return UltraNetInvalidHandle;

        curl_easy_setopt(s->easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(s->easy, CURLOPT_CONNECT_ONLY, 1L);
        curl_easy_setopt(s->easy, CURLOPT_NOSIGNAL, 1L);
        if (options.connectTimeoutMs > 0) {
            curl_easy_setopt(s->easy, CURLOPT_CONNECTTIMEOUT_MS,
                             static_cast<long>(options.connectTimeoutMs));
        }

        if (curl_easy_perform(s->easy) != CURLE_OK) return UltraNetInvalidHandle;

        const UltraNetHandle h =
            g_nextHandle.fetch_add(1, std::memory_order_relaxed);
        s->handle = h;
        {
            std::lock_guard<std::mutex> lk(g_sessionsMutex);
            g_sessions[h] = s;
        }
        return h;
    }

    UltraNetResult ReadFrame(UltraNetHandle handle,
                             std::vector<uint8_t>& outFrame) override {
        outFrame.clear();
        auto s = Find(handle);
        if (!s) return UltraNetResult::Error(
            UltraNetResultCode::InvalidHandle, "no such RTMP session");

        constexpr std::size_t kChunk = 8 * 1024;
        std::vector<uint8_t> buf(kChunk);
        std::size_t got = 0;
        const CURLcode rc = curl_easy_recv(s->easy, buf.data(), buf.size(), &got);
        if (rc != CURLE_OK && rc != CURLE_AGAIN) {
            return UltraNetResult::Error(UltraNetResultCode::ReceiveFailed,
                                         curl_easy_strerror(rc));
        }
        if (got > 0) {
            buf.resize(got);
            outFrame = std::move(buf);
        }
        return UltraNetResult::Ok();
    }
};

} // namespace

extern "C" ULTRANET_PLUGIN_EXPORT
void UltraNet_PluginInit(const UltraNetPluginHost* host) {
    if (!host || host->abiVersion < 1 || !host->RegisterPlugin) return;
    host->RegisterPlugin(std::make_shared<RtmpPlugin>());
}
extern "C" void UltraNet_PluginRegister(void) {
    UltraNet_RegisterPlugin(std::make_shared<RtmpPlugin>());
}
