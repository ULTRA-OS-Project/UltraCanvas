// Apps/DemoApp/UltraCanvasDemoPageKit.h
// The one look every demo page shares - the Level Meter page's: a light page
// with a bold title and a grey description, the examples in white rounded
// cards with a heading, captions and notes in grey.
//
// Everything here sizes to its text. No label gets a fixed width or height:
// a column stretches it across and it is as tall as its wrapped lines, so a
// longer text - another language, a larger font - makes the card taller
// instead of being cut. Rows wrap, so a card that no longer fits beside the
// others moves under them. The page is as tall as its content, and the demo's
// page area scrolls it (DisplayDemoItem); a page that should fill the area
// and scroll inside itself instead says so with SetFillsArea(true).
//
// Usage:
//   auto page = DemoKit::CreatePage("ToolbarPage", "Toolbar",
//                                   "Buttons, dropdowns, separators and spacers");
//   auto row  = DemoKit::AddRow(page, "ToolbarRow");
//   auto card = DemoKit::AddCard(row, "ToolbarStandard", "Standard toolbar",
//                                "Buttons, separators and flexible spacing");
//   DemoKit::AddFixed(card, toolbar);            // keeps the size it was built with
//   DemoKit::AddNotes(page, "ToolbarNotes", "Implementation notes",
//                     {"UltraCanvasToolbarBuilder builds it fluently", "..."});
//   return page;
//
// Version: 1.1.0 - ApplyContentMinWidth: nothing narrower than its fixed content
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace DemoKit {

    // ===== COLOURS =====
    inline Color PageBackground() { return Color(248, 249, 252, 255); }
    inline Color CardBackground() { return Color(255, 255, 255, 255); }
    inline Color CardBorder()     { return Color(224, 227, 235, 255); }
    inline Color TitleText()      { return Color(30, 41, 59, 255); }
    inline Color BodyText()       { return Color(51, 65, 85, 255); }
    inline Color SubtitleText()   { return Color(100, 110, 125, 255); }
    inline Color CaptionText()    { return Color(130, 138, 152, 255); }
    inline Color LinkText()       { return Color(37, 99, 235, 255); }

    // ===== SPACING =====
    constexpr float kPagePadding = 16.0f;
    constexpr float kPageGap     = 12.0f;
    constexpr float kCardPadding = 14.0f;
    constexpr float kCardGap     = 6.0f;
    constexpr float kCardRadius  = 10.0f;
    constexpr float kRowGap      = 12.0f;
    // The width a card in a row asks for before the rest is shared out; below
    // it the card wraps onto the next line.
    constexpr float kCardMinWidth = 340.0f;

    // A demo page. DisplayDemoItem recognises it: as tall as its content and
    // scrolled by the page area, or - SetFillsArea(true) - exactly as tall as
    // the area, for a page whose content scrolls itself (an editor, a list).
    class DemoPage : public UltraCanvasContainer {
    public:
        explicit DemoPage(const std::string& identifier) : UltraCanvasContainer(identifier) {}
        void SetFillsArea(bool fills) { fillsArea = fills; }
        bool FillsArea() const { return fillsArea; }

        // The min-widths ApplyContentMinWidth gave the page and its cards,
        // with what each had before, so they can be worked out again.
        std::vector<std::pair<UltraCanvasUIElement*, CSSLayout::Dimension>> kitMinWidths;

        // A button grows to its text only when it is first measured, after
        // the min-widths were worked out; the first layouts work them out
        // again and lay the page out anew when one changed.
        void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override;
        // The texts changed (another language): work the min-widths out
        // again on the next layouts.
        void RecheckMinWidths() { minWidthPasses = 0; InvalidateLayout(); RequestRedraw(); }
    private:
        bool fillsArea = false;
        int minWidthPasses = 0;
        // Expires with the page: a re-layout posted for later checks it.
        std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    };

    // ===== PAGE =====
    // The page: title and description at the top, then whatever is added.
    // An empty subtitle leaves the description out.
    std::shared_ptr<DemoPage> CreatePage(const std::string& id, const std::string& title,
                                         const std::string& subtitle);

    // ===== TEXT =====
    // Every label wraps and has no size of its own; added to a column it
    // takes the column's width and the height of its lines.
    std::shared_ptr<UltraCanvasLabel> CreateTitle(const std::string& id, const std::string& text);      // 18 bold
    std::shared_ptr<UltraCanvasLabel> CreateSubtitle(const std::string& id, const std::string& text);   // 11 grey
    std::shared_ptr<UltraCanvasLabel> CreateHeading(const std::string& id, const std::string& text);    // 13 bold: a card's title
    std::shared_ptr<UltraCanvasLabel> CreateText(const std::string& id, const std::string& text);       // 12 body text
    std::shared_ptr<UltraCanvasLabel> CreateCaption(const std::string& id, const std::string& text);    // 10 grey: under or over a control
    // One "• item" line per entry.
    std::shared_ptr<UltraCanvasLabel> CreateBullets(const std::string& id, const std::vector<std::string>& items);

    // ===== CONTAINERS =====
    // A white rounded card, a column. The title and the description are
    // left out when empty.
    std::shared_ptr<UltraCanvasContainer> CreateCard(const std::string& id, const std::string& title = "",
                                                     const std::string& description = "");
    // A plain row that wraps its items onto further lines when they do not fit.
    std::shared_ptr<UltraCanvasContainer> CreateRow(const std::string& id, float gap = kRowGap, bool wrap = true);
    // A plain column, its items stretched across.
    std::shared_ptr<UltraCanvasContainer> CreateColumn(const std::string& id, float gap = kCardGap);

    // ===== ADDING =====
    // Every Add puts the child in the flow: an element built with an (x, y)
    // origin is absolutely placed, which a flex container leaves where it
    // was; the Add functions drop that position.
    //
    // To a column: stretched across, as tall as it needs (grow > 0: shares
    // out the column's spare height). To a row: as wide as it needs (grow > 0:
    // shares out the row's spare width).
    void Add(const std::shared_ptr<UltraCanvasContainer>& parent,
             const std::shared_ptr<UltraCanvasUIElement>& child, float grow = 0.0f);
    // Keeps the size the element was built with - a chart, a canvas, a
    // control of a set size - at the start of the line, never squeezed.
    void AddFixed(const std::shared_ptr<UltraCanvasContainer>& parent,
                  const std::shared_ptr<UltraCanvasUIElement>& child);
    // Drops a fixed width the element was built with and stretches it across
    // the column (a toolbar, a progress bar, an input built 960 px wide to
    // fill the old page); its height stays.
    void AddStretched(const std::shared_ptr<UltraCanvasContainer>& parent,
                      const std::shared_ptr<UltraCanvasUIElement>& child);
    // Puts an element back in the flow (what every Add does first).
    void MakeInFlow(const std::shared_ptr<UltraCanvasUIElement>& element);

    // Makes the page, and every card, row and column on the way to a
    // fixed-size element (a set pixel width or min-width), at least as wide
    // as that element plus the padding and borders around it; returns the
    // page's. DisplayDemoItem calls it: a card sharing a row then wraps
    // rather than cutting the chart in it, and a window narrower than a
    // 1000 px chart scrolls the page sideways. A container that scrolls
    // sideways itself keeps what it holds.
    float ApplyContentMinWidth(DemoPage& page);

    // Creates and adds in one step; each returns what it added.
    std::shared_ptr<UltraCanvasContainer> AddRow(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                 const std::string& id, float gap = kRowGap);
    // To a row the card shares the width, at least minWidth before it wraps;
    // to a column it is stretched across.
    std::shared_ptr<UltraCanvasContainer> AddCard(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                  const std::string& id, const std::string& title = "",
                                                  const std::string& description = "",
                                                  float minWidth = kCardMinWidth);
    std::shared_ptr<UltraCanvasLabel> AddText(const std::shared_ptr<UltraCanvasContainer>& parent,
                                              const std::string& id, const std::string& text);
    std::shared_ptr<UltraCanvasLabel> AddCaption(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                 const std::string& id, const std::string& text);
    std::shared_ptr<UltraCanvasLabel> AddHeading(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                 const std::string& id, const std::string& text);
    // A card with a heading and a bulleted list - the "Implementation notes"
    // / "Features" box most pages end with.
    std::shared_ptr<UltraCanvasContainer> AddNotes(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                   const std::string& id, const std::string& title,
                                                   const std::vector<std::string>& items);

} // namespace DemoKit
} // namespace UltraCanvas
