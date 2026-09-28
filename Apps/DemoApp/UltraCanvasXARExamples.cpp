// Apps/DemoApp/UltraCanvasXARExamples.cpp
// Xara (.xar) vector graphics demo examples for UltraCanvas Framework
// Version: 1.3.0
// Last Modified: 2026-09-14
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasUtils.h"   // OpenURL — open the reference links in the system browser
#include "../Plugins/Vector/XAR/UltraCanvasXARPlugin.h"
#include <memory>
#include <string>

namespace UltraCanvas {

// ===== XAR VECTOR EXAMPLES IMPLEMENTATION =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateXARVectorExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("XARExamples", 0, 0, 1000, 1080);
        container->SetBackgroundColor(Color(245, 245, 245, 255));

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("XARTitle", 10, 10, 600, 30);
        title->SetText("Xara (.xar) Graphics Demo - Click to Open in the Media Viewer");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Description
        auto description = std::make_shared<UltraCanvasLabel>("XARDescription", 10, 45, 700, 40);
        description->SetText("Click a drawing to open it in the media viewer; zoom there to inspect details and use\nthe arrow keys to step through the folder. Press ESC to close the viewer.");
        description->SetFontSize(12);
        description->SetTextColor(Color(80, 80, 80, 255));
        container->AddChild(description);

        // Status label for feedback
        auto statusLabel = std::make_shared<UltraCanvasLabel>("XARStatus", 10, 1005, 980, 60);
        statusLabel->SetText("Ready. Click on a XAR file to view.");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        container->AddChild(statusLabel);

        // One tile per sample drawing in media/vector/XAR/
        auto makeTile = [&](const std::string& id, int x, int y,
                            const std::string& fileName) {
            auto tile = std::make_shared<UltraCanvasContainer>(id, x, y, 300, 280);
            tile->SetBackgroundColor(Colors::White);
            tile->SetBorders(2, Color(180, 180, 180, 255));

            auto element = std::make_shared<UltraCanvasXARElement>(id + "El", 10, 10, 280, 220);
            std::string path = NormalizePath(GetResourcesDir() + "media/vector/XAR/" + fileName);
            if (element->LoadFromFile(path)) {
                statusLabel->SetText("Loaded: " + path);
            } else {
                statusLabel->SetText("Failed to load " + path + ": " + element->GetLastError());
            }

            auto label = std::make_shared<UltraCanvasLabel>(id + "Label", 10, 240, 280, 30);
            label->SetText(fileName);
            label->SetAlignment(TextAlignment::Center);
            label->SetFontSize(11);
            tile->AddChild(label);

            element->SetMouseCursor(UCMouseCursor::Hand);
            element->SetEventCallback([tile = tile.get(), statusLabel, path](const UCEvent& event) {
                switch (event.type) {
                    case UCEventType::MouseUp:
                        ShowFullSizeImageViewer(path);
                        statusLabel->SetText("Opened in the media viewer: " + path);
                        return true;
                    case UCEventType::MouseEnter:
                        tile->SetBordersColor(Color(0, 122, 204, 255));
                        return true;
                    case UCEventType::MouseLeave:
                        tile->SetBordersColor(Color(180, 180, 180, 255));
                        return true;
                    default:
                        return false;
                }
            });

            tile->AddChild(element);
            container->AddChild(tile);
        };

        makeTile("XARContainer1", 20, 95, "Midget.xar");
        makeTile("XARContainer2", 340, 95, "Apple5.xar");
        makeTile("XARContainer3", 660, 95, "Backside.xar");

        // ===== INFO PANEL =====
        auto infoContainer = std::make_shared<UltraCanvasContainer>("InfoPanel", 20, 390, 470, 290);
        infoContainer->SetBackgroundColor(Color(240, 248, 255, 255));
        infoContainer->SetBorders(2, Color(100, 149, 237, 255));

        auto infoTitle = std::make_shared<UltraCanvasLabel>("InfoTitle", 10, 10, 450, 25);
        infoTitle->SetText("XAR Plugin Features");
        infoTitle->SetFontWeight(FontWeight::Bold);
        infoTitle->SetFontSize(13);
        infoContainer->AddChild(infoTitle);

        auto infoText = std::make_shared<UltraCanvasLabel>("InfoText", 10, 40, 450, 240);
        infoText->SetText(
                "✓ Xara .xar drawings (compressed too)\n"
                "✓ Paths, QuickShapes, groups, layers\n"
                "✓ Flat, gradient and bitmap fills\n"
                "✓ Contone fills and soft shadows\n"
                "✓ Text: fonts, justification, lists\n"
                "✓ Embedded bitmaps with transparency\n"
                "✓ Multi-page documents (spreads)\n"
                "✓ Opens in the media viewer\n"
        );
        infoText->SetFontSize(11);
        infoText->SetTextColor(Color(50, 50, 50, 255));
        infoContainer->AddChild(infoText);

