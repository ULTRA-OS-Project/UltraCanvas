// Apps/DemoApp/UltraCanvasScrollbarExamples.cpp
// Standalone scrollbar demonstration for the main demo app.
// Shows the variants achievable with the current UltraCanvasScrollbar element:
// preset styles, colour options, corner-radius / end-shape control and the
// custom image (SVG) handle feature.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  section, the bars in rows that wrap, every caption sized to
//                  its text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasScrollbar.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"

namespace UltraCanvas {

    // Fully-rounded ("round end") radius for a bar of the given track thickness.
    static int RoundEndRadius(int trackSize) { return trackSize / 2; }

    // Adds a vertical scrollbar with a caption underneath, as one cell of a
    // row (at least the old 110 px column, wider for a longer caption). The bar
    // is made scrollable (content > viewport) so the thumb is draggable and
    // the wheel works.
    static void AddVScrollbar(const std::shared_ptr<UltraCanvasContainer>& row,
                              const std::string& id,
                              const ScrollbarStyle& style, const std::string& caption,
                              const std::shared_ptr<UltraCanvasLabel>& status) {
        auto cell = DemoKit::CreateColumn(id + "_cell", 6);
        CSSLayout::BoxConstraints limits;
        limits.minWidth = CSSLayout::Dimension::Px(110);
        cell->boxConstraints = limits;
        DemoKit::Add(row, cell);

        const float barH = 150.0f;
        auto sb = std::make_shared<UltraCanvasScrollbar>(
                id, 0, 0, static_cast<float>(style.trackSize), barH,
                ScrollbarOrientation::Vertical);
        sb->SetStyle(style);
        sb->SetScrollDimensions(static_cast<int>(barH), 460);  // scrollable
        sb->SetScrollPosition(120);
        if (status) {
            sb->onScrollChange = [status, caption](int pos) {
                status->SetText(caption + ": position " + std::to_string(pos));
            };
        }
        DemoKit::AddFixed(cell, sb);
        sb->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);

