// Tests/FilerFormatListTest.cpp
// The list of files behind Display > Thumbnails and Display > Detail view:
// UltraCanvasFilerWidget::GetPreviewableFormats().
//
// The rule this guards: every format the FileLoader inventory reports for
// this build must be in that list, filed under the preview kind its media
// category belongs to. A format the file manager can open but cannot list is
// a format whose thumbnail nobody can switch on - which is how audio files
// were missing from both lists.
// Version: 1.0.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasSupportedFormats.h"

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// The preview kinds a media category may legitimately land in. Most map one
// to one; Documents split three ways because PDF renders a page, plain text
// previews as text and the rest as a document page.
std::set<FilerPreviewType> KindsFor(MediaFormatCategory category) {
    switch (category) {
        case MediaFormatCategory::Bitmap:  return {FilerPreviewType::Bitmaps};
        case MediaFormatCategory::Vector:  return {FilerPreviewType::VectorGraphics};
        case MediaFormatCategory::Model3D: return {FilerPreviewType::Models3D};
        case MediaFormatCategory::Document:
            return {FilerPreviewType::PDF, FilerPreviewType::Text,
                    FilerPreviewType::Docs};
        case MediaFormatCategory::Spreadsheet:
            return {FilerPreviewType::Spreadsheets};
        case MediaFormatCategory::Audio:   return {FilerPreviewType::Audio};
        case MediaFormatCategory::Video:   return {FilerPreviewType::Videos};
    }
    return {};
}

std::string KindNames(const std::set<FilerPreviewType>& kinds) {
    std::string out;
    for (FilerPreviewType k : kinds) {
        if (!out.empty()) out += " / ";
        out += UltraCanvasFilerWidget::PreviewTypeLabel(k);
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    // The inventory probes libvips per format, so the image subsystem has to
    // be up before it is asked anything.
    UCImage::InitializeImageSubsysterm(argv[0]);

    const std::vector<FilerFormatInfo> list =
            UltraCanvasFilerWidget::GetPreviewableFormats();
    std::map<std::string, FilerFormatInfo> byExtension;
    for (const FilerFormatInfo& f : list) byExtension.emplace(f.extension, f);

    std::cout << "\n=== The list holds every FileLoader format, per file type ===\n";
    size_t checked = 0;
    for (const MediaFormatInfo& f : UltraCanvasFileLoader::GetSupportedFormats()) {
        std::vector<std::string> extensions = {f.extension};
        extensions.insert(extensions.end(), f.aliases.begin(), f.aliases.end());
        for (const std::string& ext : extensions) {
            if (ext.empty()) continue;
            ++checked;
            auto it = byExtension.find(ext);
            const std::string category =
                    UltraCanvasSupportedFormats::GetCategoryName(f.category);
            const bool present = it != byExtension.end();
            Check(present, ext + " (" + category + ") is in the list");
            if (!present) continue;
            const std::set<FilerPreviewType> allowed = KindsFor(f.category);
            Check(allowed.count(it->second.kind) != 0,
                  ext + " is filed under \"" +
                          UltraCanvasFilerWidget::PreviewTypeLabel(it->second.kind) +
                          "\", one of " + KindNames(allowed));
            Check(!it->second.label.empty(), ext + " carries a readable name");
        }
    }
    Check(checked > 0, "the FileLoader reports formats at all");

    // The kinds have to cover every media category, or a format of an
    // uncovered one could not be filed anywhere.
    std::cout << "\n=== Every media category has a preview kind ===\n";
    for (MediaFormatCategory c : {MediaFormatCategory::Bitmap,
                                  MediaFormatCategory::Vector,
                                  MediaFormatCategory::Model3D,
                                  MediaFormatCategory::Document,
                                  MediaFormatCategory::Spreadsheet,
                                  MediaFormatCategory::Audio,
                                  MediaFormatCategory::Video}) {
        Check(!KindsFor(c).empty(),
              std::string(UltraCanvasSupportedFormats::GetCategoryName(c)) +
                      " maps to a preview kind");
    }
    std::cout << "\n=== The list itself ===\n";
    Check(!list.empty(), "the list is not empty");
    std::set<std::string> seen;
    bool unique = true, sorted = true;
    uint32_t previousKind = 0;
    std::string previousExtension;
    for (const FilerFormatInfo& f : list) {
        if (!seen.insert(f.extension).second) unique = false;
        if (f.kind == FilerPreviewType::NonePreview) sorted = false;
        const uint32_t kind = static_cast<uint32_t>(f.kind);
        // Grouped by kind in enum order, extensions sorted inside a group.
        if (kind < previousKind) sorted = false;
        if (kind == previousKind && f.extension < previousExtension) sorted = false;
        previousKind = kind;
        previousExtension = f.extension;
    }
    Check(unique, "each extension appears once");
    Check(sorted, "the list is grouped by kind and sorted inside each group");

    // Audio is the kind with no thumbnail producer: its rows must say so
    // rather than offering a switch that changes nothing.
    std::cout << "\n=== What the rows claim about this build ===\n";
    for (const FilerFormatInfo& f : list) {
        if (f.kind != FilerPreviewType::Audio) continue;
        Check(!f.thumbnailSupported,
              f.extension + ": no thumbnail producer is advertised");
    }
    // The container formats no reader unpacks must not advertise a page
    // preview either.
    for (const char* ext : {"epub", "mobi", "azw3", "xls"}) {
        auto it = byExtension.find(ext);
        if (it == byExtension.end()) continue;   // not in this build's tables
        Check(!it->second.thumbnailSupported,
              std::string(ext) + ": no page preview is advertised (no reader)");
    }
    for (const char* ext : {"txt", "md", "csv", "html"}) {
        auto it = byExtension.find(ext);
        if (it == byExtension.end()) continue;
        Check(it->second.thumbnailSupported,
              std::string(ext) + ": a page preview is advertised");
    }

    std::cout << "\n"
              << (g_failures == 0 ? "All checks passed\n"
                                  : std::to_string(g_failures) + " check(s) FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
