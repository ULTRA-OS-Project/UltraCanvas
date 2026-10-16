// Apps/UltraVideo/UltraVideoFormats.cpp
// FileLoader's lists, as the engine reads them (UltraVideoFormats.h).
// Version: 0.2.0 - the engine reads the picture types FFmpeg does not (HEIC,
//                  raw files) through the framework's image reader
// Version: 0.1.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework
#include "UltraVideoFormats.h"

#include "UltraCanvasImage.h"
#include "UltraCanvasMediaCodecRegistry.h"
#include "UltraCanvasPathUtf8.h"
#include "VideoFX/VideoFX.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

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

// What the engine says it handles, asked once and again after
// RegisterEngineFormats gave it the framework's picture reader: the FFmpeg
// it is linked against does not change while the program runs
std::mutex engineFormatsMutex;
std::shared_ptr<const std::vector<VideoFXFileFormat>> engineFormats;

std::shared_ptr<const std::vector<VideoFXFileFormat>> EngineFormats(bool refresh = false) {
    std::lock_guard<std::mutex> lock(engineFormatsMutex);
    if (!engineFormats || refresh)
        engineFormats = std::make_shared<const std::vector<VideoFXFileFormat>>(VideoFX_GetFileFormats());
    return engineFormats;
}

std::optional<VideoFXFileFormat> EngineFormat(const std::string& ext) {
    for (const VideoFXFileFormat& f : *EngineFormats())
        if (f.extension == ext || std::find(f.aliases.begin(), f.aliases.end(), ext) != f.aliases.end()) return f;
    return std::nullopt;
}

bool EngineReads(const std::string& ext, VideoFXFileKind kind) {
    const std::optional<VideoFXFileFormat> f = EngineFormat(ext);
    return f && f->kind == kind && f->canRead;
}

// ===== the framework's image reader, for the engine =====
// UCImageRaster (libvips, libheif ...) reads what FFmpeg does not open whole:
// a phone's HEIC, which is a grid of tiles libheif puts together and turns
// upright, a camera's raw file. Called on the engine's worker threads; each
// call reads its own image, nothing shared.

bool FrameworkPictureSize(const std::string& path, int& width, int& height) {
    auto image = UCImageRaster::Load(path, true);          // the header only
    if (!image || !image->IsValid()) return false;
    width = image->GetWidth();
    height = image->GetHeight();
    return width > 0 && height > 0;
}

bool FrameworkPictureDecode(const std::string& path, VideoFXFrame& frame) {
    auto image = UCImageRaster::Load(path, true);
    if (!image || !image->IsValid()) return false;
    auto pixmap = image->CreatePixmap(image->GetWidth(), image->GetHeight());
    if (!pixmap || !pixmap->IsValid()) return false;
    const int w = pixmap->GetWidth(), h = pixmap->GetHeight();
    const uint32_t* argb = pixmap->GetPixelData();          // premultiplied 0xAARRGGBB
    if (w <= 0 || h <= 0 || !argb) return false;
    frame.width = w;
    frame.height = h;
    frame.pixels.resize(static_cast<size_t>(w) * h * 4);
    uint8_t* out = frame.pixels.data();
    for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; ++i, out += 4) {
        const uint32_t p = argb[i];
        const unsigned a = p >> 24;
        unsigned r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
        if (a != 0 && a != 255) {
            r = std::min(255u, (r * 255 + a / 2) / a);
            g = std::min(255u, (g * 255 + a / 2) / a);
            b = std::min(255u, (b * 255 + a / 2) / a);
        }
        out[0] = static_cast<uint8_t>(r);
        out[1] = static_cast<uint8_t>(g);
        out[2] = static_cast<uint8_t>(b);
        out[3] = static_cast<uint8_t>(a);
    }
    return true;
}

void AppendRead(std::vector<std::string>& out, MediaFormatCategory category, VideoFXFileKind kind) {
    for (const std::string& ext : UltraCanvasFileLoader::GetSupportedLoadExtensions(category))
        if (EngineReads(ext, kind) && std::find(out.begin(), out.end(), ext) == out.end()) out.push_back(ext);
}

} // namespace

void RegisterEngineFormats() {
    static std::once_flag once;
    std::call_once(once, [] {
        // The picture types FileLoader reads and FFmpeg does not: the engine
        // reads them through the framework's image reader from now on
        VideoFXPictureReader reader;
        for (const std::string& ext : UltraCanvasFileLoader::GetSupportedLoadExtensions(MediaFormatCategory::Bitmap))
            if (!EngineReads(ext, VideoFXFileKind::Picture) &&
                std::find(reader.extensions.begin(), reader.extensions.end(), ext) == reader.extensions.end())
                reader.extensions.push_back(ext);
        reader.size = FrameworkPictureSize;
        reader.decode = FrameworkPictureDecode;
        VideoFX_SetPictureReader(std::move(reader));

        for (const VideoFXFileFormat& f : *EngineFormats(true)) {
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
    if (const std::optional<VideoFXFileFormat> f = EngineFormat(ext)) return f->kind == VideoFXFileKind::Picture;
    const std::vector<std::string> pictures = UltraCanvasFileLoader::GetSupportedLoadExtensions(MediaFormatCategory::Bitmap);
    return std::find(pictures.begin(), pictures.end(), ext) != pictures.end();
}

bool IsSoundPath(const std::string& path) {
    const std::string ext = ExtensionOf(path);
    if (ext.empty()) return false;
    if (const std::optional<VideoFXFileFormat> f = EngineFormat(ext)) return f->kind == VideoFXFileKind::Audio;
    return IsMediaFileOfKind(MediaCodecKind::Audio, path);
}

} // namespace UltraVideo
