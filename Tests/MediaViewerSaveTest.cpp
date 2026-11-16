// Tests/MediaViewerSaveTest.cpp
// The media viewer's Save image as at another size and with the type's save
// settings (UltraCanvasMediaSurface::GetSavePicture / SaveProcessed).
//
// The viewer never told the Save dialog how big its picture was, so a bitmap
// type's Settings had no Size row there, and it wrote every file with the
// writer's defaults whatever the Settings held. An SVG has no pixel size of
// its own at all: it is a drawing, and one written larger must be drawn again
// at that size - sharp - not the picture read at its page size stretched.
//
// A recording is saved in another audio type with FileLoader's
// ConvertAudioFile: the same type copied as it is, another encoded.
//
// A video is saved in another type with FileLoader's ConvertVideoFile: the
// same type copied as it is, another by the converter a module registered
// (MediaCodecRegistration::convertVideo) - written beside the target and put
// in place when done, so a stopped or failed conversion leaves no half file
// and keeps the one it was to replace. A converter of the test's own runs
// that in every build; where VideoFX is built (VideoFXUltraCanvasFormats),
// its converter turns a clip it generates into each type it writes.
//
// Runs headless: the surface loads and saves without a window. The save
// settings go to a scratch settings folder, never the real one.
// Version: 1.1.0 - a video saved in another type (ConvertVideoFile)
// Version: 1.0.0
// Last Modified: 2026-11-15
// Author: UltraCanvas Framework

#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasFileSaveSettings.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasAudio.h"
#include "UltraCanvasMediaCodecRegistry.h"

#ifdef MEDIAVIEWER_SAVE_TEST_WITH_VIDEOFX
#include "VideoFXUltraCanvasFormats.h"
#include "VideoFX/VideoFX.h"
#endif

#include <cairo/cairo.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// The UltraCanvas settings folder moved into `root`, so the stored save
// settings never touch a real installation's SaveSettings.conf.
void RedirectSettingsFolder(const fs::path& root) {
    const std::string path = PathToUtf8(root);
#if defined(_WIN32) || defined(_WIN64)
    _putenv_s("APPDATA", path.c_str());
#elif defined(__APPLE__)
    setenv("HOME", path.c_str(), 1);
#else
    setenv("XDG_CONFIG_HOME", path.c_str(), 1);
#endif
}

// A w x h picture: a horizontal grey ramp with a red diagonal, so a JPEG of
// it has detail for its quality to keep or lose.
bool WritePng(const fs::path& path, int w, int h) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create(s);
    cairo_pattern_t* ramp = cairo_pattern_create_linear(0, 0, w, 0);
    cairo_pattern_add_color_stop_rgb(ramp, 0, 0, 0, 0);
    cairo_pattern_add_color_stop_rgb(ramp, 1, 1, 1, 1);
    cairo_set_source(cr, ramp);
    cairo_paint(cr);
    cairo_pattern_destroy(ramp);
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, w, h);
    cairo_stroke(cr);
    cairo_destroy(cr);
    const bool ok = cairo_surface_write_to_png(s, PathToUtf8(path).c_str()) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(s);
    return ok;
}

struct Png {
    int width = 0, height = 0;
    cairo_surface_t* surface = nullptr;
    explicit Png(const fs::path& path) {
        surface = cairo_image_surface_create_from_png(PathToUtf8(path).c_str());
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) return;
        width = cairo_image_surface_get_width(surface);
        height = cairo_image_surface_get_height(surface);
    }
    ~Png() { if (surface) cairo_surface_destroy(surface); }
    // The grey level at (x, y), 0 black .. 255 white, over white.
    int Grey(int x, int y) const {
        const unsigned char* data = cairo_image_surface_get_data(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        const auto px = *reinterpret_cast<const uint32_t*>(data + y * stride + x * 4);
        const int g = static_cast<int>((px >> 8) & 0xFF);   // premultiplied
        if (cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32) return g;
        const int a = static_cast<int>((px >> 24) & 0xFF);
        return g + (255 - a);
    }
};

