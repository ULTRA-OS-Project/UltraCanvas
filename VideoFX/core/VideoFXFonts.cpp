// VideoFX/core/VideoFXFonts.cpp
// Which font a text overlay without a fontPath is drawn in.
//
// Order: the application's choice (VideoFX_SetDefaultFontPath); the
// framework's bundled Ubuntu font next to the executable, where UltraCanvas
// applications ship media/ (so every UltraCanvas app renders titles alike);
// a common system sans font; fontconfig's "Sans" - and that last one only if
// this FFmpeg can actually load it, so a machine with no usable font gets a
// clear error before the export starts rather than a filter failure halfway.
// Version: 0.2.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "VideoFXBackend.h"
#include "VideoFXPlatform.h"
#include "VideoFX/VideoFX.h"

#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"   // PathFromUtf8

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace VideoFX {

namespace {

std::mutex fontMutex;
std::string chosenFont;             // VideoFX_SetDefaultFontPath

bool FileExists(const std::string& utf8) {
    std::error_code ec;
    return !utf8.empty() && std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(utf8), ec);
}

// The framework's bundled font, in each deployment layout UltraCanvas uses
// for its resources directory (see UltraCanvasConfig.cpp SetResourcesDir)
std::string BundledFont() {
    const std::string exe = Internal::ExecutableDir();
    if (exe.empty()) return "";
    const char* dirs[] = {
        "/share/media/fonts/",              // dev build, executable at the build root
        "/../share/media/fonts/",           // portable Linux package, bin/ + share/; build/bin tests
        "/../share/UltraCanvas/media/fonts/",
        "/Resources/media/fonts/",          // Windows: exe/Resources
        "/../Resources/media/fonts/",       // macOS: Contents/MacOS -> Contents/Resources
    };
    for (const char* d : dirs) {
        const std::string candidate = exe + d + "Ubuntu-R.ttf";
        if (FileExists(candidate)) return candidate;
    }
    return "";
}

std::string SystemFont() {
    static const char* candidates[] = {
#if defined(_WIN32)
        "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
#else
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf", "/usr/share/fonts/gnu-free/FreeSans.ttf",
#endif
    };
    for (const char* c : candidates) {
        if (FileExists(c)) return c;
    }
    return "";
}

} // namespace

namespace Internal {

std::string ResolveDefaultFont() {
    {
        std::lock_guard<std::mutex> lock(fontMutex);
        if (!chosenFont.empty()) return chosenFont;
    }
    static const std::string automatic = [] {
        std::string f = BundledFont();
        return f.empty() ? SystemFont() : f;
    }();
    return automatic;
}

bool FontconfigCanDrawText() {
    static const bool works = [] {
        EnsureBackendInitialised();
        if (!avfilter_get_by_name("drawtext")) return false;
        // drawtext loads its font when it is created, so building the graph is the test
        FilterGraphPtr graph(avfilter_graph_alloc());
        if (!graph) return false;
        AVFilterInOut* inputs = nullptr;
        AVFilterInOut* outputs = nullptr;
        const int saved = av_log_get_level();
        av_log_set_level(AV_LOG_QUIET);     // a failed probe is an answer, not an error
        const int err = avfilter_graph_parse2(graph.get(), "color=s=16x16:d=0.04,drawtext=text=x:font=Sans,nullsink",
                                              &inputs, &outputs);
        av_log_set_level(saved);
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        return err >= 0;
    }();
    return works;
}

} // namespace Internal

bool VideoFX_SetDefaultFontPath(const std::string& path) {
    if (!path.empty() && !FileExists(path)) return false;
    std::lock_guard<std::mutex> lock(fontMutex);
    chosenFont = path;
    return true;
}

std::string VideoFX_GetDefaultFontPath() {
    return Internal::ResolveDefaultFont();
}

} // namespace VideoFX
