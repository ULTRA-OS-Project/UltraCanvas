// Apps/UltraVideo/UltraVideoFormats.cpp
// FileLoader's lists, as the engine reads them (UltraVideoFormats.h).
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework
#include "UltraVideoFormats.h"

#include "UltraCanvasMediaCodecRegistry.h"
#include "UltraCanvasPathUtf8.h"
#include "VideoFX/VideoFX.h"

#include <algorithm>
#include <cctype>
#include <mutex>

using namespace UltraCanvas;
using namespace VideoFX;

namespace UltraVideo {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string ExtensionOf(const std::string& path) {
    std::string ext = PathToUtf8(PathFromUtf8(path).extension());
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    return Lower(ext);
}

// What the engine says it handles - asked once: the FFmpeg it is linked
// against does not change while the program runs
const std::vector<VideoFXFileFormat>& EngineFormats() {
    static const std::vector<VideoFXFileFormat> formats = VideoFX_GetFileFormats();
    return formats;
}

const VideoFXFileFormat* EngineFormat(const std::string& ext) {
    for (const VideoFXFileFormat& f : EngineFormats()) {
        if (f.extension == ext) return &f;
        if (std::find(f.aliases.begin(), f.aliases.end(), ext) != f.aliases.end()) return &f;
    }
    return nullptr;
}

bool EngineReads(const std::string& ext, VideoFXFileKind kind) {
    const VideoFXFileFormat* f = EngineFormat(ext);
    return f && f->kind == kind && f->canRead;
}

void AppendRead(std::vector<std::string>& out, MediaFormatCategory category, VideoFXFileKind kind) {
    for (const std::string& ext : UltraCanvasFileLoader::GetSupportedLoadExtensions(category))
        if (EngineReads(ext, kind) && std::find(out.begin(), out.end(), ext) == out.end()) out.push_back(ext);
}

} // namespace

void RegisterEngineFormats() {
    static std::once_flag once;
    std::call_once(once, [] {
        for (const VideoFXFileFormat& f : EngineFormats()) {
            if (f.kind == VideoFXFileKind::Picture || (!f.canRead && !f.canWrite)) continue;
            const MediaCodecKind kind = f.kind == VideoFXFileKind::Audio ? MediaCodecKind::Audio : MediaCodecKind::Video;
            if (auto known = FindMediaCodecByExtension(kind, f.extension))
                if ((known->canDecode || !f.canRead) && (known->canEncode || !f.canWrite)) continue;   // adds nothing
            MediaCodecRegistration codec;
            codec.extension = f.extension;
            codec.aliases = f.aliases;
            codec.description = f.description;
            codec.kind = kind;
            codec.canDecode = f.canRead;
            codec.canEncode = f.canWrite;
            codec.provider = "VideoFX (FFmpeg)";
            codec.notes = "UltraVideo's engine";
            RegisterMediaCodec(codec);
        }
    });
}

std::vector<std::string> ImportExtensions(bool films, bool pictures, bool sound) {
    std::vector<std::string> out;
    if (films) AppendRead(out, MediaFormatCategory::Video, VideoFXFileKind::Video);
    if (pictures) AppendRead(out, MediaFormatCategory::Bitmap, VideoFXFileKind::Picture);
    if (sound) AppendRead(out, MediaFormatCategory::Audio, VideoFXFileKind::Audio);
    return out;
}

void AddImportTypes(FileDialogOptions& opts, const std::string& label, bool films, bool pictures, bool sound) {
    const std::vector<std::string> extensions = ImportExtensions(films, pictures, sound);
    if (!extensions.empty()) opts.AddFilter(label, extensions);
}

std::string FormatName(const std::string& extension) {
    const std::string ext = Lower(extension.empty() || extension[0] != '.' ? extension : extension.substr(1));
    for (const MediaFormatInfo& f : UltraCanvasFileLoader::GetSupportedFormats())
        if (f.MatchesExtension(ext) && !f.description.empty()) return f.description;
    std::string upper = ext;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return upper;
}

// The engine's word first: it knows every file it handles, and asking it
// needs nothing initialised. FileLoader's lists cover what it does not know.
bool IsPicturePath(const std::string& path) {
    const std::string ext = ExtensionOf(path);
    if (ext.empty()) return false;
    if (const VideoFXFileFormat* f = EngineFormat(ext)) return f->kind == VideoFXFileKind::Picture;
    const std::vector<std::string> pictures = UltraCanvasFileLoader::GetSupportedLoadExtensions(MediaFormatCategory::Bitmap);
    return std::find(pictures.begin(), pictures.end(), ext) != pictures.end();
}

bool IsSoundPath(const std::string& path) {
    const std::string ext = ExtensionOf(path);
    if (ext.empty()) return false;
    if (const VideoFXFileFormat* f = EngineFormat(ext)) return f->kind == VideoFXFileKind::Audio;
    return IsMediaFileOfKind(MediaCodecKind::Audio, path);
}

} // namespace UltraVideo