std::shared_ptr<UltraCanvasMediaSurface> Showing(const fs::path& file) {
    auto surface = std::make_shared<UltraCanvasMediaSurface>("surface");
    surface->ShowImage(UCImage::GetFresh(PathToUtf8(file)), MediaTransition::NoTransition, 0, false);
    return surface;
}

std::string ReadAll(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void WriteAll(const fs::path& path, const std::string& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << bytes;
}

// Files in `dir` other than `keep` - what a conversion left lying about.
std::vector<std::string> Leftovers(const fs::path& dir, const std::vector<std::string>& keep) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const std::string name = PathToUtf8(entry.path().filename());
        bool kept = false;
        for (const std::string& k : keep) kept = kept || k == name;
        if (!kept) out.push_back(name);
    }
    return out;
}

FileSaveImageSize Size(int w, int h, bool keep = true) {
    FileSaveImageSize size;
    size.width = w;
    size.height = h;
    size.keepProportions = keep;
    return size;
}

} // namespace

int main() {
    std::cout << "MediaViewerSaveTest" << std::endl;
    // An application does this on start; a headless test has to.
    UCImage::InitializeImageSubsysterm("MediaViewerSaveTest");
#ifndef HAS_LIBVIPS
    std::cout << "SKIP: saving needs libvips\n";
    return 0;
#else
    const fs::path scratch = fs::temp_directory_path() / "uc-mediaviewer-save-test";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch / "config");
    RedirectSettingsFolder(scratch / "config");

    // ----- A bitmap -----
    const fs::path photo = scratch / "photo.png";
    Check(WritePng(photo, 40, 20), "a 40 x 20 picture to show");
    auto surface = Showing(photo);
    FileSavePicture picture = surface->GetSavePicture();
    Check(picture.width == 40 && picture.height == 20 && !picture.drawing,
          "A bitmap is offered at its own size, as a bitmap");
    surface->RotateBy(1);
    picture = surface->GetSavePicture();
    Check(picture.width == 20 && picture.height == 40, "... a quarter turn swaps its sides");
    surface->RotateBy(-1);

    std::string error;
    const fs::path bigger = scratch / "bigger.png";
    Check(surface->SaveProcessed(PathToUtf8(bigger), Size(80, 40), error), "Saved at 80 x 40: " + error);
    {
        Png out(bigger);
        Check(out.width == 80 && out.height == 40, "... the file is 80 x 40");
    }
    const fs::path squashed = scratch / "squashed.png";
    Check(surface->SaveProcessed(PathToUtf8(squashed), Size(30, 30, false), error), "Saved at 30 x 30");
    {
        Png out(squashed);
        Check(out.width == 30 && out.height == 30, "... in another shape when proportions are free");
    }
    const fs::path same = scratch / "same.png";
    Check(surface->SaveProcessed(PathToUtf8(same), FileSaveImageSize(), error), "Saved at its own size");
    {
        Png out(same);
        Check(out.width == 40 && out.height == 20, "... the file is the picture's size");
    }

    // ----- FileLoader's types: an icon and a QOI too -----
    const fs::path icon = scratch / "photo.ico", quite = scratch / "photo.qoi";
    Check(surface->SaveProcessed(PathToUtf8(icon), FileSaveImageSize(), error) && fs::file_size(icon, ec) > 22,
          "Saved as an icon, a type FileLoader offers: " + error);
    Check(surface->SaveProcessed(PathToUtf8(quite), FileSaveImageSize(), error) && fs::file_size(quite, ec) > 14,
          "... and as QOI: " + error);

    // ----- The type's save settings -----
    auto storeQuality = [](int q) {
        FileSaveSettings settings = UltraCanvasFileLoader::GetSaveSettings("jpg");
        settings.SetInt("quality", q);
        return UltraCanvasFileLoader::SetSaveSettings(settings);
    };
    const fs::path big = scratch / "big.png";
    WritePng(big, 400, 300);
    auto large = Showing(big);
    const fs::path poor = scratch / "poor.jpg", fine = scratch / "fine.jpg";
    Check(storeQuality(5), "JPEG quality 5 stored");
    Check(large->SaveProcessed(PathToUtf8(poor), error), "Saved as JPEG at quality 5: " + error);
    Check(storeQuality(98), "JPEG quality 98 stored");
    Check(large->SaveProcessed(PathToUtf8(fine), error), "Saved as JPEG at quality 98");
    const auto poorBytes = fs::file_size(poor, ec), fineBytes = fs::file_size(fine, ec);
    std::cout << "         quality 5: " << poorBytes << " bytes, quality 98: " << fineBytes << " bytes\n";
    Check(poorBytes > 0 && fineBytes > poorBytes * 2, "The stored JPEG quality is what the file is written at");
    UltraCanvasFileLoader::ResetSaveSettings("jpg");

    // ----- A drawing -----
    // 20 x 10 on the page, its left half black: drawn again at 400 x 200 the
    // edge in the middle is sharp; the 20 x 10 picture stretched would fade
    // across many pixels.
    const fs::path svg = scratch / "drawing.svg";
    {
        std::ofstream out(svg, std::ios::binary);
        out << "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='10'>"
               "<rect x='0' y='0' width='10' height='10' fill='black'/></svg>";
    }
    auto drawing = Showing(svg);
    picture = drawing->GetSavePicture();
    std::cout << "         drawing offered at " << picture.width << " x " << picture.height << "\n";
    if (!drawing->GetImage() || !drawing->GetImage()->IsValid()) {
        std::cout << "  SKIP: this build does not read SVG\n";
    } else {
        Check(picture.drawing, "An SVG is offered as a drawing");
        Check(picture.width > 0 && picture.height == picture.width / 2, "... at its size on the page");
        const fs::path drawn = scratch / "drawn.png";
        Check(drawing->SaveProcessed(PathToUtf8(drawn), Size(400, 200), error), "Drawing saved at 400 x 200: " + error);
        Png out(drawn);
        Check(out.width == 400 && out.height == 200, "... the file is 400 x 200");
        if (out.width == 400 && out.height == 200) {
            int between = 0;
            for (int x = 0; x < 400; ++x) {
                const int g = out.Grey(x, 100);
                if (g > 30 && g < 225) ++between;
            }
            std::cout << "         pixels between black and white across the edge: " << between << "\n";
            Check(between <= 3, "... drawn again at that size: its edge is sharp, not stretched");
            Check(out.Grey(100, 100) < 30 && out.Grey(300, 100) > 225, "... black on the left, clear on the right");
        }
    }

    // ----- A recording -----
    // Save as for a sound is FileLoader's ConvertAudioFile: the same type is
    // the file copied as it is, another is decoded and encoded, and a type
    // this build does not encode is refused with a reason.
    {
        std::cout << "\nA recording\n";
        AudioBufferInfo info;
        info.sampleRate = 22050;
        info.channels = 1;
        info.sampleType = AudioSampleType::PCM_S16;
        info.frameCount = 2205;   // 0.1 s
        info.durationSeconds = 0.1;
        std::vector<int16_t> samples(info.frameCount);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<int16_t>(((i / 25) % 2) ? 8000 : -8000);
        auto tone = UCAudio::FromRawPCM(samples.data(), samples.size() * sizeof(int16_t), info);
        const fs::path wav = scratch / "tone.wav";
        std::string audioError;
        if (!tone || !UltraCanvasFileLoader::SaveAudio(*tone, PathToUtf8(wav), audioError)) {
            std::cout << "  SKIP: this build writes no audio (" << audioError << ")\n";
        } else {
            Check(fs::file_size(wav) > 4000, "SaveAudio wrote the WAV");
            const fs::path copy = scratch / "copy.wav";
            Check(UltraCanvasFileLoader::ConvertAudioFile(PathToUtf8(wav), PathToUtf8(copy), audioError),
                  "Saved as the same type: " + audioError);
            Check(fs::exists(copy) && fs::file_size(copy) == fs::file_size(wav), "... the file copied as it is");
            const fs::path bad = scratch / "tone.xyz";
            Check(!UltraCanvasFileLoader::ConvertAudioFile(PathToUtf8(wav), PathToUtf8(bad), audioError) &&
                  !audioError.empty() && !fs::exists(bad),
                  "A type nothing encodes is refused, with a reason (" + audioError + ")");
            for (const std::string ext : { "flac", "ogg", "mp3", "opus" }) {
                if (!CanEncodeMediaExtension(MediaCodecKind::Audio, ext)) {
                    std::cout << "         (no ." << ext << " encoder in this build)\n";
                    continue;
                }
                const fs::path out = scratch / PathFromUtf8("tone." + ext);
                const bool ok = UltraCanvasFileLoader::ConvertAudioFile(PathToUtf8(wav), PathToUtf8(out), audioError);
                Check(ok && fs::exists(out) && fs::file_size(out) > 0, "Saved as ." + ext + ": " + audioError);
                if (ok && CanDecodeMediaFile(MediaCodecKind::Audio, PathToUtf8(out))) {
                    auto back = UCAudio::LoadFromFile(PathToUtf8(out));
                    Check(back && back->IsValid() && back->GetDuration() > 0.05,
                          "... and it reads back as sound");
                }
            }
        }
    }

    // ----- A video -----
    // Save as for a film is FileLoader's ConvertVideoFile. The core encodes
    // no video: a type is written by the converter its registration carries.
    // This one "converts" by writing a marker and the source after it, and
    // reports its progress in four steps - or stops, fails, or ignores a stop
    // when told to.
    {
        std::cout << "\nA video\n";
        const fs::path films = scratch / "films";
        fs::create_directories(films);
        const fs::path film = films / "clip.mp4";
        WriteAll(film, std::string("not really a film, but bytes to keep"));

        struct Behaviour {
            enum Mode { Convert, Fail, IgnoreStop } mode = Convert;
            std::string wroteTo;
            std::vector<double> reported;
        };
        auto behaviour = std::make_shared<Behaviour>();
        MediaCodecRegistration codec;
        codec.extension = "ucvout";
        codec.aliases = { "ucvo" };
        codec.description = "Test video type";
        codec.kind = MediaCodecKind::Video;
        codec.provider = "MediaViewerSaveTest";
        codec.convertVideo = [behaviour](const std::string& source, const std::string& target,
                                         const MediaProgressFn& progress, std::string& error) {
            behaviour->wroteTo = target;
            WriteAll(PathFromUtf8(target), "converted:" + ReadAll(PathFromUtf8(source)));
            for (double f : { 0.25, 0.5, 0.75, 1.0 }) {
                behaviour->reported.push_back(f);
                if (progress && !progress(f) && behaviour->mode != Behaviour::IgnoreStop) {
                    error = "Stopped";
                    return false;
                }
            }
            if (behaviour->mode == Behaviour::Fail) { error = "the test converter refuses"; return false; }
            return true;
        };

        std::string error;
        const fs::path out = films / "clip.ucvout";
        Check(!UltraCanvasFileLoader::CanConvertVideoTo("ucvout"), "Before a converter: .ucvout is not written");
        Check(!UltraCanvasFileLoader::ConvertVideoFile(PathToUtf8(film), PathToUtf8(out), error) &&
              !error.empty() && !fs::exists(out),
              "... and ConvertVideoFile refuses it, with a reason (" + error + ")");

        // The same type is the file as it is, converter or none.
        const fs::path copy = films / "copy.mp4";
        std::vector<double> copySteps;
        Check(UltraCanvasFileLoader::ConvertVideoFile(PathToUtf8(film), PathToUtf8(copy), error,
                                                      [&copySteps](double f) { copySteps.push_back(f); return true; }),
              "Saved as the same type: " + error);
        Check(ReadAll(copy) == ReadAll(film), "... the file copied as it is");
        Check(!copySteps.empty() && copySteps.back() == 1.0, "... and its progress reaches the end");
        Check(UltraCanvasFileLoader::ConvertVideoFile(PathToUtf8(film), PathToUtf8(film), error) &&
              ReadAll(film) == "not really a film, but bytes to keep",
              "Saved over itself: nothing to do, the file untouched");

        RegisterMediaCodec(codec);
        Check(CanEncodeMediaExtension(MediaCodecKind::Video, "ucvout"),
              "A registration with a converter is one that encodes");
        Check(UltraCanvasFileLoader::CanConvertVideoTo("ucvout") &&
              UltraCanvasFileLoader::CanConvertVideoTo(".UCVO") &&
              UltraCanvasFileLoader::CanConvertVideoTo(PathToUtf8(films / "x.ucvout")),
              "CanConvertVideoTo: the type, an alias in any case, a path");
        const std::vector<std::string> types = UltraCanvasFileLoader::GetVideoSaveExtensions();
        Check(std::find(types.begin(), types.end(), "ucvout") != types.end() &&
              std::find(types.begin(), types.end(), "ucvo") != types.end(),
              "GetVideoSaveExtensions: the type and its alias");
        FileDialogOptions opts;
        opts.SetDefaultFileName("clip.ucvout");
        UltraCanvasFileLoader::AddVideoSaveTypes(opts);
        Check(!opts.filters.empty() && opts.filters.front().description == "Test video type" &&
              opts.filters.front().extensions.size() == 2,
              "AddVideoSaveTypes: the type under its name, first for a name that ends in it");
        bool onlyConverted = true;
        for (const FileFilter& filter : opts.filters)
            for (const std::string& ext : filter.extensions)
                onlyConverted = onlyConverted && UltraCanvasFileLoader::CanConvertVideoTo(ext);
        Check(onlyConverted, "... and no type no converter writes (a camera's muxer alone does not count)");

        // Converted: the converter wrote beside the target, the result was put in place.
        std::vector<double> seen;
        const bool converted = UltraCanvasFileLoader::ConvertVideoFile(
                PathToUtf8(film), PathToUtf8(out), error, [&seen](double f) { seen.push_back(f); return true; });
        Check(converted && ReadAll(out) == "converted:" + ReadAll(film), "Saved as .ucvout by its converter: " + error);
        Check(!behaviour->wroteTo.empty() && PathFromUtf8(behaviour->wroteTo) != out &&
              PathFromUtf8(behaviour->wroteTo).parent_path() == films &&
              PathFromUtf8(behaviour->wroteTo).extension() == out.extension(),
              "... written beside the target under another name, its extension last (" +
              PathToUtf8(PathFromUtf8(behaviour->wroteTo).filename()) + ")");
        Check(seen.size() == 4 && seen.back() == 1.0, "... its progress handed on");
        Check(Leftovers(films, { "clip.mp4", "copy.mp4", "clip.ucvout" }).empty(), "... and nothing else left");

        // Stopped: no file, and the one it was to replace is kept.
        WriteAll(out, "the earlier file");
        behaviour->reported.clear();
        Check(!UltraCanvasFileLoader::ConvertVideoFile(PathToUtf8(film), PathToUtf8(out), error,
                                                       [](double f) { return f < 0.5; }) &&
              error == "Stopped",
              "Stopped half way: not saved, and says so (" + error + ")");
        Check(ReadAll(out) == "the earlier file", "... the file it was to replace kept as it was");
        Check(Leftovers(films, { "clip.mp4", "copy.mp4", "clip.ucvout" }).empty(), "... and no half file left");

        // A converter that does not honour a stop is stopped all the same.
        behaviour->mode = Behaviour::IgnoreStop;
        const fs::path fresh = films / "fresh.ucvout";
        Check(!UltraCanvasFileLoader::ConvertVideoFile(PathToUtf8(film), PathToUtf8(fresh), error,
                                                       [](double) { return false; }) &&
              error == "Stopped" && !fs::exists(fresh),
              "A converter that goes on after a stop: still not saved");
        Check(Leftovers(films, { "clip.mp4", "copy.mp4", "clip.ucvout" }).empty(), "... what it wrote thrown away");

        // Failed: the converter's reason, the earlier file kept.
        behaviour->mode = Behaviour::Fail;
        Check(!UltraCanvasFileLoader::ConvertVideoFile(PathToUtf8(film), PathToUtf8(out), error) &&
              error == "the test converter refuses" && ReadAll(out) == "the earlier file",
              "A failed conversion: its reason, the earlier file kept");
        Check(Leftovers(films, { "clip.mp4", "copy.mp4", "clip.ucvout" }).empty(), "... and no half file left");

        UnregisterMediaCodec(MediaCodecKind::Video, "ucvout");
        Check(!UltraCanvasFileLoader::CanConvertVideoTo("ucvout"), "Unregistered: no longer written");

#ifdef MEDIAVIEWER_SAVE_TEST_WITH_VIDEOFX
        // VideoFX's converter, as an application registers it: a generated
        // clip saved as every type it writes, each a film again.
        const int registered = VideoFX::VideoFX_RegisterWithFileLoader();
        Check(VideoFX::VideoFX_RegisterWithFileLoader() == registered, "VideoFX_RegisterWithFileLoader: once is once");
        if (!VideoFX::VideoFX_IsAvailable()) {
            Check(registered == 0, "A VideoFX without FFmpeg registers nothing: a video has no Save as");
        } else {
            Check(registered > 0, "VideoFX registers the types it writes (" + std::to_string(registered) + ")");
            const fs::path source = films / "generated.mp4";
            const bool generated = VideoFX::VideoFX_GenerateTestClip(PathToUtf8(source), 1.0, 160, 96, 10.0, true) ==
                                   VideoFX::VideoFXResult::Ok;
            Check(generated, "a one-second clip to save");
            bool everyWritten = true;
            for (const VideoFX::VideoFXFileFormat& f : VideoFX::VideoFX_GetFileFormats()) {
                if (f.kind != VideoFX::VideoFXFileKind::Video || !f.canWrite) continue;
                everyWritten = everyWritten && UltraCanvasFileLoader::CanConvertVideoTo(f.extension);
            }
            Check(everyWritten, "... every video type VideoFX writes is one ConvertVideoFile writes");
            for (const std::string ext : { "webm", "mkv", "mov", "avi" }) {
                if (!generated) break;
                if (!UltraCanvasFileLoader::CanConvertVideoTo(ext)) {
                    std::cout << "         (VideoFX writes no ." << ext << " in this build)\n";
                    continue;
                }
                const fs::path target = films / PathFromUtf8("generated." + ext);
                double last = 0.0;
                const bool ok = UltraCanvasFileLoader::ConvertVideoFile(
                        PathToUtf8(source), PathToUtf8(target), error, [&last](double f) { last = f; return true; });
                VideoFX::VideoFXMediaInfo info;
                const bool probed = ok && VideoFX::VideoFX_Probe(PathToUtf8(target), info) == VideoFX::VideoFXResult::Ok;
                Check(ok && probed && info.HasVideo() && info.duration > 0.5 && info.duration < 1.5,
                      "Saved as ." + ext + " by VideoFX, a film of the same length: " + error);
                Check(last > 0.9, "... its progress reported to the end");
            }
            // Stopped at the first report: no file.
            const fs::path stopped = films / "stopped.webm";
            if (generated && UltraCanvasFileLoader::CanConvertVideoTo("webm")) {
                Check(!UltraCanvasFileLoader::ConvertVideoFile(PathToUtf8(source), PathToUtf8(stopped), error,
                                                               [](double) { return false; }) &&
                      error == "Stopped" && !fs::exists(stopped),
                      "A VideoFX conversion stopped: not saved, no file (" + error + ")");
            }
        }
#endif
    }

    fs::remove_all(scratch, ec);
    std::cout << (g_failures == 0 ? "All save checks passed.\n" : "Save checks FAILED.\n");
    return g_failures == 0 ? 0 : 1;
#endif
}
