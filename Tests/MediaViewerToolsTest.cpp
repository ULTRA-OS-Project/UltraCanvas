// Tests/MediaViewerToolsTest.cpp
// The media viewer's second toolbar row offers only the tools the shown file
// takes. A photo gets zoom, rotate / mirror, the colour adjustments, Curves
// and Save as; a drawing is never offered colour sliders or a histogram; a
// text file, a spreadsheet, a 3D model, a video or an audio file gets none of
// them, because its view brings its own controls. And colour set on one photo
// must not tint the drawing browsed to next - with the sliders hidden there
// would be no way to take it off.
//
// Runs headless: the viewer, its toolbars and the image surface are built
// and loaded without a window; only what decides visibility is checked.
// Version: 1.0.0
// Last Modified: 2026-10-05
// Author: UltraCanvas Framework

#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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

void WriteFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

bool Shown(UltraCanvasMediaViewer& viewer, const std::string& id) {
    UltraCanvasUIElement* item = viewer.FindChildById(id);
    return item && item->IsVisible();
}

// A separator has no element id of its own; the toolbar knows it by its key.
bool SeparatorShown(UltraCanvasMediaViewer& viewer, const std::string& key) {
    auto* row = dynamic_cast<UltraCanvasToolbar*>(viewer.FindChildById("MV_Toolbar2"));
    auto item = row ? row->GetWidget(key) : nullptr;
    return item && item->IsVisible();
}

// The toolbar must say what GetAvailableTools() says, group by group, with
// a separator only between two groups that are both up.
void CheckToolbarMatches(UltraCanvasMediaViewer& viewer, const std::string& label) {
    const MediaViewerTools t = viewer.GetAvailableTools();
    bool zoomOk = true, transformOk = true;
    for (const char* id : { "mv_zoomout", "mv_zoom", "mv_zoomin", "mv_fit" })
        zoomOk = zoomOk && Shown(viewer, id) == t.zoom;
    for (const char* id : { "mv_rotl", "mv_rotr", "mv_mirh", "mv_mirv" })
        transformOk = transformOk && Shown(viewer, id) == t.transform;
    Check(zoomOk, label + ": zoom buttons follow the tool set");
    Check(transformOk, label + ": rotate / mirror buttons follow the tool set");
    Check(Shown(viewer, "mv_adjust") == t.colour && Shown(viewer, "mv_curves") == t.colour,
          label + ": adjustments and Curves follow the tool set");
    Check(Shown(viewer, "mv_save") == t.save, label + ": Save as follows the tool set");
    Check(Shown(viewer, "mv_info"), label + ": Details is always offered");
    Check(SeparatorShown(viewer, "mv_sep3") == (t.zoom && t.transform) &&
          SeparatorShown(viewer, "mv_sep4") == (t.zoom || t.transform),
          label + ": no separator stands alone");
}

void Expect(UltraCanvasMediaViewer& viewer, const fs::path& file,
            bool zoom, bool transform, bool colour, bool save) {
    const std::string label = PathToUtf8(file.filename());
    viewer.SetFiles({ PathToUtf8(file) });
    const MediaViewerTools t = viewer.GetAvailableTools();
    std::cout << "\n" << label << "\n";
    Check(t.zoom == zoom, label + (zoom ? ": offers zoom" : ": offers no zoom"));
    Check(t.transform == transform,
          label + (transform ? ": offers rotate / mirror" : ": offers no rotate / mirror"));
    Check(t.colour == colour,
          label + (colour ? ": offers colour adjustments and Curves"
                          : ": offers no colour adjustments or Curves"));
    Check(t.save == save, label + (save ? ": offers Save as" : ": offers no Save as"));
    CheckToolbarMatches(viewer, label);
}

} // namespace

