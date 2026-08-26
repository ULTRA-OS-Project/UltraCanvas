// Tests/EPSProbeTest.cpp
// Parses EPS files through the EPS plugin's EPSDocument and reports what the
// interpreter understood: document metadata and the parse diagnostics
// (tokens dispatched, unknown operators with counts, warnings).
//
// This is the triage tool for "the file displays wrong": an unknown operator
// the file leans on is a feature to implement; a warning marks an
// approximation the renderer took.
//
// Usage: EPSProbeTest [--render <outdir>] [file.eps ...]
// With no arguments it probes the repo samples (media/eps/*.eps). Exit code
// is the number of files that failed to load, so it doubles as a regression
// test: the shipped samples must always parse.
//
// --render <outdir> additionally rasterizes each file through the real
// render context into <outdir>/<stem>.png (longest side capped at 1600 px),
// so the renderer's output can be compared against a ghostscript rendering
// of the same file. Available when the build has cairo (EPSPROBE_HAVE_CAIRO).
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/EPS/UltraCanvasEPSPlugin.h"

#ifdef EPSPROBE_HAVE_CAIRO
#include <cairo.h>
#endif

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

std::string FileStem(const std::string& path) {
    std::string stem = path;
    size_t slash = stem.find_last_of("/\\");
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    return stem;
}

bool RenderToPng(EPSDocument& doc, const std::string& srcPath, const std::string& outDir) {
#ifndef EPSPROBE_HAVE_CAIRO
    (void)doc; (void)srcPath; (void)outDir;
    std::printf("  render: unavailable (built without cairo)\n");
    return false;
#else
    float w = doc.GetWidth(), h = doc.GetHeight();
    if (w <= 0 || h <= 0) {
        std::printf("  render: skipped (no size)\n");
        return false;
    }
    const float cap = 1600.0f;
    float s = std::min(4.0f, cap / std::max(w, h));   // small pages render sharper
    int pw = std::max(1, static_cast<int>(w * s + 0.5f));
    int ph = std::max(1, static_cast<int>(h * s + 0.5f));

    auto ctx = CreateRenderContext(Size2Di(pw, ph), nullptr);
    if (!ctx) {
        std::printf("  render: failed to create offscreen context\n");
        return false;
    }

    // White page behind the drawing, as PostScript media
    ctx->SetFillPaint(Color(255, 255, 255, 255));
    ctx->FillRectangle(Rect2Dd(0, 0, pw, ph));

    doc.Render(ctx.get(), s);

    cairo_t* cr = static_cast<cairo_t*>(ctx->GetNativeContext());
    if (!cr) {
        std::printf("  render: no native context\n");
        return false;
    }
    cairo_surface_t* surface = cairo_get_target(cr);
    cairo_surface_flush(surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        std::printf("  render: context error: %s\n",
                    cairo_status_to_string(cairo_status(cr)));
    }
    std::string outPath = outDir + "/" + FileStem(srcPath) + ".png";
    cairo_status_t ws = cairo_surface_write_to_png(surface, outPath.c_str());
    if (ws != CAIRO_STATUS_SUCCESS) {
        std::printf("  render: failed to write %s: %s\n", outPath.c_str(),
                    cairo_status_to_string(ws));
        return false;
    }
    std::printf("  render: %s (%dx%d)\n", outPath.c_str(), pw, ph);
    return true;
#endif
}

bool ProbeFile(const std::string& path, const std::string& renderDir) {
    std::printf("== %s ==\n", path.c_str());

    EPSDocument doc;
    bool loaded = doc.LoadFromFile(path);
    const auto& diag = doc.GetDiagnostics();

    if (!loaded) {
        std::printf("  LOAD FAILED\n");
        for (const auto& w : diag.warnings) {
            std::printf("  warning: %s\n", w.c_str());
        }
        std::printf("\n");
        return false;
    }

    std::printf("  title: %s\n  creator: %s\n",
                doc.GetTitle().c_str(), doc.GetCreator().c_str());
    std::printf("  bounding box: %.1f x %.1f pt\n", doc.GetWidth(), doc.GetHeight());
    std::printf("  tokens dispatched: %zu\n", diag.tokenCount);

    if (!diag.unknownOperators.empty()) {
        std::printf("  unknown operators: %zu distinct\n", diag.unknownOperators.size());
        for (const auto& [name, count] : diag.unknownOperators) {
            std::printf("    %-24s x%zu\n", name.c_str(), count);
        }
    } else {
        std::printf("  unknown operators: none\n");
    }

    for (const auto& w : diag.warnings) {
        std::printf("  warning: %s\n", w.c_str());
    }

    if (!renderDir.empty()) {
        RenderToPng(doc, path, renderDir);
    }

    std::printf("\n");
    return true;
}

} // namespace

int main(int argc, char** argv) {
    // Text measurement runs through the core font system; images do not
    // need vips here, but initializing keeps parity with real applications.
    UCImage::InitializeImageSubsysterm(argv[0]);

    std::string renderDir;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--render" && i + 1 < argc) {
            renderDir = argv[++i];
        } else {
            files.push_back(arg);
        }
    }
    if (files.empty()) {
#ifdef EPS_SAMPLES_DIR
        files.push_back(std::string(EPS_SAMPLES_DIR) + "/demo.eps");
        files.push_back(std::string(EPS_SAMPLES_DIR) + "/gears.eps");
#else
        std::printf("usage: EPSProbeTest file.eps [file.eps ...]\n");
        return 2;
#endif
    }

    int failures = 0;
    for (const auto& f : files) {
        if (!ProbeFile(f, renderDir)) ++failures;
    }
    std::printf("%d of %zu file(s) failed to load\n", failures, files.size());
    return failures;
}
