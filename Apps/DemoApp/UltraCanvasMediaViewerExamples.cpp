// Apps/DemoApp/UltraCanvasMediaViewerExamples.cpp
// Demonstration of UltraCanvasMediaViewer: the comprehensive media / photo /
// document viewer widget. The live widget is opened on the media/images folder
// so it shows the first image immediately; the toolbar drives navigation,
// slideshow, zoom, rotation, mirroring, colour adjustments, save-as and the
// info popup. The widget is framed and captioned to separate it from the
// programmer-facing notes below it.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the frame and
//                  caption are a card, the viewer fills the visible area
// Version: 1.0.2 - ShowInMediaViewer lives here, built in every configuration
// Version: 1.0.1
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasMediaViewerWindow.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

// ===== FULL-SIZE VIEWER =====
    // Every demo page that opens a sample "full size" - bitmaps, vector
    // drawings, 3D models - comes here, and the file opens in one
    // UltraCanvasMediaViewerWindow over the window the click came from (the
    // demo's main window). The arrow keys walk the rest of the sample's
    // folder; Escape closes it. The instance is allocated once and never
    // destroyed: its destructor closes a window, which must not run during
    // static teardown after the windowing system has shut down. It lives in
    // this file because the vector and 3D pages call it too, and this file
    // is built in every configuration where UltraCanvasBitmapFormatDemo.cpp,
    // its old home, needs libvips (a build without it failed to link).
    void ShowInMediaViewer(const std::string& path) {
        if (path.empty()) return;
        static auto* viewerWindow = new UltraCanvasMediaViewerWindow();
        UltraCanvasWindowBase* host = nullptr;
        if (auto* app = UltraCanvasApplication::GetInstance()) host = app->GetFocusedWindow();
        viewerWindow->Show(path, host);
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMediaViewerExamples() {
        auto page = DemoKit::CreatePage("MediaViewerExamples", "UltraCanvas Media Viewer widget",
                "The framed area below is the live widget, opened on the media/images "
                "folder. The arrow keys browse the folder right away — no click "
                "needed (Left = previous, Right = next); clicking the picture does "
                "the same (left click = next, right click = previous) and works "
                "while zoomed. Try the slideshow, zoom, rotate, mirror, the Adjust "
                "panel, Save as and Info.");
        // The viewer scales and scrolls its picture itself: the page is the
        // area's height and the viewer takes what the texts leave.
        page->SetFillsArea(true);

        // The card is the frame and caption that separate the live widget
        // from the programmer-facing note below it.
        auto card = DemoKit::CreateCard("MediaViewerWidgetFrame", "Media Viewer widget");
        DemoKit::Add(page, card, 1);
        card->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexShrink(1);

        // ---- The live media viewer widget. ----
        auto viewer = CreateMediaViewer("ultracanvas-mediaviewer", 0, 0, 940, 688);

        // Point it at the media/images folder; the first (sorted) image shows.
        const std::string imagesDir = NormalizePath(GetResourcesDir() + "media/images");
        viewer->OpenFolder(imagesDir);

        // A zero flex basis: the viewer takes exactly the card's spare height,
        // never its 688 px build height, so the note below stays visible.
        viewer->size.width = CSSLayout::Dimension::Auto();
        DemoKit::Add(card, viewer, 1);
        viewer->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexShrink(1);

        // Programmer-facing note.
        DemoKit::AddCaption(page, "MediaViewerNote",
                "Images load/save through UCImage and UltraCanvasFileLoader, with rotation, "
                "mirror, gamma, brightness, RGB correction, auto-optimisation and sharpening "
                "via PixelFX. PDFs render through UltraCanvasPDFView and audio/video through "
                "the player elements. Drag a folder onto the widget to browse it, or drag "
                "files (images, PDFs, audio or video) to view them.");

        return page;
    }

} // namespace UltraCanvas
