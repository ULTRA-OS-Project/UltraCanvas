// libspecific/Video/VideoCodecPlugin.cpp
// Side table of decoding callbacks for video codecs registered in the media
// codec registry (see VideoCodecPlugin.h). Always compiled: a plugin may bring
// a decoder to a build that has no platform video backend at all.
// Version: 0.1.0
// Last Modified: 2026-09-07
// Author: UltraCanvas Framework

#include "VideoCodecPlugin.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <vector>

namespace UltraCanvas {

namespace {

std::string NormalizeExtension(const std::string& ext) {
    std::string out = ext;
    if (!out.empty() && out[0] == '.') out.erase(0, 1);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

struct PluginEntry {
    std::string extension;              // the registry entry's canonical extension
    VideoDecoderFactory openDecoder;
    VideoThumbnailGrabber grabThumbnail;
};

struct Table {
    std::mutex mutex;
    std::vector<PluginEntry> entries;
};

Table& Store() {
    static Table table;
    return table;
}

// Resolve a source to the canonical extension of the registry entry claiming
// it, so the side table is keyed the same way the registry is: an alias or a
// content-probed extension finds the plugin its canonical entry registered.
std::string CanonicalExtensionFor(const std::string& source) {
    if (auto codec = FindMediaCodecForFile(MediaCodecKind::Video, source)) {
        return codec->extension;
    }
    return {};
}

} // namespace

void RegisterVideoCodecPlugin(const MediaCodecRegistration& codec,
                              VideoDecoderFactory openDecoder,
                              VideoThumbnailGrabber grabThumbnail) {
    MediaCodecRegistration registration = codec;
    registration.kind = MediaCodecKind::Video;
    // Supplying a decoder is the claim that it decodes; the two cannot disagree.
    if (openDecoder) registration.canDecode = true;
    RegisterMediaCodec(registration);

    const std::string ext = NormalizeExtension(registration.extension);
    if (ext.empty()) return;

    std::lock_guard<std::mutex> lock(Store().mutex);
    auto& entries = Store().entries;
    for (PluginEntry& e : entries) {
        if (e.extension != ext) continue;
        // Mirror the registry's upgrade rule: a later registration wins where
        // it brings something, and never silently drops what was there.
        if (openDecoder)   e.openDecoder = std::move(openDecoder);
        if (grabThumbnail) e.grabThumbnail = std::move(grabThumbnail);
        return;
    }
    entries.push_back({ ext, std::move(openDecoder), std::move(grabThumbnail) });
}

void UnregisterVideoCodecPlugin(const std::string& extension) {
    const std::string ext = NormalizeExtension(extension);
    if (ext.empty()) return;
    std::lock_guard<std::mutex> lock(Store().mutex);
    auto& entries = Store().entries;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const PluginEntry& e) { return e.extension == ext; }),
                  entries.end());
}

VideoDecoderFactory FindVideoDecoderFor(const std::string& source) {
    const std::string ext = CanonicalExtensionFor(source);
    if (ext.empty()) return {};
    std::lock_guard<std::mutex> lock(Store().mutex);
    for (const PluginEntry& e : Store().entries) {
        if (e.extension == ext) return e.openDecoder;
    }
    return {};
}

VideoThumbnailGrabber FindVideoThumbnailGrabberFor(const std::string& source) {
    const std::string ext = CanonicalExtensionFor(source);
    if (ext.empty()) return {};
    std::lock_guard<std::mutex> lock(Store().mutex);
    for (const PluginEntry& e : Store().entries) {
        if (e.extension == ext) return e.grabThumbnail;
    }
    return {};
}

} // namespace UltraCanvas
