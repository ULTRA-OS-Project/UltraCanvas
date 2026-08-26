// Tests/XARProbeTest.cpp
// Parses XAR files through the XAR plugin's XARDocument and reports what the
// parser understood: document metadata, the node tree by type, and the parse
// diagnostics (records dispatched, unhandled record tags, warnings).
//
// This is the triage tool for "the file displays wrong": an unhandled tag the
// file leans on is a feature to implement; a warning is a parse defect; a
// node-type count of zero where the drawing clearly has such objects points
// at the matching Parse*Record handler.
//
// Usage: XARProbeTest [--render <outdir>] [file.xar ...]
// With no arguments it probes the repo samples (media/xar/*.xar). Exit code
// is the number of files that failed to load, so it doubles as a regression
// test: the shipped samples must always parse.
//
// --render <outdir> additionally rasterizes each file through the real
// render context into <outdir>/<stem>.png (longest side capped at 1600 px),
// so the renderer's output can be compared against reference screenshots.
// Available when the build has cairo (XARPROBE_HAVE_CAIRO).
// Version: 1.1.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/XAR/UltraCanvasXARPlugin.h"

#ifdef XARPROBE_HAVE_CAIRO
#include <cairo.h>
#endif

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

const char* NodeTypeName(XARNodeType t) {
    switch (t) {
        case XARNodeType::Document:       return "Document";
        case XARNodeType::Chapter:        return "Chapter";
        case XARNodeType::Spread:         return "Spread";
        case XARNodeType::Layer:          return "Layer";
        case XARNodeType::Page:           return "Page";
        case XARNodeType::Group:          return "Group";
        case XARNodeType::Path:           return "Path";
        case XARNodeType::Rectangle:      return "Rectangle";
        case XARNodeType::Ellipse:        return "Ellipse";
        case XARNodeType::Polygon:        return "Polygon";
        case XARNodeType::Text:           return "Text";
        case XARNodeType::TextStory:      return "TextStory";
        case XARNodeType::TextLine:       return "TextLine";
        case XARNodeType::TextString:     return "TextString";
        case XARNodeType::Bitmap:         return "Bitmap";
        case XARNodeType::ContonedBitmap: return "ContonedBitmap";
        case XARNodeType::Blend:          return "Blend";
        case XARNodeType::Mould:          return "Mould";
        case XARNodeType::Bevel:          return "Bevel";
        case XARNodeType::Contour:        return "Contour";
        case XARNodeType::Shadow:         return "Shadow";
        case XARNodeType::ClipView:       return "ClipView";
        case XARNodeType::Feather:        return "Feather";
        case XARNodeType::LiveEffect:     return "LiveEffect";
        case XARNodeType::Brush:          return "Brush";
        case XARNodeType::Unknown:        return "Unknown";
    }
    return "?";
}

void CountNodes(const XARNodePtr& node, std::map<XARNodeType, size_t>& counts,
                size_t& total, size_t& maxDepth, size_t depth) {
    if (!node) return;
    counts[node->type]++;
    total++;
    if (depth > maxDepth) maxDepth = depth;
    for (const auto& child : node->children) {
        CountNodes(child, counts, total, maxDepth, depth + 1);
    }
}

std::string FileStem(const std::string& path) {
    std::string stem = path;
    size_t slash = stem.find_last_of("/\\");
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    return stem;
}