        container->AddChild(infoContainer);

        // ===== HOW IT WORKS =====
        auto howContainer = std::make_shared<UltraCanvasContainer>("HowPanel", 510, 390, 470, 290);
        howContainer->SetBackgroundColor(Color(255, 250, 240, 255));
        howContainer->SetBorders(2, Color(222, 184, 135, 255));

        auto howTitle = std::make_shared<UltraCanvasLabel>("HowTitle", 10, 10, 450, 25);
        howTitle->SetText("The bundled drawings");
        howTitle->SetFontWeight(FontWeight::Bold);
        howTitle->SetFontSize(13);
        howContainer->AddChild(howTitle);

        auto howText = std::make_shared<UltraCanvasLabel>("HowText", 10, 40, 450, 240);
        howText->SetText(
                "Midget.xar - 842 x 595 px, 5,380 records, 577 nodes:\n"
                "524 paths over multi-stage linear and elliptical fills,\n"
                "7 text stories, 5 groups.\n\n"
                "Apple5.xar - 612 x 846 px, 7,175 records, 699 nodes:\n"
                "691 filled-and-stroked paths in 3 groups - flat fills and\n"
                "multi-stage gradients, no text and no bitmaps.\n\n"
                "Backside.xar - 842 x 1191 px, 3,898 records, 642 nodes:\n"
                "259 paths, 175 QuickShape polygons, 46 soft shadows and\n"
                "26 text stories across 44 groups - the widest feature mix.\n\n"
                "Every .xar in media/vector/XAR is read by UltraCanvasXARPlugin\n"
                "directly - no external tools and no conversion step.\n"
                "Tests/XARProbeTest prints these counts, the unhandled record\n"
                "tags and the parse warnings for each of them."
        );
        howText->SetFontSize(11);
        howText->SetTextColor(Color(50, 50, 50, 255));
        howContainer->AddChild(howText);

        container->AddChild(howContainer);

        // ===== WHERE THE FORMAT COMES FROM =====
        auto historyContainer = std::make_shared<UltraCanvasContainer>("HistoryPanel", 20, 690, 960, 300);
        historyContainer->SetBackgroundColor(Color(245, 245, 250, 255));
        historyContainer->SetBorders(2, Color(150, 150, 170, 255));

        auto historyTitle = std::make_shared<UltraCanvasLabel>("HistoryTitle", 10, 10, 940, 25);
        historyTitle->SetText("Where the format comes from");
        historyTitle->SetFontWeight(FontWeight::Bold);
        historyTitle->SetFontSize(13);
        historyContainer->AddChild(historyTitle);

        auto historyText = std::make_shared<UltraCanvasLabel>("HistoryText", 10, 40, 940, 160);
        historyText->SetText(
                "The Xara file format is the successor of the ArtWorks vector graphics file format.\n"
                "ArtWorks is a powerful vector graphics editor for RISC OS, the operating system of\n"
                "the 32-bit ARM machines - RISC OS was the first OS that ran on ARM CPUs.\n\n"
                "Two things set ArtWorks apart: its ultra-fast CPU-based vector graphics rendering,\n"
                "and its user-friendly user interface. It was later ported to Windows, where it got\n"
                "the name Xara."
        );
        historyText->SetFontSize(11);
        historyText->SetTextColor(Color(50, 50, 50, 255));
        historyContainer->AddChild(historyText);

        // Reference links - a click hands them to the system browser.
        auto makeLink = [&](const std::string& id, int y, const std::string& url) {
            auto link = std::make_shared<UltraCanvasLabel>(id, 10, y, 600, 18);
            link->SetTextIsMarkup(true);
            link->SetText("<span color=\"blue\" underline=\"single\">" + url + "</span>");
            link->SetFontSize(11);
            link->onClick = [url]() { OpenURL(url); };   // hand cursor comes with onClick
            historyContainer->AddChild(link);
        };
        makeLink("HistoryLinkArtWorks", 215, "https://en.wikipedia.org/wiki/ArtWorks");
        makeLink("HistoryLinkXara", 240, "https://en.wikipedia.org/wiki/Xara_Designer_Pro%2B");

        container->AddChild(historyContainer);

        return container;
    }

} // namespace UltraCanvas