int main() {
    std::cout << "MediaViewerToolsTest\n";
    // An application does this on start; a headless test has to.
    UCImage::InitializeImageSubsysterm("MediaViewerToolsTest");

    const fs::path dir = fs::temp_directory_path() / PathFromUtf8(
        "uc-mediaviewer-tools-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);

    const fs::path media = PathFromUtf8(UC_MEDIA_DIR);
    const fs::path photo = media / "images" / "tn_dice.jpg";
    const fs::path svg   = dir / "drawing.svg";
    const fs::path text  = dir / "notes.txt";
    const fs::path sheet = dir / "table.csv";
    const fs::path model = dir / "part.stl";
    // Empty media files: the player fails to open them (or, in a build without
    // that backend, the image fallback does) - no playback starts either way,
    // and neither way may offer the image tools.
    const fs::path video = dir / "clip.mp4";
    const fs::path audio = dir / "song.mp3";

    WriteFile(svg,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"48\">"
        "<rect x=\"4\" y=\"4\" width=\"56\" height=\"40\" fill=\"#3080e0\"/></svg>");
    WriteFile(text, "Plain text.\n");
    WriteFile(sheet, "a,b\n1,2\n");
    WriteFile(model,
        "solid part\n"
        " facet normal 0 0 1\n  outer loop\n"
        "   vertex 0 0 0\n   vertex 1 0 0\n   vertex 0 1 0\n"
        "  endloop\n endfacet\n"
        "endsolid part\n");
    WriteFile(video, "");
    WriteFile(audio, "");

    auto viewer = CreateMediaViewer("Viewer", 0, 0, 900, 600);

    std::cout << "\nNothing loaded\n";
    {
        const MediaViewerTools t = viewer->GetAvailableTools();
        Check(!t.zoom && !t.transform && !t.colour && !t.save,
              "an empty viewer offers no view / edit tool");
        CheckToolbarMatches(*viewer, "empty");
    }

    Expect(*viewer, photo, true, true, true, true);
    // A drawing on the image surface turns, mirrors, zooms and saves as a
    // bitmap - but its pixels are only a rendering, so no colour, no curves.
    // (A build with no SVG loader shows nothing, and so offers nothing.)
    viewer->SetFiles({ PathToUtf8(svg) });
    auto drawn = viewer->GetSurface()->GetImage();
    const bool svgShown = drawn && drawn->IsValid();
    if (!svgShown) std::cout << "\n(no SVG loader in this build)\n";
    Expect(*viewer, svg, svgShown, svgShown, false, svgShown);
    Expect(*viewer, text,  false, false, false, false);
    Expect(*viewer, sheet, false, false, false, false);
    Expect(*viewer, model, false, false, false, false);
    Expect(*viewer, video, false, false, false, false);
    Expect(*viewer, audio, false, false, false, false);

    // A PDF zooms in the PDF view; without the PDF plugin a libvips build
    // rasterizes it onto the image surface instead. Either way it is a
    // document, not a photo.
    {
        const fs::path pdf = media / "sample.pdf";
        viewer->SetFiles({ PathToUtf8(pdf) });
        std::cout << "\nsample.pdf\n";
        Check(!viewer->GetAvailableTools().colour, "sample.pdf: offers no colour adjustments or Curves");
        CheckToolbarMatches(*viewer, "sample.pdf");
    }

    // ----- The adjustments panel and the adjustments themselves -----
    std::cout << "\nAdjustments across files\n";
    {
        auto* adjust = dynamic_cast<UltraCanvasButton*>(viewer->FindChildById("mv_adjust"));
        auto* gamma  = dynamic_cast<UltraCanvasSlider*>(viewer->FindChildById("adj_gamma"));
        Check(adjust && adjust->onToggle && gamma, "the adjustments toggle and gamma slider exist");
        if (adjust && adjust->onToggle && gamma) {
            viewer->SetFiles({ PathToUtf8(photo), PathToUtf8(svg), PathToUtf8(text) });
            adjust->onToggle(true);
            gamma->SetValue(2.0f);
            Check(Shown(*viewer, "MV_Adjust"), "photo: the open panel shows");
            Check(viewer->GetSurface()->GetAdjustments().gamma == 2.0,
                  "photo: the surface draws with the gamma set");

            viewer->Next();   // the drawing
            Check(!Shown(*viewer, "MV_Adjust"), "drawing: the panel folds away");
            Check(viewer->GetSurface()->GetAdjustments().IsIdentity(),
                  "drawing: shown without the photo's adjustments");

            viewer->Next();   // the text file
            Check(!Shown(*viewer, "MV_Adjust"), "text: the panel stays folded away");

            viewer->Next();   // round to the photo again
            Check(Shown(*viewer, "MV_Adjust"), "photo again: the panel comes back");
            Check(viewer->GetSurface()->GetAdjustments().gamma == 2.0,
                  "photo again: the gamma carried over");

            viewer->SetTopBarsVisible(false);
            Check(!Shown(*viewer, "MV_Adjust"), "bars hidden: the panel is hidden with them");
            viewer->SetTopBarsVisible(true);
            Check(Shown(*viewer, "MV_Adjust"), "bars shown: the open panel returns");

            adjust->onToggle(false);
            Check(!Shown(*viewer, "MV_Adjust"), "toggle off: the panel closes");
        }
    }

    viewer->CloseFile();
    {
        const MediaViewerTools t = viewer->GetAvailableTools();
        std::cout << "\nAfter CloseFile\n";
        Check(!t.zoom && !t.transform && !t.colour && !t.save,
              "a closed viewer offers no view / edit tool");
    }

    std::error_code ec;
    fs::remove_all(dir, ec);

    std::cout << "\n" << (g_failures == 0 ? "All checks passed" : "FAILURES: ")
              << (g_failures == 0 ? "" : std::to_string(g_failures)) << "\n";
    return g_failures == 0 ? 0 : 1;
}