// Rasterize the document into outDir/<stem>.png through the real render
// context, longest side capped at 1600 px. Returns false when rendering is
// unavailable or fails; probing continues either way.
bool RenderToPng(XARDocument& doc, const std::string& srcPath, const std::string& outDir) {
#ifndef XARPROBE_HAVE_CAIRO
    (void)doc; (void)srcPath; (void)outDir;
    std::printf("  render: unavailable (built without cairo)\n");
    return false;
#else
    bool allOk = true;
    const int pages = doc.GetPageCount();
    for (int page = 0; page < pages; ++page) {
        float w = doc.GetPageWidth(page), h = doc.GetPageHeight(page);
        if (w <= 0 || h <= 0) {
            std::printf("  render: page %d skipped (no size)\n", page + 1);
            allOk = false;
            continue;
        }
        const float cap = 1600.0f;
        float s = std::min(1.0f, cap / std::max(w, h));
        int pw = std::max(1, static_cast<int>(w * s + 0.5f));
        int ph = std::max(1, static_cast<int>(h * s + 0.5f));

        auto ctx = CreateRenderContext(Size2Di(pw, ph), nullptr);
        if (!ctx) {
            std::printf("  render: failed to create offscreen context\n");
            return false;
        }

        // White page behind the drawing, like the on-screen element
        ctx->SetFillPaint(Color(255, 255, 255, 255));
        ctx->FillRectangle(Rect2Di(0, 0, pw, ph));

        // Pre-scale the context and render at scale 1 so every coordinate,
        // including the document's internal Y-flip, scales consistently.
        ctx->PushState();
        ctx->Scale(s, s);
        doc.RenderPage(ctx.get(), page, 1.0f);
        ctx->PopState();

        cairo_t* cr = static_cast<cairo_t*>(ctx->GetNativeContext());
        if (!cr) {
            std::printf("  render: no native context\n");
            return false;
        }
        cairo_surface_t* surface = cairo_get_target(cr);
        cairo_surface_flush(surface);
        if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
            std::printf("  render: page %d context error: %s\n", page + 1,
                        cairo_status_to_string(cairo_status(cr)));
        }
        std::string outPath = outDir + "/" + FileStem(srcPath) +
                (page == 0 ? std::string() : "-p" + std::to_string(page + 1)) + ".png";
        cairo_status_t ws = cairo_surface_write_to_png(surface, outPath.c_str());
        if (ws != CAIRO_STATUS_SUCCESS) {
            std::printf("  render: failed to write %s: %s\n", outPath.c_str(),
                        cairo_status_to_string(ws));
            allOk = false;
            continue;
        }
        std::printf("  render: %s (%dx%d)\n", outPath.c_str(), pw, ph);
    }
    return allOk;
#endif
}

bool ProbeFile(const std::string& path, const std::string& renderDir) {
    std::printf("== %s ==\n", path.c_str());

    XARDocument doc;
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

    std::printf("  file type: %s, producer: %s %s (build %s)\n",
                doc.GetFileType().c_str(), doc.GetProducer().c_str(),
                doc.GetProducerVersion().c_str(), doc.GetProducerBuild().c_str());
    std::printf("  document size: %.1f x %.1f px\n", doc.GetWidth(), doc.GetHeight());
    std::printf("  records dispatched: %zu\n", diag.recordCount);

    std::map<XARNodeType, size_t> counts;
    size_t total = 0, maxDepth = 0;
    CountNodes(doc.GetRoot(), counts, total, maxDepth, 0);
    std::printf("  node tree: %zu nodes, depth %zu\n", total, maxDepth);
    for (const auto& [type, count] : counts) {
        std::printf("    %-15s %zu\n", NodeTypeName(type), count);
    }

    if (!diag.unhandledTags.empty()) {
        // Sorted for stable, comparable output
        std::map<uint32_t, size_t> sorted(diag.unhandledTags.begin(),
                                          diag.unhandledTags.end());
        std::printf("  unhandled record tags: %zu distinct\n", sorted.size());
        for (const auto& [tag, count] : sorted) {
            std::printf("    tag %5u (0x%04X)  x%zu\n", tag, tag, count);
        }
    } else {
        std::printf("  unhandled record tags: none\n");
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
    // Bitmap fills decode through UCImage, which needs the image subsystem
    // (vips) initialized — normally the application's job.
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
#ifdef XAR_SAMPLES_DIR
        files.push_back(std::string(XAR_SAMPLES_DIR) + "/demo.xar");
        files.push_back(std::string(XAR_SAMPLES_DIR) + "/backside.xar");
#else
        std::printf("usage: XARProbeTest file.xar [file.xar ...]\n");
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
