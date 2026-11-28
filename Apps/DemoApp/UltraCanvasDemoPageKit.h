// Apps/DemoApp/UltraCanvasDemoPageKit.h
// The one look every demo page shares - the Level Meter page's: a light page
// with a bold title and a grey description, the examples in white rounded
// cards with a heading, captions and notes in grey. In a dark (or a
// high-contrast) theme the same parts take the theme's roles: the ground for
// the page, the panel for the cards, ink and muted ink for the text.
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
// Version: 1.4.0 - StyleTabs: every page's tabs in one look, underlined (the OpenGL showcase's)
// Version: 1.3.0 - Themed: a page's own decorative colour, as it is in the light look and with its
//                  lightness turned over (hue kept) in a dark one
// Version: 1.2.0 - the colours follow the theme: the light look as it was, the theme's roles in a
//                  dark or high-contrast one; KitContainer / KitLabel re-colour on a theme change
// Version: 1.1.0 - ApplyContentMinWidth: nothing narrower than its fixed content
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTheme.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraLocalisation/UltraLocalisation.h"   // Tr for the pages' texts

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace DemoKit {

    // ===== COLOURS =====
    // The kit's colours by part. In the plain light theme they are the kit's
    // own (the light look the pages were designed in); in a dark or a
    // high-contrast theme they are the current theme's roles, so a page is
    // dark when the application is.
    enum class KitColour { Page, Card, CardBorder, Title, Body, Subtitle, Caption, Link };
    Color ColourOf(KitColour part, const UltraCanvasTheme& theme);
    inline Color ColourOf(KitColour part) { return ColourOf(part, UltraCanvasTheme::Current()); }

    inline Color PageBackground() { return ColourOf(KitColour::Page); }
    inline Color CardBackground() { return ColourOf(KitColour::Card); }
    inline Color CardBorder()     { return ColourOf(KitColour::CardBorder); }
    inline Color TitleText()      { return ColourOf(KitColour::Title); }
    inline Color BodyText()       { return ColourOf(KitColour::Body); }
    inline Color SubtitleText()   { return ColourOf(KitColour::Subtitle); }
    inline Color CaptionText()    { return ColourOf(KitColour::Caption); }
    inline Color LinkText()       { return ColourOf(KitColour::Link); }

    // A colour a page chose for the light look - a pastel panel, a grey
    // caption, a tinted readout - in the current theme: unchanged in the
    // light look; in a dark one its lightness is turned over and its hue
    // and saturation kept, so a pale green panel becomes a dark green one
    // and dark grey text light grey. For decoration only: a chart's palette,
    // a colour swatch or a chart drawn on its own white paper keep their
    // colours. The demo rebuilds the page shown when the theme switches,
    // so a colour chosen when the page was built follows the switch.
    Color Themed(const Color& lightLookColour, const UltraCanvasTheme& theme);
    inline Color Themed(const Color& lightLookColour) { return Themed(lightLookColour, UltraCanvasTheme::Current()); }
    // For a colour known only when the page runs (a helper's parameter):
    // a pale fill (a panel, a pane) is Themed, a coloured or dark one - a
    // button, a badge, a deliberately dark panel - is kept; dark text is
    // Themed, light or coloured text (on a coloured fill) is kept.
    Color ThemedFill(const Color& lightLookFill);
    Color ThemedInk(const Color& lightLookText);

    // A container painted in a kit colour (a page, a card: with the card's
    // border), painted again when the theme changes while it is shown.
    class KitContainer : public UltraCanvasContainer {
    public:
        explicit KitContainer(const std::string& identifier) : UltraCanvasContainer(identifier) {}
        // The background part, and whether it has the card's rounded border
        void SetKitBackground(KitColour part, bool cardBorder = false);
        void OnThemeChanged(const UltraCanvasTheme& theme) override;
    private:
        void PaintKitColours(const UltraCanvasTheme& theme);
        bool hasKitBackground = false;
        KitColour background = KitColour::Page;
        bool hasCardBorder = false;
    };

    // A label whose text is in a kit colour, again when the theme changes.
    class KitLabel : public UltraCanvasLabel {
    public:
        KitLabel(const std::string& identifier, const std::string& text, KitColour part)
            : UltraCanvasLabel(identifier, text), textPart(part) { SetTextColor(ColourOf(part)); }
        void OnThemeChanged(const UltraCanvasTheme& theme) override {
            UltraCanvasLabel::OnThemeChanged(theme);
            SetTextColor(ColourOf(textPart, theme));
        }
    private:
        KitColour textPart;
    };

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
    class DemoPage : public KitContainer {
    public:
        explicit DemoPage(const std::string& identifier) : KitContainer(identifier) {}
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

    // The tab look every page shares: TabStyle::Modern, the open tab marked
    // by a 3 px accent underline, so a tab bar reads as tabs at a glance.
    void StyleTabs(const std::shared_ptr<UltraCanvasTabbedContainer>& tabs);

} // namespace DemoKit
} // namespace UltraCanvas