        auto cap = DemoKit::AddCaption(cell, id + "_cap", caption);
        cap->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);
    }

    // Adds a horizontal scrollbar with a caption to its right (under it when
    // the line is too narrow for both).
    static void AddHScrollbar(const std::shared_ptr<UltraCanvasContainer>& parent,
                              const std::string& id, float width,
                              const ScrollbarStyle& style, const std::string& caption,
                              const std::shared_ptr<UltraCanvasLabel>& status) {
        auto cell = DemoKit::CreateRow(id + "_cell", 12);
        cell->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        DemoKit::Add(parent, cell);

        auto sb = std::make_shared<UltraCanvasScrollbar>(
                id, 0, 0, width, static_cast<float>(style.trackSize),
                ScrollbarOrientation::Horizontal);
        sb->SetStyle(style);
        sb->SetScrollDimensions(static_cast<int>(width), static_cast<int>(width * 2.5f));
        sb->SetScrollPosition(80);
        if (status) {
            sb->onScrollChange = [status, caption](int pos) {
                status->SetText(caption + ": position " + std::to_string(pos));
            };
        }
        DemoKit::AddFixed(cell, sb);
        sb->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);

        auto cap = DemoKit::CreateText(id + "_cap", caption);
        cap->SetFontSize(11);
        cap->SetTextColor(Color(90, 90, 90, 255));
        DemoKit::Add(cell, cap);
    }

    // A coloured, round-ended vertical style built from the Default() base.
    static ScrollbarStyle ColorStyle(Color track, Color thumb, Color hover, Color pressed) {
        ScrollbarStyle s;                       // trackSize 16
        s.trackColor = track;
        s.thumbColor = thumb;
        s.thumbHoverColor = hover;
        s.thumbPressedColor = pressed;
        s.thumbCornerRadius = RoundEndRadius(s.trackSize);
        s.trackCornerRadius = RoundEndRadius(s.trackSize);
        return s;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateScrollbarExamples() {
        auto root = DemoKit::CreatePage("ScrollbarExamples", "UltraCanvas Scrollbar Showcase",
                "Preset styles, colour options, corner-radius / end-shape control and a "
                "custom SVG handle. Drag a thumb or use the mouse wheel over a bar.");

        auto status = DemoKit::AddText(root, "ScrollbarStatus",
                "Interact with any scrollbar below to see its live position here.");
        status->SetFontSize(11);
        status->SetBackgroundColor(Color(245, 245, 245, 255));
        status->SetBorders(1.0f, Color(220, 220, 220, 255));
        status->SetPadding(6.0f);

        // ===== 1. PRESET STYLES =====
        {
            auto card = DemoKit::AddCard(root, "hdrPresets",
                    "1. Preset styles  —  ScrollbarStyle::Default / Modern / Minimal / Classic");
            auto row = DemoKit::AddRow(card, "rowPresets", 10);
            AddVScrollbar(row, "sbDefault", ScrollbarStyle::Default(), "Default", status);
            AddVScrollbar(row, "sbModern", ScrollbarStyle::Modern(), "Modern", status);
            AddVScrollbar(row, "sbMinimal", ScrollbarStyle::Minimal(), "Minimal", status);
            AddVScrollbar(row, "sbClassic", ScrollbarStyle::Classic(), "Classic (arrows)", status);
            AddVScrollbar(row, "sbDropdown", ScrollbarStyle::DropDown(), "DropDown", status);
        }

        // ===== 2. COLOUR OPTIONS =====
        {
            auto card = DemoKit::AddCard(root, "hdrColors",
                    "2. Colour options  —  track + thumb (normal / hover / pressed)");
            auto row = DemoKit::AddRow(card, "rowColors", 10);
            AddVScrollbar(row, "sbBlue",
                          ColorStyle(Color(225, 232, 245, 255), Color(90, 140, 220, 255),
                                     Color(70, 120, 200, 255), Color(50, 100, 180, 255)),
                          "Blue", status);
            AddVScrollbar(row, "sbGreen",
                          ColorStyle(Color(228, 244, 232, 255), Color(80, 180, 120, 255),
                                     Color(60, 160, 100, 255), Color(40, 140, 80, 255)),
                          "Green", status);
            AddVScrollbar(row, "sbAmber",
                          ColorStyle(Color(248, 240, 224, 255), Color(235, 170, 60, 255),
                                     Color(215, 150, 40, 255), Color(195, 130, 20, 255)),
                          "Amber", status);
            AddVScrollbar(row, "sbRose",
                          ColorStyle(Color(248, 228, 235, 255), Color(220, 90, 130, 255),
                                     Color(200, 70, 110, 255), Color(180, 50, 90, 255)),
                          "Rose", status);
            AddVScrollbar(row, "sbDark",
                          ColorStyle(Color(60, 60, 70, 255), Color(150, 150, 170, 255),
                                     Color(180, 180, 200, 255), Color(210, 210, 225, 255)),
                          "Dark", status);
        }

        // ===== 3. CORNER RADIUS / END SHAPE =====
        {
            auto card = DemoKit::AddCard(root, "hdrShape",
                    "3. Corner radius / end shape  —  square, soft and round (pill) ends");
            auto row = DemoKit::AddRow(card, "rowShape", 10);
            auto mk = [](int thumbR, int trackR) {
                ScrollbarStyle s;               // trackSize 16
                s.thumbColor = Color(150, 160, 175, 255);
                s.thumbHoverColor = Color(120, 130, 150, 255);
                s.thumbCornerRadius = thumbR;
                s.trackCornerRadius = trackR;
                return s;
            };
            AddVScrollbar(row, "sbSquare", mk(0, 0),
                          "Square (r=0)", status);
            AddVScrollbar(row, "sbSoft", mk(3, 3),
                          "Soft (r=3)", status);
            AddVScrollbar(row, "sbPill", mk(8, 8),
                          "Round/Pill (r=8)", status);
            // Pill thumb riding a flat (square) track.
            AddVScrollbar(row, "sbPillFlat", mk(8, 0),
                          "Pill on flat track", status);
        }

        // ===== 4. CUSTOM HANDLE (SVG IMAGE) =====
        {
            auto card = DemoKit::AddCard(root, "hdrHandle",
                    "4. Custom handle  —  thumb drawn from an SVG image (riffled grip)");
            auto row = DemoKit::AddRow(card, "rowHandle", 10);
            const std::string vHandle =
                    NormalizePath(GetResourcesDir() + "media/icons/scrollbar-handle-v.svg");
            const std::string hHandle =
                    NormalizePath(GetResourcesDir() + "media/icons/scrollbar-handle-h.svg");

            ScrollbarStyle handleV;
            handleV.trackSize = 14;
            handleV.thumbMinSize = 44;
            handleV.trackColor = Color(238, 240, 243, 255);
            handleV.trackCornerRadius = 7;
            handleV.thumbImagePath = vHandle;     // <-- SVG handle
            AddVScrollbar(row, "sbHandleV", handleV,
                          "SVG handle", status);

            // Same handle on a wider bar to show it scales.
            ScrollbarStyle handleVWide = handleV;
            handleVWide.trackSize = 20;
            handleVWide.thumbMinSize = 52;
            AddVScrollbar(row, "sbHandleVWide", handleVWide,
                          "SVG handle (wide)", status);

            // Horizontal handle.
            ScrollbarStyle handleH;
            handleH.trackSize = 14;
            handleH.thumbMinSize = 44;
            handleH.trackColor = Color(238, 240, 243, 255);
            handleH.trackCornerRadius = 7;
            handleH.thumbImagePath = hHandle;
            AddHScrollbar(row, "sbHandleH", 240.0f,
                          handleH, "Horizontal SVG handle", status);
        }

        // ===== 5. HORIZONTAL SCROLLBARS =====
        {
            auto card = DemoKit::AddCard(root, "hdrHoriz",
                    "5. Horizontal orientation  —  default and round-ended");
            ScrollbarStyle flat = ScrollbarStyle::Default();
            AddHScrollbar(card, "sbHDefault", 300.0f, flat, "Default", status);

            ScrollbarStyle round = ScrollbarStyle::Default();
            round.thumbColor = Color(90, 140, 220, 255);
            round.thumbCornerRadius = RoundEndRadius(round.trackSize);
            round.trackCornerRadius = RoundEndRadius(round.trackSize);
            AddHScrollbar(card, "sbHRound", 300.0f, round, "Round ends", status);
        }

        return root;
    }

} // namespace UltraCanvas
