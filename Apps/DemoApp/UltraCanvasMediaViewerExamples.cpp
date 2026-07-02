// Apps/DemoApp/UltraCanvasMediaViewerExamples.cpp
// Demonstration of UltraCanvasMediaViewer: the comprehensive media / photo /
// document viewer widget. The live widget is opened on the media/images folder
// so it shows the first image immediately; the toolbar drives navigation,
// slideshow, zoom, rotation, mirroring, colour adjustments, save-as and the
// info popup. The widget is framed and captioned to separate it from the
// programmer-facing notes below it.
// Version: 1.0.0
// Last Modified: 2026-06-26
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMediaViewerExamples() {
        auto root = std::make_shared<UltraCanvasContainer>("MediaViewerExamples", 0, 0, 1000, 900);
        root->SetPadding(0, 5, 5, 0);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("MediaViewerTitle", 20, 10, 900, 30);
        title->SetText("UltraCanvas Media Viewer widget");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        root->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("MediaViewerSubtitle", 20, 42, 940, 36);
        subtitle->SetText("The framed area below is the live widget, opened on the media/images "
                          "folder. Use the toolbar or click the image / arrow keys to browse "
                          "(left = next, right = previous, works while zoomed). Try the "
                          "slideshow, zoom, rotate, mirror, the Adjust panel, Save as and Info.");
        subtitle->SetFontSize(11);
        subtitle->SetTextColor(Color(110, 110, 110, 255));
        subtitle->SetWrap(TextWrap::WrapWord);
        root->AddChild(subtitle);

        // ---- Frame + caption around the live widget (drawn behind it). ----
        auto widgetFrame = std::make_shared<UltraCanvasUIElement>(
                "MediaViewerWidgetFrame", 12, 90, 956, 704);
        widgetFrame->SetBackgroundColor(Colors::Transparent);
        widgetFrame->SetBorders(1.5f, Color(120, 120, 130, 255), 6.0f);
        root->AddChild(widgetFrame);

        // ---- The live media viewer widget. ----
        auto viewer = CreateMediaViewer("ultracanvas-mediaviewer", 20, 98, 940, 688);

        // Point it at the media/images folder; the first (sorted) image shows.
        const std::string imagesDir = NormalizePath(GetResourcesDir() + "media/images");
        viewer->OpenFolder(imagesDir);

        root->AddChild(viewer);

        auto widgetCaption = std::make_shared<UltraCanvasLabel>(
                "MediaViewerWidgetCaption", 26, 80, 180, 20);
        widgetCaption->SetText("  Media Viewer widget  ");
        widgetCaption->SetFontSize(13);
        widgetCaption->SetFontWeight(FontWeight::Bold);
        widgetCaption->SetTextColor(Color(40, 40, 60, 255));
        widgetCaption->SetBackgroundColor(Color(255, 255, 255, 255));
        root->AddChild(widgetCaption);

        // Programmer-facing note.
        auto note = std::make_shared<UltraCanvasLabel>("MediaViewerNote", 20, 806, 940, 60);
        note->SetText("Images load/save through UCImage and UltraCanvasFileLoader, with rotation, "
                      "mirror, gamma, brightness, RGB correction, auto-optimisation and sharpening "
                      "via PixelFX. PDFs render through UltraCanvasPDFView and audio/video through "
                      "the player elements. Drag a folder onto the widget to browse it, or drag "
                      "files (images, PDFs, audio or video) to view them.");
        note->SetFontSize(11);
        note->SetTextColor(Color(120, 120, 120, 255));
        note->SetWrap(TextWrap::WrapWord);
        root->AddChild(note);

        return root;
    }

} // namespace UltraCanvas
