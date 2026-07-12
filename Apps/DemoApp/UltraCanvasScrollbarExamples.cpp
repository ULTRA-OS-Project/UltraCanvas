// Apps/DemoApp/UltraCanvasScrollbarExamples.cpp
// Standalone scrollbar demonstration for the main demo app.
// Shows the variants achievable with the current UltraCanvasScrollbar element:
// preset styles, colour options, corner-radius / end-shape control and the
// custom image (SVG) handle feature.
// Version: 1.0.0
// Last Modified: 2026-06-20
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasScrollbar.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"

namespace UltraCanvas {

    // Fully-rounded ("round end") radius for a bar of the given track thickness.
    static int RoundEndRadius(int trackSize) { return trackSize / 2; }

    // Section header label.
    static std::shared_ptr<UltraCanvasLabel> SectionHeader(const std::string& id,
                                                           float x, float y,
                                                           const std::string& text) {
        auto lbl = std::make_shared<UltraCanvasLabel>(id, x, y, 900, 22);
        lbl->SetText(text);
        lbl->SetFontSize(13);
        lbl->SetFontWeight(FontWeight::Bold);
        lbl->SetTextColor(Color(50, 50, 120, 255));
        return lbl;
    }

    // Adds a vertical scrollbar with a caption underneath. The bar is made
    // scrollable (content > viewport) so the thumb is draggable and the wheel works.
    static void AddVScrollbar(const std::shared_ptr<UltraCanvasContainer>& parent,
                              const std::string& id, float x, float y,
                              const ScrollbarStyle& style, const std::string& caption,
                              const std::shared_ptr<UltraCanvasLabel>& status) {
        const float barH = 150.0f;
        auto sb = std::make_shared<UltraCanvasScrollbar>(
                id, x, y, static_cast<float>(style.trackSize), barH,
                ScrollbarOrientation::Vertical);
        sb->SetStyle(style);
        sb->SetScrollDimensions(static_cast<int>(barH), 460);  // scrollable
        sb->SetScrollPosition(120);
        if (status) {
            sb->onScrollChange = [status, caption](int pos) {
                status->SetText(caption + " → position " + std::to_string(pos));
            };
        }
        parent->AddChild(sb);

        auto cap = std::make_shared<UltraCanvasLabel>(
                id + "_cap", x + style.trackSize / 2.0f - 55.0f, y + barH + 6.0f, 110, 30);
        cap->SetText(caption);
        cap->SetFontSize(10);
        cap->SetAlignment(TextAlignment::Center);
        cap->SetTextColor(Color(90, 90, 90, 255));
        parent->AddChild(cap);
    }

    // Adds a horizontal scrollbar with a caption to its right.
    static void AddHScrollbar(const std::shared_ptr<UltraCanvasContainer>& parent,
                              const std::string& id, float x, float y, float width,
                              const ScrollbarStyle& style, const std::string& caption,
                              const std::shared_ptr<UltraCanvasLabel>& status) {
        auto sb = std::make_shared<UltraCanvasScrollbar>(
                id, x, y, width, static_cast<float>(style.trackSize),
                ScrollbarOrientation::Horizontal);
        sb->SetStyle(style);
        sb->SetScrollDimensions(static_cast<int>(width), static_cast<int>(width * 2.5f));
        sb->SetScrollPosition(80);
        if (status) {
            sb->onScrollChange = [status, caption](int pos) {
                status->SetText(caption + " → position " + std::to_string(pos));
            };
        }
        parent->AddChild(sb);

        auto cap = std::make_shared<UltraCanvasLabel>(
                id + "_cap", x + width + 12.0f, y - 4.0f, 220, 24);
        cap->SetText(caption);
        cap->SetFontSize(11);
        cap->SetTextColor(Color(90, 90, 90, 255));
        parent->AddChild(cap);
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
        auto root = std::make_shared<UltraCanvasContainer>("ScrollbarExamples", 0, 0, 1000, 1220);

        // ===== PAGE TITLE =====
        auto title = std::make_shared<UltraCanvasLabel>("ScrollbarTitle", 20, 10, 760, 32);
        title->SetText("UltraCanvas Scrollbar Showcase");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        root->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("ScrollbarSubtitle", 20, 44, 940, 24);
        subtitle->SetText("Preset styles, colour options, corner-radius / end-shape control and a "
                          "custom SVG handle. Drag a thumb or use the mouse wheel over a bar.");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(100, 100, 100, 255));
        root->AddChild(subtitle);

        auto status = std::make_shared<UltraCanvasLabel>("ScrollbarStatus", 20, 74, 940, 24);
        status->SetText("Interact with any scrollbar below to see its live position here.");
        status->SetFontSize(11);
        status->SetBackgroundColor(Color(245, 245, 245, 255));
        status->SetBorders(1.0f, Color(220, 220, 220, 255));
        status->SetPadding(6.0f);
        root->AddChild(status);

        const float colGap = 120.0f;
        const float col0 = 60.0f;

        // ===== 1. PRESET STYLES =====
        root->AddChild(SectionHeader("hdrPresets", 20, 112,
                                     "1. Preset styles  —  ScrollbarStyle::Default / Modern / Minimal / Classic"));
        {
            float y = 140.0f;
            AddVScrollbar(root, "sbDefault", col0 + colGap * 0, y,
                          ScrollbarStyle::Default(), "Default", status);
            AddVScrollbar(root, "sbModern", col0 + colGap * 1, y,
                          ScrollbarStyle::Modern(), "Modern", status);
            AddVScrollbar(root, "sbMinimal", col0 + colGap * 2, y,
                          ScrollbarStyle::Minimal(), "Minimal", status);
            AddVScrollbar(root, "sbClassic", col0 + colGap * 3, y,
                          ScrollbarStyle::Classic(), "Classic (arrows)", status);
            AddVScrollbar(root, "sbDropdown", col0 + colGap * 4, y,
                          ScrollbarStyle::DropDown(), "DropDown", status);
        }

