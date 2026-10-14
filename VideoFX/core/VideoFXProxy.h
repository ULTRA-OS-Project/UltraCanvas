// VideoFX/core/VideoFXProxy.h
// Internal: proxy media - small stand-ins for large clips while editing.
// Where a proxy lives and what it is called, which clips a timeline plays,
// a timeline switched to its proxies, and making them through the public
// VideoFX_Probe / VideoFX_Export (so the same code serves the FFmpeg build
// and the stub, which simply cannot make any). The public VideoFX_*Prox*
// calls wrap these and set the last error.
//
//   clip  /videos/Trip 2026.mov  (4K, 1.2 GB)
//   proxy <cache>/VideoFX/Proxies/Trip_2026-3f9a0c41d27be805.mp4  (540p, H.264,
//         a keyframe every 0.5 s)
//         the hex is FNV-1a of the clip's absolute path, size and modification
//         time and the proxy settings: a changed clip or setting, a new name
// Version: 0.11.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include "VideoFX/VideoFX.h"

#include <cstdint>
#include <string>
#include <vector>

namespace VideoFX {
namespace Internal {

// <UserCacheDir()>/VideoFX/Proxies, "" when the system names no cache folder
std::string DefaultProxyDirectory();

// settings.cacheDirectory, or the default
std::string ProxyDirectory(const VideoFXProxySettings& settings);

bool ValidateProxySettings(const VideoFXProxySettings& settings, std::string& error);

// The proxy's file name for a clip known by these facts - pure, so it is
// tested without files: "<stem>-<16 hex>.mp4", the stem the clip's own name
// cut to ASCII letters, digits, '-' and '_' (at most 40)
std::string ProxyFileName(const std::string& absoluteClip, uint64_t size, int64_t modified,
                          const VideoFXProxySettings& settings);

// The proxy path of a clip on disk, "" when it does not exist
std::string ProxyPathFor(const std::string& clip, const VideoFXProxySettings& settings);

// Whether a file name is one ProxyFileName makes, or a half-made one of its
// ("<name>.<8 hex>.part.mp4") - what VideoFX_ClearProxies may delete
bool IsProxyFileName(const std::string& name);

// Every clip a timeline plays that a proxy can stand in for, each once, in
// order: file segments, and picture-in-picture videos with a set height
std::vector<std::string> ProxyableClips(const std::vector<VideoFXSegment>& segments);

// A Crop effect in the clip's pixels as the same region of the proxy, whose
// picture is `scaleX` / `scaleY` the clip's and `proxyWidth` x `proxyHeight`
VideoFXEffect ScaleCrop(const VideoFXEffect& crop, double scaleX, double scaleY, int proxyWidth, int proxyHeight);

VideoFXResult CreateProxy(const std::string& clip, const VideoFXProxySettings& settings,
                          const VideoFXProgressCallback& progress, std::string* editPath, std::string& error);
VideoFXResult CreateProxies(const std::vector<VideoFXSegment>& segments, const VideoFXProxySettings& settings,
                            const VideoFXProgressCallback& progress, std::string& error);
std::vector<VideoFXSegment> UseProxies(const std::vector<VideoFXSegment>& segments,
                                       const VideoFXProxySettings& settings, int* replaced);
VideoFXResult ClearProxies(const VideoFXProxySettings& settings, int* filesRemoved, uint64_t* bytesFreed,
                           std::string& error);

} // namespace Internal
} // namespace VideoFX
