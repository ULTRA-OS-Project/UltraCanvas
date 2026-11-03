// include/UltraCanvasCard.h
// A card: a raised surface with a header (icon, title, subtitle, something
// trailing such as a badge), a content area for the caller's elements and a
// row of actions - real UltraCanvasButton children, right-aligned - at the
// foot. The surface itself can be clicked (onClick fires for a press no
// child took), hovered, highlighted and selected. A store's app tile, a
// password vault's entry, a settings page's section with Save beside it.
//
//   auto card = CreateCard("tile", "UltraVideo", "The video editor");
//   card->SetIcon("media/icons/ultravideo.svg");
//   card->AddContent(CreateLabel("tile.summary", 0, 0, 0, 40, "Cut, trim, ..."));
//   card->AddActionButton("tile.install", "Install", [] { ... });
//   card->onClick = [] { /* open the details page */ };
//
// Built on UltraCanvasContainer: the children are the framework's labels and
// buttons, laid out as a flex column; this class adds the structure, the
// states and the surface.
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"
#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

    struct CardStyle {
        Color backgroundColor      = Colors::White;
        Color hoverBackgroundColor = Color(248, 249, 251, 255);
        Color selectedBackgroundColor = Color(235, 243, 255, 255);
        Color borderColor          = Color(210, 210, 214, 255);
        Color highlightBorderColor = Colors::Selection;        // highlighted or selected
        float borderWidth          = 1.0f;
        float highlightBorderWidth = 2.0f;
        float cornerRadius         = 10.0f;
        float padding              = 12.0f;
        float gap                  = 8.0f;        // between header, content and actions
        float iconSize             = 32.0f;
        float iconGap              = 10.0f;
        FontStyle titleFont;                       // bold 14 by default
        FontStyle subtitleFont;                    // 12 by default
        Color titleColor           = Color(30, 30, 36, 255);
        Color subtitleColor        = Color(110, 110, 118, 255);
        float actionsGap           = 8.0f;

        static CardStyle Default();
    };

    class UltraCanvasCard : public UltraCanvasContainer {
    public:
        AccessibleRole GetAccessibleRole() const override { return AccessibleRole::Group; }
        std::string GetAccessibleName() const override {
            return GetAccessibleNameOverride().empty() ? GetTitle() : GetAccessibleNameOverride();
        }

        UltraCanvasCard(const std::string& identifier, float x, float y, float w, float h,
                        const std::string& title = "", const std::string& subtitle = "");
        UltraCanvasCard(const std::string& identifier, float w, float h,
                        const std::string& title = "", const std::string& subtitle = "")
                : UltraCanvasCard(identifier, -1, -1, w, h, title, subtitle) {}
        explicit UltraCanvasCard(const std::string& identifier, const std::string& title = "",
                                 const std::string& subtitle = "")
                : UltraCanvasCard(identifier, -1, -1, -1, -1, title, subtitle) {}

        // ----- header -----
        void SetTitle(const std::string& text);
        std::string GetTitle() const;
        void SetSubtitle(const std::string& text);        // empty hides the line
        std::string GetSubtitle() const;
        void SetIcon(const std::string& path);           // SVG or image; empty removes it
        // The element at the header's right end (a badge, a chip, a switch).
        void SetTrailing(std::shared_ptr<UltraCanvasUIElement> element);
        std::shared_ptr<UltraCanvasLabel> GetTitleLabel() const { return titleLabel; }
        std::shared_ptr<UltraCanvasLabel> GetSubtitleLabel() const { return subtitleLabel; }

        // ----- content -----
        // The content area, a flex column the caller fills.
        std::shared_ptr<UltraCanvasContainer> GetContent() const { return content; }
        void AddContent(std::shared_ptr<UltraCanvasUIElement> element);
        void ClearContent();

        // ----- actions -----
        // The actions row, right-aligned; shown only while it has children.
        void AddAction(std::shared_ptr<UltraCanvasUIElement> element);
        // A real button with this text, wired to onClick; returned for styling.
        std::shared_ptr<UltraCanvasButton> AddActionButton(const std::string& id, const std::string& text,
                                                           std::function<void()> onClick, float minWidth = 72.0f);
        void ClearActions();
        std::shared_ptr<UltraCanvasContainer> GetActions() const { return actions; }

        // ----- states -----
        // Clickable: the surface shows a hand cursor and onClick fires for a
        // press no child took. On by default once onClick is set.
        void SetClickable(bool clickable);
        bool IsClickable() const { return clickable || static_cast<bool>(onClick); }
        void SetHighlighted(bool on);                    // accent border
        bool IsHighlighted() const { return highlighted; }
        void SetSelected(bool on);                       // accent border and tinted surface
        bool IsSelected() const { return selected; }

        // ----- look -----
        void SetStyle(const CardStyle& newStyle);
        const CardStyle& GetStyle() const { return style; }
        CardStyle& GetStyle() { return style; }

        // ----- callbacks -----
        std::function<void()> onClick;
        std::function<void()> onDoubleClick;

        // ----- framework -----
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        void Build();
        void ApplySurface();
        void ApplyFonts();
        void UpdateHeaderVisibility();

        CardStyle style;
        std::string iconPath;
        std::shared_ptr<UCImage> iconImage;
        std::shared_ptr<UltraCanvasContainer> header;     // row: titles | trailing
        std::shared_ptr<UltraCanvasContainer> titles;     // column: title, subtitle
        std::shared_ptr<UltraCanvasLabel> titleLabel;
        std::shared_ptr<UltraCanvasLabel> subtitleLabel;
        std::shared_ptr<UltraCanvasUIElement> trailing;
        std::shared_ptr<UltraCanvasContainer> content;
        std::shared_ptr<UltraCanvasContainer> actions;
        bool clickable = false;
        bool highlighted = false;
        bool selected = false;
        bool surfacePressed = false;
    };

    inline std::shared_ptr<UltraCanvasCard> CreateCard(const std::string& identifier,
                                                       const std::string& title = "",
                                                       const std::string& subtitle = "") {
        return std::make_shared<UltraCanvasCard>(identifier, title, subtitle);
    }
    inline std::shared_ptr<UltraCanvasCard> CreateCard(const std::string& identifier,
                                                       float x, float y, float w, float h,
                                                       const std::string& title = "",
                                                       const std::string& subtitle = "") {
        return std::make_shared<UltraCanvasCard>(identifier, x, y, w, h, title, subtitle);
    }

} // namespace UltraCanvas