        // ===== 2. COLOUR OPTIONS =====
        root->AddChild(SectionHeader("hdrColors", 20, 352,
                                     "2. Colour options  —  track + thumb (normal / hover / pressed)"));
        {
            float y = 380.0f;
            AddVScrollbar(root, "sbBlue", col0 + colGap * 0, y,
                          ColorStyle(Color(225, 232, 245, 255), Color(90, 140, 220, 255),
                                     Color(70, 120, 200, 255), Color(50, 100, 180, 255)),
                          "Blue", status);
            AddVScrollbar(root, "sbGreen", col0 + colGap * 1, y,
                          ColorStyle(Color(228, 244, 232, 255), Color(80, 180, 120, 255),
                                     Color(60, 160, 100, 255), Color(40, 140, 80, 255)),
                          "Green", status);
            AddVScrollbar(root, "sbAmber", col0 + colGap * 2, y,
                          ColorStyle(Color(248, 240, 224, 255), Color(235, 170, 60, 255),
                                     Color(215, 150, 40, 255), Color(195, 130, 20, 255)),
                          "Amber", status);
            AddVScrollbar(root, "sbRose", col0 + colGap * 3, y,
                          ColorStyle(Color(248, 228, 235, 255), Color(220, 90, 130, 255),
                                     Color(200, 70, 110, 255), Color(180, 50, 90, 255)),
                          "Rose", status);
            AddVScrollbar(root, "sbDark", col0 + colGap * 4, y,
                          ColorStyle(Color(60, 60, 70, 255), Color(150, 150, 170, 255),
                                     Color(180, 180, 200, 255), Color(210, 210, 225, 255)),
                          "Dark", status);
        }

        // ===== 3. CORNER RADIUS / END SHAPE =====
        root->AddChild(SectionHeader("hdrShape", 20, 592,
                                     "3. Corner radius / end shape  —  square → soft → round (pill) ends"));
        {
            float y = 620.0f;
            auto mk = [](int thumbR, int trackR) {
                ScrollbarStyle s;               // trackSize 16
                s.thumbColor = Color(150, 160, 175, 255);
                s.thumbHoverColor = Color(120, 130, 150, 255);
                s.thumbCornerRadius = thumbR;
                s.trackCornerRadius = trackR;
                return s;
            };
            AddVScrollbar(root, "sbSquare", col0 + colGap * 0, y, mk(0, 0),
                          "Square (r=0)", status);
            AddVScrollbar(root, "sbSoft", col0 + colGap * 1, y, mk(3, 3),
                          "Soft (r=3)", status);
            AddVScrollbar(root, "sbPill", col0 + colGap * 2, y, mk(8, 8),
                          "Round/Pill (r=8)", status);
            // Pill thumb riding a flat (square) track.
            AddVScrollbar(root, "sbPillFlat", col0 + colGap * 3, y, mk(8, 0),
                          "Pill on flat track", status);
        }

        // ===== 4. CUSTOM HANDLE (SVG IMAGE) =====
        root->AddChild(SectionHeader("hdrHandle", 20, 832,
                                     "4. Custom handle  —  thumb drawn from an SVG image (riffled grip)"));
        {
            const std::string vHandle =
                    NormalizePath(GetResourcesDir() + "media/icons/scrollbar-handle-v.svg");
            const std::string hHandle =
                    NormalizePath(GetResourcesDir() + "media/icons/scrollbar-handle-h.svg");

            float y = 862.0f;

            ScrollbarStyle handleV;
            handleV.trackSize = 14;
            handleV.thumbMinSize = 44;
            handleV.trackColor = Color(238, 240, 243, 255);
            handleV.trackCornerRadius = 7;
            handleV.thumbImagePath = vHandle;     // <-- SVG handle
            AddVScrollbar(root, "sbHandleV", col0 + colGap * 0, y, handleV,
                          "SVG handle", status);

            // Same handle on a wider bar to show it scales.
            ScrollbarStyle handleVWide = handleV;
            handleVWide.trackSize = 20;
            handleVWide.thumbMinSize = 52;
            AddVScrollbar(root, "sbHandleVWide", col0 + colGap * 1, y, handleVWide,
                          "SVG handle (wide)", status);

            // Horizontal handle.
            ScrollbarStyle handleH;
            handleH.trackSize = 14;
            handleH.thumbMinSize = 44;
            handleH.trackColor = Color(238, 240, 243, 255);
            handleH.trackCornerRadius = 7;
            handleH.thumbImagePath = hHandle;
            AddHScrollbar(root, "sbHandleH", col0 + colGap * 2, y + 20.0f, 240.0f,
                          handleH, "Horizontal SVG handle", status);
        }

        // ===== 5. HORIZONTAL SCROLLBARS =====
        root->AddChild(SectionHeader("hdrHoriz", 20, 1062,
                                     "5. Horizontal orientation  —  default and round-ended"));
        {
            ScrollbarStyle flat = ScrollbarStyle::Default();
            AddHScrollbar(root, "sbHDefault", 60, 1094, 300.0f, flat, "Default", status);

            ScrollbarStyle round = ScrollbarStyle::Default();
            round.thumbColor = Color(90, 140, 220, 255);
            round.thumbCornerRadius = RoundEndRadius(round.trackSize);
            round.trackCornerRadius = RoundEndRadius(round.trackSize);
            AddHScrollbar(root, "sbHRound", 60, 1130, 300.0f, round, "Round ends", status);
        }

        return root;
    }

} // namespace UltraCanvas
