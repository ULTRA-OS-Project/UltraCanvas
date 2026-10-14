// VideoFX/core/VideoFXProxy.cpp
// Proxy media: see VideoFXProxy.h. Built in every configuration; making a
// proxy goes through VideoFX_Probe / VideoFX_Export, which the stub answers
// with NotAvailable.
// Version: 0.12.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "VideoFXProxy.h"
#include "VideoFXPlatform.h"

#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"      // PathFromUtf8, PathToUtf8

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <random>
#include <set>
#include <system_error>

namespace VideoFX {
namespace Internal {

namespace {

namespace fs = std::filesystem;

constexpr const char* kProxyTag = "vfxproxy1";     // part of every name: a new proxy recipe, new names
constexpr size_t kMaxStem = 40;

uint64_t Fnv1a64(const std::string& text) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string Hex(uint64_t value, int digits) {
    static const char* kDigits = "0123456789abcdef";
    std::string s(static_cast<size_t>(digits), '0');
    for (int i = digits - 1; i >= 0; --i) {
        s[static_cast<size_t>(i)] = kDigits[value & 0xF];
        value >>= 4;
    }
    return s;
}

bool IsHex(const std::string& s, size_t from, size_t count) {
    if (from + count > s.size()) return false;
    for (size_t i = from; i < from + count; ++i) {
        const char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool EndsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// The clip's picture size as displayed (rotation applied); false without one
bool DisplaySize(const VideoFXMediaInfo& info, int& w, int& h) {
    w = info.width;
    h = info.height;
    return info.HasVideo() && w > 0 && h > 0;
}

bool HasCrop(const VideoFXSegment& seg) {
    return std::any_of(seg.effects.begin(), seg.effects.end(),
                       [](const VideoFXEffect& e) { return e.type == VideoFXEffectType::Crop; });
}

std::string RandomHex8() {
    std::random_device rd;
    return Hex((static_cast<uint64_t>(rd()) << 16) ^ rd(), 8);
}

} // namespace

std::string DefaultProxyDirectory() {
    const std::string cache = UserCacheDir();
    if (cache.empty()) return "";
    return UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(cache) / "VideoFX" / "Proxies");
}

std::string ProxyDirectory(const VideoFXProxySettings& settings) {
    return settings.cacheDirectory.empty() ? DefaultProxyDirectory() : settings.cacheDirectory;
}

bool ValidateProxySettings(const VideoFXProxySettings& settings, std::string& error) {
    if (settings.height < 16 || settings.height > 4320) {
        error = "Proxy height must be 16..4320";
        return false;
    }
    if (settings.quality < 0 || settings.quality > 100) {
        error = "Proxy quality must be 0..100";
        return false;
    }
    if (!(settings.keyframeInterval >= 0.0) || settings.keyframeInterval > 60.0) {
        error = "Proxy keyframe interval must be 0..60 seconds";
        return false;
    }
    return true;
}

std::string ProxyFileName(const std::string& absoluteClip, uint64_t size, int64_t modified,
                          const VideoFXProxySettings& settings) {
    // Name part: the clip's file name without its extension, readable in a
    // file manager; anything but ASCII letters and digits becomes one '_'
    // (UTF-8 bytes included - the hash, not the stem, tells clips apart)
    std::string base = absoluteClip;
    const size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    const size_t dot = base.rfind('.');
    if (dot != std::string::npos && dot > 0) base.resize(dot);
    std::string stem;
    for (unsigned char c : base) {
        const bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                          c == '-' || c == '_';
        if (keep) stem += static_cast<char>(c);
        else if (stem.empty() || stem.back() != '_') stem += '_';
        if (stem.size() >= kMaxStem) break;
    }
    while (!stem.empty() && stem.back() == '_') stem.pop_back();
    while (!stem.empty() && stem.front() == '_') stem.erase(stem.begin());
    if (stem.empty()) stem = "clip";

    // Integers only in the key: no locale can change it
    const std::string key = std::string(kProxyTag) + "\n" + absoluteClip + "\n" + std::to_string(size) + "\n" +
                            std::to_string(modified) + "\n" + std::to_string(settings.height) + "\n" +
                            std::to_string(settings.quality) + "\n" +
                            std::to_string(std::llround(settings.keyframeInterval * 1000.0));
    return stem + "-" + Hex(Fnv1a64(key), 16) + ".mp4";
}

std::string ProxyPathFor(const std::string& clip, const VideoFXProxySettings& settings) {
    if (clip.empty()) return "";
    std::error_code ec;
    const fs::path p = fs::absolute(UltraCanvas::PathFromUtf8(clip), ec).lexically_normal();
    if (ec || !fs::is_regular_file(p, ec)) return "";
    const uint64_t size = fs::file_size(p, ec);
    if (ec) return "";
    const auto modified = fs::last_write_time(p, ec);
    if (ec) return "";
    const std::string dir = ProxyDirectory(settings);
    if (dir.empty()) return "";
    const std::string name = ProxyFileName(UltraCanvas::PathToUtf8(p), size,
                                           static_cast<int64_t>(modified.time_since_epoch().count()), settings);
    return UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(dir) / UltraCanvas::PathFromUtf8(name));
}

bool IsProxyFileName(const std::string& name) {
    // "<stem>-<16 hex>.mp4"  or  "<stem>-<16 hex>.<8 hex>.part.mp4"
    std::string rest = name;
    if (!EndsWith(rest, ".mp4")) return false;
    rest.resize(rest.size() - 4);
    if (EndsWith(rest, ".part")) {
        rest.resize(rest.size() - 5);
        if (rest.size() < 9 || rest[rest.size() - 9] != '.' || !IsHex(rest, rest.size() - 8, 8)) return false;
        rest.resize(rest.size() - 9);
    }
    return rest.size() >= 18 && rest[rest.size() - 17] == '-' && IsHex(rest, rest.size() - 16, 16);
}

std::vector<std::string> ProxyableClips(const std::vector<VideoFXSegment>& segments) {
    std::vector<std::string> clips;
    std::set<std::string> seen;
    auto add = [&](const std::string& path) {
        if (!path.empty() && seen.insert(path).second) clips.push_back(path);
    };
    for (const VideoFXSegment& seg : segments) {
        if (seg.kind == VideoFXSourceKind::File) add(seg.path);
        for (const VideoFXOverlay& o : seg.overlays) {
            if (o.kind == VideoFXOverlayKind::Video && o.imageHeight > 0.0) add(o.videoPath);
        }
    }
    return clips;
}

VideoFXEffect ScaleCrop(const VideoFXEffect& crop, double scaleX, double scaleY, int proxyWidth, int proxyHeight) {
    VideoFXEffect e = crop;
    // The edges move, not the size: the region covers the same picture
    const int x0 = std::clamp(static_cast<int>(std::lround(crop.x * scaleX)), 0, std::max(0, proxyWidth - 1));
    const int y0 = std::clamp(static_cast<int>(std::lround(crop.y * scaleY)), 0, std::max(0, proxyHeight - 1));
    const int x1 = std::clamp(static_cast<int>(std::lround((crop.x + crop.width) * scaleX)), x0 + 1, proxyWidth);
    const int y1 = std::clamp(static_cast<int>(std::lround((crop.y + crop.height) * scaleY)), y0 + 1, proxyHeight);
    e.x = x0;
    e.y = y0;
    e.width = x1 - x0;
    e.height = y1 - y0;
    return e;
}

VideoFXResult CreateProxy(const std::string& clip, const VideoFXProxySettings& settings,
                          const VideoFXProgressCallback& progress, std::string* editPath, std::string& error) {
    if (!ValidateProxySettings(settings, error)) return VideoFXResult::InvalidArgument;
    const std::string proxy = ProxyPathFor(clip, settings);
    if (proxy.empty()) {
        std::error_code ec;
        if (!fs::is_regular_file(UltraCanvas::PathFromUtf8(clip), ec)) {
            error = "No such file: " + clip;
            return VideoFXResult::FileNotFound;
        }
        error = "No cache folder for proxies: set VideoFXProxySettings::cacheDirectory";
        return VideoFXResult::WriteError;
    }

    VideoFXMediaInfo info;
    VideoFXResult r = VideoFX_Probe(clip, info);
    if (r != VideoFXResult::Ok) {
        error = VideoFX_GetLastError();
        return r;
    }
    int w = 0, h = 0;
    if (!DisplaySize(info, w, h) || h <= settings.height) {
        // No picture, or no bigger than a proxy: the clip is its own stand-in
        if (editPath) *editPath = clip;
        if (progress) progress(1.0);
        return VideoFXResult::Ok;
    }

    std::error_code ec;
    const fs::path target = UltraCanvas::PathFromUtf8(proxy);
    if (fs::is_regular_file(target, ec)) {
        if (editPath) *editPath = proxy;
        if (progress) progress(1.0);
        return VideoFXResult::Ok;
    }
    fs::create_directories(target.parent_path(), ec);
    if (ec) {
        error = "Cannot create the proxy folder " + UltraCanvas::PathToUtf8(target.parent_path());
        return VideoFXResult::WriteError;
    }

    // Written aside and renamed when whole: what exists under the proxy's
    // name is always a complete proxy
    std::string partName = UltraCanvas::PathToUtf8(target.filename());
    partName.resize(partName.size() - 4);                                   // ".mp4"
    const fs::path part = target.parent_path() / UltraCanvas::PathFromUtf8(partName + "." + RandomHex8() + ".part.mp4");

    VideoFXExportSettings s;
    s.container = VideoFXContainer::MP4;
    s.videoCodec = VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec::H264) ? VideoFXVideoCodec::H264
                                                                            : VideoFXVideoCodec::MPEG4;
    s.height = settings.height;                         // width follows the clip's shape
    s.quality = settings.quality;
    s.encoderPreset = "veryfast";
    s.keyframeInterval = settings.keyframeInterval;
    s.acceleration = settings.acceleration;
    s.audioBitRate = 128000;
    r = VideoFX_Export({VideoFXSegment::FromFile(clip)}, UltraCanvas::PathToUtf8(part), s, progress);
    if (r != VideoFXResult::Ok) {
        error = VideoFX_GetLastError();
        fs::remove(part, ec);
        return r;
    }
    fs::rename(part, target, ec);
    if (ec) {
        // Windows will not rename over an existing file: another job may have
        // finished the same proxy meanwhile, which is as good
        std::error_code ec2;
        if (fs::is_regular_file(target, ec2)) {
            fs::remove(part, ec2);
        } else {
            fs::remove(part, ec2);
            error = "Cannot move the finished proxy into place: " + ec.message();
            return VideoFXResult::WriteError;
        }
    }
    if (editPath) *editPath = proxy;
    return VideoFXResult::Ok;
}

VideoFXResult CreateProxies(const std::vector<VideoFXSegment>& segments, const VideoFXProxySettings& settings,
                            const VideoFXProgressCallback& progress, std::string& error) {
    if (!ValidateProxySettings(settings, error)) return VideoFXResult::InvalidArgument;
    const std::vector<std::string> clips = ProxyableClips(segments);

    // Weighted by length, so a long clip moves the bar as much as it takes
    std::vector<double> weight(clips.size(), 1.0);
    double total = 0.0;
    for (size_t i = 0; i < clips.size(); ++i) {
        VideoFXMediaInfo info;
        const VideoFXResult r = VideoFX_Probe(clips[i], info);
        if (r != VideoFXResult::Ok) {
            error = VideoFX_GetLastError();
            return r;
        }
        if (info.duration > 0.0) weight[i] = info.duration;
        total += weight[i];
    }
    double done = 0.0;
    for (size_t i = 0; i < clips.size(); ++i) {
        const double base = done, share = weight[i];
        VideoFXProgressCallback step;
        if (progress) {
            step = [&progress, base, share, total](double f) { return progress((base + share * f) / total); };
        }
        const VideoFXResult r = CreateProxy(clips[i], settings, step, nullptr, error);
        if (r != VideoFXResult::Ok) return r;
        done += share;
    }
    if (progress) progress(1.0);
    return VideoFXResult::Ok;
}

std::vector<VideoFXSegment> UseProxies(const std::vector<VideoFXSegment>& segments,
                                       const VideoFXProxySettings& settings, int* replaced) {
    std::vector<VideoFXSegment> out = segments;
    int count = 0;
    std::map<std::string, std::string> proxyOf;         // clip -> its proxy, "" = none
    auto lookUp = [&](const std::string& clip) -> const std::string& {
        auto it = proxyOf.find(clip);
        if (it != proxyOf.end()) return it->second;
        std::string proxy = ProxyPathFor(clip, settings);
        std::error_code ec;
        if (!proxy.empty() && !fs::is_regular_file(UltraCanvas::PathFromUtf8(proxy), ec)) proxy.clear();
        return proxyOf.emplace(clip, std::move(proxy)).first->second;
    };

    for (VideoFXSegment& seg : out) {
        if (seg.kind == VideoFXSourceKind::File) {
            const std::string& proxy = lookUp(seg.path);
            bool use = !proxy.empty();
            if (use && HasCrop(seg)) {
                // A crop is in the clip's pixels: the proxy needs the same
                // region in its own. Without both sizes the clip stays.
                VideoFXMediaInfo a, b;
                int aw = 0, ah = 0, bw = 0, bh = 0;
                use = VideoFX_Probe(seg.path, a) == VideoFXResult::Ok && VideoFX_Probe(proxy, b) == VideoFXResult::Ok &&
                      DisplaySize(a, aw, ah) && DisplaySize(b, bw, bh);
                if (use) {
                    const double sx = static_cast<double>(bw) / aw, sy = static_cast<double>(bh) / ah;
                    for (VideoFXEffect& e : seg.effects) {
                        if (e.type == VideoFXEffectType::Crop) e = ScaleCrop(e, sx, sy, bw, bh);
                    }
                }
            }
            if (use) {
                seg.path = proxy;
                ++count;
            }
        }
        for (VideoFXOverlay& o : seg.overlays) {
            if (o.kind != VideoFXOverlayKind::Video || o.imageHeight <= 0.0) continue;
            const std::string& proxy = lookUp(o.videoPath);
            if (!proxy.empty()) {
                o.videoPath = proxy;
                ++count;
            }
        }
    }
    if (replaced) *replaced = count;
    return out;
}

VideoFXResult ClearProxies(const VideoFXProxySettings& settings, int* filesRemoved, uint64_t* bytesFreed,
                           std::string& error) {
    int files = 0;
    uint64_t bytes = 0;
    if (filesRemoved) *filesRemoved = 0;
    if (bytesFreed) *bytesFreed = 0;
    const std::string dir = ProxyDirectory(settings);
    if (dir.empty()) return VideoFXResult::Ok;          // no cache folder: nothing was ever made
    std::error_code ec;
    const fs::path folder = UltraCanvas::PathFromUtf8(dir);
    if (!fs::is_directory(folder, ec)) return VideoFXResult::Ok;

    std::vector<fs::path> victims;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (it->is_regular_file(fec) && IsProxyFileName(UltraCanvas::PathToUtf8(it->path().filename()))) {
            victims.push_back(it->path());
        }
    }
    if (ec) {
        error = "Cannot list the proxy folder " + dir + ": " + ec.message();
        return VideoFXResult::OpenFailed;
    }
    std::string failed;
    for (const fs::path& p : victims) {
        std::error_code fec;
        const uint64_t size = fs::file_size(p, fec);
        if (fs::remove(p, fec) && !fec) {
            ++files;
            bytes += size;
        } else if (failed.empty()) {
            failed = UltraCanvas::PathToUtf8(p);
        }
    }
    if (filesRemoved) *filesRemoved = files;
    if (bytesFreed) *bytesFreed = bytes;
    if (!failed.empty()) {
        error = "Cannot delete the proxy " + failed;
        return VideoFXResult::WriteError;
    }
    return VideoFXResult::Ok;
}

} // namespace Internal
} // namespace VideoFX
