// VideoFX/core/VideoFXProject.h
// Internal: VideoFX project files (.vfxproj) - a VideoFXProject to and from
// JSON, media paths relative to the file. Pure C++ (nlohmann/json, vendored),
// no FFmpeg, unit-tested; the public VideoFX_SaveProject & co. wrap it.
// Version: 0.7.0
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework
#pragma once

#include "VideoFX/VideoFX.h"

#include <string>
#include <vector>

namespace VideoFX {
namespace Internal {

// `baseDirectory`: what relative paths in the JSON are relative to ("" = none)
VideoFXResult ProjectToJsonText(const VideoFXProject& project, const std::string& baseDirectory, std::string& json,
                                std::string& error);
VideoFXResult ProjectFromJsonText(const std::string& json, const std::string& baseDirectory, VideoFXProject& project,
                                  std::string& error);

VideoFXResult SaveProjectFile(const VideoFXProject& project, const std::string& path, std::string& error);
VideoFXResult LoadProjectFile(const std::string& path, VideoFXProject& project, std::vector<std::string>* missingMedia,
                              std::string& error);

// Every media file a project refers to (clips, photos, songs, LUTs, overlay
// images, fonts), in order, without repeats
std::vector<std::string> ProjectMedia(const VideoFXProject& project);

} // namespace Internal
} // namespace VideoFX
