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
// Usage: XARProbeTest [file.xar ...]
// With no arguments it probes the repo samples (media/xar/*.xar). Exit code
// is the number of files that failed to load, so it doubles as a regression
// test: the shipped samples must always parse.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/XAR/UltraCanvasXARPlugin.h"

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

bool ProbeFile(const std::string& path) {
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

    std::printf("\n");
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) files.push_back(argv[i]);
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
        if (!ProbeFile(f)) ++failures;
    }
    std::printf("%d of %zu file(s) failed to load\n", failures, files.size());
    return failures;
}
