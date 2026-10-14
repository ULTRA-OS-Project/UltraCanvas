// Tests/CDRPluginRenderTest.cpp
// Draws every page of the bundled CorelDRAW samples (media/vector/CDR)
// through the CDR plugin - libcdr parsing into UltraCanvasCDRPainterImpl,
// UltraCanvasCDRRenderer replaying the page - into an offscreen context, and
// checks that each page painted something.
//
// The painter records a drawing as draw commands on the document's pages
// during the parse; the renderer replays them when a page is drawn, after
// the parse has returned. The commands used to call back into the painter
// that recorded them, a local of ParseCDRFile and gone by then, so a
// gradient fill looked its gradient up through freed memory: the DemoApp's
// CDR page crashed on detailed.cdr, which has ten of them. Each page is
// drawn here well after its parse, the way the DemoApp draws it.
//
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/CDR/UltraCanvasCDRPlugin.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"

#ifdef CDRRENDER_HAVE_CAIRO
#include <cairo.h>
#endif

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#if defined(__linux__) && defined(__GLIBC__)
#include <execinfo.h>
#include <csignal>
#include <unistd.h>
#define CDRRENDER_HAVE_BACKTRACE 1
#endif

#ifndef CDRRENDER_SAMPLES_DIR
#error "CDRRENDER_SAMPLES_DIR must name media/vector/CDR (Tests/VectorFormatsTests.cmake)"
#endif

using namespace UltraCanvas;

namespace {

int failures = 0;

#ifdef CDRRENDER_HAVE_BACKTRACE
// A crash in a replayed draw command is the failure this test is about; on a
// CI runner the backtrace is all there is to say whose code it was. Only
// async-signal-safe calls here.
void CrashHandler(int sig) {
    static const char msg[] = "\n*** CDRPluginRenderTest crashed - backtrace ***\n";
    ssize_t ignored = write(STDERR_FILENO, msg, sizeof(msg) - 1);
    (void)ignored;
    void* frames[32];
    const int n = backtrace(frames, 32);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    _exit(128 + sig);
}
#endif

// Unbuffered: a crash must not take the log of how far the test got with it.
void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
    std::fflush(stdout);
}

void Step(const std::string& what) {
    std::printf("  .. %s\n", what.c_str());
    std::fflush(stdout);
}

struct Sample {
    const char* file;
    bool hasGradients;
};

void DrawSample(const Sample& sample) {
    const std::string path = std::string(CDRRENDER_SAMPLES_DIR) + "/" + sample.file;
    std::printf("%s\n", sample.file);

    UltraCanvasCDRRenderer renderer;
    Step("LoadFromFile (libcdr parses; the painter is gone when it returns)");
    Check(renderer.LoadFromFile(path), std::string(sample.file) + " loads");
    if (!renderer.IsLoaded()) return;
    Check(renderer.GetPageCount() >= 1, std::string(sample.file) + " has a page");

    constexpr int kWidth = 480;
    constexpr int kHeight = 360;
    for (int page = 0; page < renderer.GetPageCount(); ++page) {
        auto ctx = CreateRenderContext(Size2Di(kWidth, kHeight), nullptr);
        Check(ctx != nullptr, "offscreen render context created");
        if (!ctx) return;
        ctx->SetFillPaint(Color(255, 255, 255, 255));
        ctx->FillRectangle(Rect2Dd(0, 0, kWidth, kHeight));
        renderer.SetFitMode(CDRFitMode::FitPage);
        renderer.SetViewport(kWidth, kHeight);
        Step("RenderPage " + std::to_string(page + 1) + " (replays the recorded draw commands)");
        renderer.RenderPage(ctx.get(), page);
        Step("RenderPage returned");

#ifdef CDRRENDER_HAVE_CAIRO
        cairo_t* cr = static_cast<cairo_t*>(ctx->GetNativeContext());
        cairo_surface_t* surface = cr ? cairo_get_target(cr) : nullptr;
        if (!surface) continue;
        cairo_surface_flush(surface);
        const unsigned char* data = cairo_image_surface_get_data(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        const int w = cairo_image_surface_get_width(surface);
        const int h = cairo_image_surface_get_height(surface);
        if (!data || stride <= 0) continue;
        int inked = 0;
        std::set<uint32_t> colours;
        for (int y = 0; y < h; y += 2) {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(data + y * stride);
            for (int x = 0; x < w; x += 2) {
                const uint32_t px = row[x] & 0xFFFFFF;
                if (px != 0xFFFFFF) ++inked;
                colours.insert(px);
            }
        }
        Check(inked > (w / 2) * (h / 2) / 100,
              std::string(sample.file) + " page " + std::to_string(page + 1) + " paints its drawing (" +
                      std::to_string(inked) + " sampled pixels inked)");
        // A gradient is many shades of one fill; flat fills give few colours.
        if (sample.hasGradients) {
            Check(colours.size() > 500,
                  std::string(sample.file) + " page " + std::to_string(page + 1) +
                          " shows its gradients (" + std::to_string(colours.size()) + " colours)");
        }
#endif
    }
}

} // namespace

int main(int /*argc*/, char* argv[]) {
#ifdef CDRRENDER_HAVE_BACKTRACE
    std::signal(SIGSEGV, CrashHandler);
    std::signal(SIGABRT, CrashHandler);
    std::signal(SIGBUS, CrashHandler);
#endif
    // The drawings' embedded bitmaps are decoded by the image subsystem
    // (libvips), which an application starts in Initialize().
    Check(UCImage::InitializeImageSubsysterm(argv[0]), "image subsystem started");
    const std::vector<Sample> samples = {
        { "detailed.cdr", true },          // ten gradient fills, embedded bitmaps
        { "door-panel.cdr", false },
        { "dubai-atlantis.cdr", false },
    };
    for (const auto& sample : samples) DrawSample(sample);

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
