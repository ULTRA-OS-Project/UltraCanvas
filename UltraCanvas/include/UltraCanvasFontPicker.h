// include/UltraCanvasFontPicker.h
// A font list in the WYSIWYG style of a word processor or a drawing program:
// a closed box that shows the current family in its own font, and a list
// that opens wider than the box with
//   - the fonts the document uses (the host names them: SetDocumentFonts),
//     a font the computer lacks marked so - in grey italics with
//     "(not installed)", or the name of the font drawn in its place;
//   - the fonts chosen last, "Recent" (SetRecentFonts, onRecentFontsChanged: the host
//     keeps them in its settings);
//   - a divider and "All fonts": every installed family A to Z
//     (GetInstalledFontCatalogue), each name drawn in its own font at one
//     size, with a small icon for its format at the left - O for OpenType,
//     TT for TrueType, a for Type 1, a pixel A for a bitmap font. A symbol or
//     dingbat font (Wingdings, Symbol) is named in the interface's font, with
//     a few of its own characters after the name;
//   - an arrow on a family with styles bold and italic cannot reach (Light,
//     SemiBold, Condensed, Black ...): resting the pointer on it, or Right,
//     opens its styles beside the list, each drawn in itself, and choosing
//     one gives the family with that weight, slant and width. A family of
//     Regular, Bold, Italic and Bold Italic alone has no arrow - the B and I
//     buttons beside the list reach those (SetStylesSubmenu changes the rule).
// Typing with the list open goes to the first family of "All fonts" whose
// name starts with what was typed (a pause starts afresh; one letter again
// steps on); Up, Down, Page Up, Page Down, Home and End move, Return
// chooses, Right opens the styles and Left closes them, Escape closes.
//
// The rows are virtual - only those on screen are drawn - and each name's
// picture (its layout in its own font) is made once and kept: the rows the
// list opens on first, then the rest, in small slices while the program is
// idle, as the dropdown does its font previews; loading a font is the dear
// part, and it happens on the UI thread, where the text engine lives.
//
// doc-check: std::shared_ptr<UltraCanvas::UltraCanvasToolbar> bar;
// doc-check: example
//   auto font = std::make_shared<UltraCanvas::UltraCanvasFontPicker>("FontList", 0, 0, 130, 26);
//   font->SetDefaultFontEntry("Default font");
//   font->SetRecentFonts({"Georgia", "Ubuntu"});
//   font->onFontChosen = [](const UltraCanvas::FontPickerChoice& choice) {
//       // choice.family, and from the styles list choice.weight, choice.slant
//       (void)choice;
//   };
//   bar->AddWidget("FontList", font);
//
// Version: 1.0.0 - the font list: sections (the document's fonts, recent fonts, all fonts), every
//                  name in its own font, format icons, symbol fonts with a sample, a styles list
//                  beside it, type-ahead, missing fonts marked, the closed box in the current font
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTextLayoutCache.h"
#include "UltraCanvasInstalledFonts.h"
#include "UltraCanvasScrollbar.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasListView;
    class UltraCanvasTheme;
    class FontPickerListModel;
    class FontPickerRowDelegate;
    class FontPickerStylesModel;
    class FontPickerStylesDelegate;

    // What was chosen.
    struct FontPickerChoice {
        std::string family;          // "" : the default-font row (SetDefaultFontEntry)
        // From the styles list: the style's name and what it is. From a
        // family's row: empty and the regular one's numbers - the host
        // changes the family and keeps its bold and italic.
        std::string style;
        int weight = 400;
        FontSlant slant = FontSlant::Normal;
        int stretch = 100;
        // The face's own family name, where the text engine reaches the
        // face by it (InstalledFontStyle::faceFamily, faceFamilyReach), and
        // whether with bold: what a document without weights stores for a
        // light or a condensed style. Empty where it cannot.
        std::string faceFamily;
        bool faceFamilyBold = false;
        bool fromStyles = false;     // chosen in the styles list
    };

    // Which families get the styles list.
    enum class FontPickerStylesSubmenu {
        BeyondBoldItalic,   // those with a style B and I cannot reach (the default)
        EveryMultiStyleFamily,   // every family with more than one style
        Off                      // none
    };

    struct FontPickerStyle {
        // The closed box, as a dropdown's
        Color normalColor = Colors::White;
        Color hoverColor = Color(240, 245, 255, 255);
        Color pressedColor = Color(225, 235, 255, 255);
        Color disabledColor = Color(245, 245, 245, 255);
        Color borderColor = Color(180, 180, 180, 255);
        Color focusBorderColor = Color(100, 150, 255, 255);
        Color textColor = Colors::Black;
        Color faintTextColor = Color(128, 128, 128, 255);   // a missing font, a section's caption
        // The list
        Color listBackgroundColor = Colors::White;
        Color listBorderColor = Color(180, 180, 180, 255);
        Color itemHoverColor = Color(240, 245, 255, 255);
        Color itemSelectedColor = Color(225, 235, 255, 255);
        Color dividerColor = Color(210, 210, 210, 255);

        std::string fontFamily;      // the interface's font: "" the system's
        float fontSize = 11.0f;      // ... and its size (the captions, the box)
        float previewSize = 14.0f;   // every name drawn in its own font at this size
        float rowHeight = 26.0f;
        float listWidth = 300.0f;    // the open list; at least the box's width
        float stylesWidth = 220.0f;  // the styles list, at least
        int maxVisibleRows = 18;
        float iconSize = 16.0f;
        float borderWidth = 1.0f;
        float arrowSize = 8.0f;
        ScrollbarStyle scrollbarStyle = GetDefaultScrollbarStyleOr(ScrollbarStyle::DropDown());

        static FontPickerStyle FromTheme(const UltraCanvasTheme& theme, const FontPickerStyle& base);
        static FontPickerStyle FromTheme(const UltraCanvasTheme& theme) { return FromTheme(theme, FontPickerStyle()); }
    };

    class UltraCanvasFontPicker : public UltraCanvasUIElement {
    public:
        // One row of the open list.
        enum class RowKind { Caption, DefaultFont, Family, Missing };
        enum class Section { Top, Document, Recent, AllFonts };
        struct Row {
            RowKind kind = RowKind::Family;
            Section section = Section::AllFonts;
            std::string text;        // a caption's text, the default row's label, a family's name
            int familyIndex = -1;    // into GetFamilies(), for an installed family
            bool selectable = true;  // a caption is not
        };

        UltraCanvasFontPicker(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasFontPicker(const std::string& identifier, float w, float h)
            : UltraCanvasFontPicker(identifier, -1, -1, w, h) {}
        explicit UltraCanvasFontPicker(const std::string& identifier)
            : UltraCanvasFontPicker(identifier, -1, -1, -1, -1) {}
        ~UltraCanvasFontPicker() override;

        // ===== ACCESSIBILITY =====
        AccessibleRole GetAccessibleRole() const override { return AccessibleRole::ComboBox; }
        std::string GetAccessibleValueText() const override;

        // ===== CALLBACKS =====
        std::function<void(const FontPickerChoice&)> onFontChosen;
        // The recent fonts changed (a font was chosen): the host saves them.
        std::function<void(const std::vector<std::string>&)> onRecentFontsChanged;
        // Just before the list opens: the host brings the document's fonts
        // and the recent ones up to date (SetDocumentFonts, SetRecentFonts).
        std::function<void()> onListOpening;
        std::function<void()> onListOpened;
        std::function<void()> onListClosed;

        // ===== WHAT IT SHOWS =====
        // The current family (and style) - what the closed box shows, in its
        // own font, and what the open list highlights. "" is the default
        // font when there is a default-font row; a family not installed is
        // shown in grey italics.
        void SetCurrentFont(const std::string& family, const std::string& style = std::string());
        // A selection set in several fonts: the box is blank.
        void SetMixed();
        bool IsMixed() const { return mixed; }
        const std::string& GetCurrentFamily() const { return currentFamily; }
        const std::string& GetCurrentStyle() const { return currentStyle; }
        // Whether the current family is installed (the default font always is).
        bool IsCurrentInstalled() const;

        // A first row that chooses "" - the text's own default - labelled
        // so ("Default font"); empty: no such row.
        void SetDefaultFontEntry(const std::string& label);
        // The top section: the families the document uses, in its order.
        void SetDocumentFonts(const std::vector<std::string>& families);
        const std::vector<std::string>& GetDocumentFonts() const { return documentFonts; }
        // The second: the fonts chosen last, newest first. Choosing a font
        // puts it first (and raises onRecentFontsChanged).
        void SetRecentFonts(const std::vector<std::string>& families);
        const std::vector<std::string>& GetRecentFonts() const { return recentFonts; }
        void SetRecentLimit(int limit);              // 8 by default
        int GetRecentLimit() const { return recentLimit; }
        // The captions of the sections - "In this document", "Recent", "All
        // fonts", translated - for a host that words them otherwise.
        void SetSectionCaptions(const std::string& document, const std::string& recent, const std::string& all);
        // How a font not installed reads: "{0} (not installed)", and
        // "{0} (shown in {1})" where a font stands in for it by name
        // (FontFamilySubstitute) - translated, for a host that words them
        // otherwise.
        void SetMissingTexts(const std::string& notInstalled, const std::string& shownIn);

        // The families of "All fonts". By default the installed ones
        // (GetInstalledFontCatalogue), followed as they change; a host or a
        // test can hand its own.
        void SetFamilies(std::shared_ptr<const std::vector<InstalledFontFamily>> families);
        const std::vector<InstalledFontFamily>& GetFamilies() const;

        void SetStylesSubmenu(FontPickerStylesSubmenu rule);
        FontPickerStylesSubmenu GetStylesSubmenu() const { return stylesRule; }
        // Whether a family's row has the arrow to its styles.
        bool HasStylesList(const InstalledFontFamily& family) const;

        // ===== STYLE =====
        void SetStyle(const FontPickerStyle& newStyle);
        const FontPickerStyle& GetStyle() const { return style; }
        void ApplyTheme(const UltraCanvasTheme& theme) override;
        void SetListWidth(float width);
        void SetPreviewSize(float size);

        // ===== KEYBOARD FOCUS =====
        // Takes the focus like a dropdown (Tab, a click); off - a toolbar's
        // list, which leaves the keyboard with the document - the open
        // list still takes the keys typed.
        bool AcceptsFocus() const override { return acceptsFocus; }
        void SetAcceptsFocus(bool accept) { acceptsFocus = accept; }

        // ===== THE OPEN LIST =====
        void OpenList();
        void CloseList();
        bool IsListOpen() const { return listOpen; }
        int GetRowCount() const { return static_cast<int>(rows.size()); }
        const Row* GetRow(int index) const;
        // The row the keys are on (-1: none).
        int GetHighlightedRow() const;
        void HighlightRow(int row);
        // The row of `family` in a section (case ignored); -1 when not there.
        int FindRow(const std::string& family, Section section = Section::AllFonts) const;
        // Choose a row as a click does: a family, a missing one or the
        // default-font row. False for a caption.
        bool ChooseRow(int row);
        // What a row's picture is drawn from - its name in its own font, or
        // the interface's for a symbol font, a caption, a missing font.
        // For a test and for the delegate.
        std::string RowFontFamily(int row) const;

        // ===== THE STYLES LIST =====
        bool OpenStyles(int row);
        void CloseStyles();
        bool IsStylesOpen() const { return stylesOpen; }
        int GetStylesRow() const { return stylesRow; }   // the family's row it belongs to
        const std::vector<InstalledFontStyle>& GetStylesShown() const { return stylesShown; }
        bool ChooseStyle(int index);

        // ===== TYPING =====
        // What typing with the list open does: the first family of "All
        // fonts" whose name starts with the letters typed within a second of
        // each other, regardless of case. True when the text was taken.
        bool TypeAhead(const std::string& text);

        // For a test: the list views (null until made).
        UltraCanvasListView* GetListView() const { return listView.get(); }
        UltraCanvasListView* GetStylesListView() const { return stylesView.get(); }
        // How many pictures (layouts) have been made, for a test.
        size_t GetPreparedPreviewCount() const { return nameLayouts.size(); }

        // ===== ELEMENT =====
        void SetWindow(UltraCanvasWindowBase* win) override;
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
        void InvalidateLayout() override;

    private:
        friend class FontPickerRowDelegate;
        friend class FontPickerStylesDelegate;

        FontPickerStyle style;
        std::shared_ptr<const std::vector<InstalledFontFamily>> families;
        bool familiesGiven = false;
        unsigned long familiesGeneration = 0;
        std::string currentFamily, currentStyle;
        bool mixed = false;
        std::string defaultLabel;
        std::vector<std::string> documentFonts, recentFonts;
        int recentLimit = 8;
        std::string captionDocument, captionRecent, captionAll;   // Tr in the constructor
        std::string missingText, substituteText;
        FontPickerStylesSubmenu stylesRule = FontPickerStylesSubmenu::BeyondBoldItalic;

        std::vector<Row> rows;
        bool rowsDirty = true;
        bool listOpen = false, stylesOpen = false, buttonPressed = false;
        bool acceptsFocus = true;
        int stylesRow = -1;
        std::vector<InstalledFontStyle> stylesShown;
        Point2Di listPosition;
        int hoverRow = -1, hoverX = -1;
        unsigned int hoverTimer = 0;
        std::string typed;
        std::chrono::steady_clock::time_point typedAt;

        std::shared_ptr<UltraCanvasListView> listView, stylesView;
        std::shared_ptr<FontPickerListModel> listModel;
        std::shared_ptr<FontPickerRowDelegate> listDelegate;
        std::shared_ptr<FontPickerStylesModel> stylesModel;
        std::shared_ptr<FontPickerStylesDelegate> stylesDelegate;

        // The pictures: each family's name in its own font, a symbol font's
        // sample, the texts in the interface's font, the styles.
        std::unordered_map<std::string, UltraCanvasTextLayoutCache> nameLayouts;
        std::unordered_map<std::string, UltraCanvasTextLayoutCache> plainLayouts;
        std::vector<UltraCanvasTextLayoutCache> styleLayouts;
        UltraCanvasTextLayoutCache boxLayout;
        // The idle preparation of the pictures.
        std::vector<int> previewQueue;
        size_t previewNext = 0;
        unsigned int previewTimer = 0;
        bool previewsDirty = true;

        void StyleChanged();
        void RefreshFamilies();
        void BuildRows();
        const InstalledFontFamily* FamilyOfRow(int row) const;
        void CreateListViews();
        void PlaceList();
        void Chosen(const FontPickerChoice& choice);
        void RememberRecent(const std::string& family);
        void StopHoverTimer();
        void HoverChanged(int row);
        bool HandleListKey(const UCEvent& event);
        bool HandleStylesKey(const UCEvent& event);
        void MoveHighlight(int step);
        int VisibleRowCount() const;

        void QueuePreviews();
        void PrepareNextPreviews();
        void StopPreviewTimer();

        // The pictures, made on first use and kept.
        ITextLayout* NameLayout(IRenderContext* ctx, const std::string& family, float size);
        ITextLayout* PlainLayout(IRenderContext* ctx, const std::string& text, bool italic, float size,
                                 const std::string& fontFamily = std::string());
        std::string MissingRowText(const std::string& family) const;
        void RenderBox(IRenderContext* ctx);
        void RenderRow(IRenderContext* ctx, int row, const Rect2Di& rect, bool highlighted);
        void RenderStyleRow(IRenderContext* ctx, int index, const Rect2Di& rect);
        void RenderFormatIcon(IRenderContext* ctx, FontFileFormat format, const Rect2Di& rect);
        void RenderArrow(IRenderContext* ctx, const Rect2Di& rect, bool down, const Color& colour);
    };

    // The icon a font list shows for a format, under media/icons/fonts/
    // (the resources folder's path); "" for Unknown.
    std::string FontFormatIconPath(FontFileFormat format);

    inline std::shared_ptr<UltraCanvasFontPicker> CreateFontPicker(const std::string& identifier, float x, float y,
                                                                   float w, float h = 26) {
        return std::make_shared<UltraCanvasFontPicker>(identifier, x, y, w, h);
    }

} // namespace UltraCanvas
