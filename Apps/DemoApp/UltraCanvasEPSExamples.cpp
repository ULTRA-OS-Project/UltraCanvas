// Apps/DemoApp/UltraCanvasEPSExamples.cpp
// EPS (Encapsulated PostScript) vector graphics demo examples
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): each drawing
//                  in a card, the features panel as a notes card beside them,
//                  the status line at the bottom sized to its text
// Version: 1.0.1
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"
#include "../Plugins/Vector/EPS/UltraCanvasEPSPlugin.h"
#include <memory>
#include <string>

namespace UltraCanvas {

// ===== EPS VECTOR EXAMPLES IMPLEMENTATION =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateEPSVectorExamples() {
        auto page = DemoKit::CreatePage("EPSExamples",
                "EPS (PostScript) Graphics Demo - Click to Open in the Media Viewer",
                "EPS files are PostScript programs, interpreted by the plugin's built-in interpreter.\nClick a drawing to open it in the media viewer; zoom there. Press ESC to close.");

        // Status label for feedback; the tiles write to it while they load,
        // and it goes in at the bottom of the page.
        auto statusLabel = DemoKit::CreateText("EPSStatus", "Ready. Click on an EPS file to view.");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        statusLabel->SetPadding(6);

        // One tile per sample drawing in media/vector/EPS/
        auto tiles = DemoKit::AddRow(page, "EPSTiles");
        auto makeTile = [&](const std::string& id, const std::string& fileName) {
            auto tile = DemoKit::AddCard(tiles, id);

            auto element = std::make_shared<UltraCanvasEPSElement>(id + "El", 0, 0, 280, 220);
            std::string path = NormalizePath(GetResourcesDir() + "media/vector/EPS/" + fileName);
            if (element->LoadFromFile(path)) {
                statusLabel->SetText("Loaded: " + path);
            } else {
                statusLabel->SetText("Failed to load " + path + ": " + element->GetLastError());
            }
            DemoKit::AddFixed(tile, element);

            auto label = DemoKit::AddText(tile, id + "Label", fileName);
            label->SetFontSize(11);

            // Hovering the drawing highlights its card; leaving gives the
            // card its own border back.
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
                        tile->SetBordersColor(DemoKit::CardBorder());
                        return true;
                    default:
                        return false;
                }
            });
        };

        // Every .eps file shipped under media/vector/EPS.
        makeTile("EPSContainer1", "demo.eps");
        makeTile("EPSContainer2", "gears.eps");

        // ===== INFO PANEL =====
        DemoKit::AddNotes(tiles, "InfoPanel", "EPS Plugin Features", {
                "PostScript interpreter (level 1/2 core)",
                "Procedures, loops, dictionaries",
                "Paths, arcs, beziers, clipping",
                "Gray / RGB / CMYK / HSB color",
                "Dashes, caps, joins, transforms",
                "Sampled images (hex data)",
                "Text via mapped system fonts",
                "DOS EPS binary preview headers"});

        DemoKit::Add(page, statusLabel);

        return page;
    }

} // namespace UltraCanvas
