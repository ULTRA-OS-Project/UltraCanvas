// Apps/DemoApp/UltraCanvasXARExamples.cpp
// Xara (.xar) vector graphics demo examples for UltraCanvas Framework
// Version: 1.4.0 - the shared page look (UltraCanvasDemoPageKit): the drawings as
//                  tiles in a wrapping row, the notes and the history as cards,
//                  every text sized to fit and wrapped
// Version: 1.3.1
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
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
        auto container = DemoKit::CreatePage("XARExamples",
                "Xara (.xar) Graphics Demo - Click to Open in the Media Viewer",
                "Click a drawing to open it in the media viewer; zoom there to inspect details and use "
                "the arrow keys to step through the folder. Press ESC to close the viewer.");

        // Status label for feedback; the tiles report into it as they load,
        // and it sits under the panels.
        auto statusLabel = DemoKit::CreateText("XARStatus", "Ready. Click on a XAR file to view.");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        statusLabel->SetPadding(8);

        // One tile per sample drawing in media/vector/XAR/, side by side
        // while they fit.
        auto tileRow = DemoKit::AddRow(container, "XARTiles");
        auto makeTile = [&](const std::string& id, const std::string& fileName) {
            // The drawing over its file name; the border lights up on hover.
            auto tile = DemoKit::CreateColumn(id, 8);
            tile->SetPadding(10);
            tile->SetBackgroundColor(Colors::White);
            tile->SetBorders(2, Color(180, 180, 180, 255));

            auto element = std::make_shared<UltraCanvasXARElement>(id + "El", 0, 0, 280, 220);
            std::string path = NormalizePath(GetResourcesDir() + "media/vector/XAR/" + fileName);
            if (element->LoadFromFile(path)) {
                statusLabel->SetText("Loaded: " + path);
            } else {
                statusLabel->SetText("Failed to load " + path + ": " + element->GetLastError());
            }

            element->SetMouseCursor(UCMouseCursor::Hand);
            element->SetEventCallback([tile = tile.get(), statusLabel, path](const UCEvent& event) {
                switch (event.type) {
                    case UCEventType::MouseUp:
                        ShowInMediaViewer(path);
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
            DemoKit::AddFixed(tile, element);

            auto label = DemoKit::AddText(tile, id + "Label", fileName);
            label->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);
            label->SetFontSize(11);

            DemoKit::Add(tileRow, tile);
        };

        makeTile("XARContainer1", "Midget.xar");
        makeTile("XARContainer2", "Apple5.xar");
        makeTile("XARContainer3", "Backside.xar");

        auto panelRow = DemoKit::AddRow(container, "XARPanels");

        // ===== INFO PANEL =====
        auto infoContainer = DemoKit::AddCard(panelRow, "InfoPanel", "XAR Plugin Features");
        auto infoText = DemoKit::CreateBullets("InfoText", {
                "Xara .xar drawings (compressed too)",
                "Paths, QuickShapes, groups, layers",
                "Flat, gradient and bitmap fills",
                "Contone fills and soft shadows",
                "Text: fonts, justification, lists",
                "Embedded bitmaps with transparency",
                "Multi-page documents (spreads)",
                "Opens in the media viewer"});
        DemoKit::Add(infoContainer, infoText);

        // ===== HOW IT WORKS =====
        auto howContainer = DemoKit::AddCard(panelRow, "HowPanel", "The bundled drawings");
        auto howText = DemoKit::AddText(howContainer, "HowText",
                "Midget.xar - 842 x 595 px, 5,380 records, 577 nodes: "
                "524 paths over multi-stage linear and elliptical fills, "
                "7 text stories, 5 groups.\n\n"
                "Apple5.xar - 612 x 846 px, 7,175 records, 699 nodes: "
                "691 filled-and-stroked paths in 3 groups - flat fills and "
                "multi-stage gradients, no text and no bitmaps.\n\n"
                "Backside.xar - 842 x 1191 px, 3,898 records, 642 nodes: "
                "259 paths, 175 QuickShape polygons, 46 soft shadows and "
                "26 text stories across 44 groups - the widest feature mix.\n\n"
                "Every .xar in media/vector/XAR is read by UltraCanvasXARPlugin "
                "directly - no external tools and no conversion step. "
                "Tests/XARProbeTest prints these counts, the unhandled record "
                "tags and the parse warnings for each of them.");
        howText->SetFontSize(11);

        // ===== WHERE THE FORMAT COMES FROM =====
        auto historyContainer = DemoKit::AddCard(container, "HistoryPanel", "Where the format comes from");
        auto historyText = DemoKit::AddText(historyContainer, "HistoryText",
                "The Xara file format is the successor of the ArtWorks vector graphics file format. "
                "ArtWorks is a powerful vector graphics editor for RISC OS, the operating system of "
                "the 32-bit ARM machines - RISC OS was the first OS that ran on ARM CPUs.\n\n"
                "Two things set ArtWorks apart: its ultra-fast CPU-based vector graphics rendering, "
                "and its user-friendly user interface. It was later ported to Windows, where it got "
                "the name Xara.");
        historyText->SetFontSize(11);

        // Reference links - a click hands them to the system browser. Each is
        // as wide as its text, so only the text is clickable.
        auto makeLink = [&](const std::string& id, const std::string& url) {
            auto link = DemoKit::CreateText(id, "");
            link->SetTextIsMarkup(true);
            link->SetText("<span color=\"blue\" underline=\"single\">" + url + "</span>");
            link->SetFontSize(11);
            link->onClick = [url]() { OpenURL(url); };   // hand cursor comes with onClick
            DemoKit::Add(historyContainer, link);
            link->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);
        };
        makeLink("HistoryLinkArtWorks", "https://en.wikipedia.org/wiki/ArtWorks");
        makeLink("HistoryLinkXara", "https://en.wikipedia.org/wiki/Xara_Designer_Pro%2B");

        DemoKit::Add(container, statusLabel);

        return container;
    }

} // namespace UltraCanvas
