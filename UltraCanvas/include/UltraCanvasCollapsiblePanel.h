// include/UltraCanvasCollapsiblePanel.h
// A panel that folds to its header: a strip with a disclosure chevron, a
// title, a detail text ("3 downloads") and real buttons at the right, over
// a body the caller fills. Collapsed, only the header shows; expanded, the
// body below it at the expanded height (or as tall as its content). Docked
// at the bottom of a flex column it is a download queue, a log, a problems
// list; its top edge can be dragged to resize it.
//
//   auto panel = CreateCollapsiblePanel("downloads", "Downloads");
//   panel->SetDetail("2 running");
//   panel->AddHeaderButton("downloads.clear", "Clear", [] { ... });
//   panel->GetBody()->AddChild(queueList);
//   panel->SetExpandedHeight(220);
//   column->AddChild(panel);        // the parent is a flex column
//
// Built on UltraCanvasContainer: the header's label and buttons and the body's
// children are framework elements; this class adds the fold, the chevron,
// the separator and the resize grip.
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

    struct CollapsiblePanelStyle {
        Color backgroundColor       = Colors::White;
        Color headerBackgroundColor = Color(245, 245, 247, 255);
        Color headerHoverColor      = Color(236, 237, 240, 255);
        Color borderColor           = Color(210, 210, 214, 255);
        Color separatorColor        = Color(222, 222, 226, 255);
        Color chevronColor          = Color(90, 90, 95, 255);
        Color titleColor            = Color(30, 30, 36, 255);
        Color detailColor           = Color(110, 110, 118, 255);
        Color gripColor             = Colors::Selection;        // while the top edge is dragged
        float borderWidth   = 1.0f;
        float cornerRadius  = 0.0f;
        float headerHeight  = 30.0f;
        float paddingH      = 10.0f;
        float chevronSize   = 12.0f;
        float gap           = 8.0f;
        float gripSize      = 5.0f;          // the band along the top edge that resizes
        float minExpandedHeight = 80.0f;
        FontStyle titleFont;                 // bold 13 by default
        FontStyle detailFont;                // 12 by default

        static CollapsiblePanelStyle Default();
    };

    class UltraCanvasCollapsiblePanel : public UltraCanvasContainer {
    public:
        AccessibleRole GetAccessibleRole() const override { return AccessibleRole::Group; }
        std::string GetAccessibleName() const override {
            return GetAccessibleNameOverride().empty() ? GetTitle() : GetAccessibleNameOverride();
        }

        UltraCanvasCollapsiblePanel(const std::string& identifier, float x, float y, float w, float h,
                                    const std::string& title = "");
        UltraCanvasCollapsiblePanel(const std::string& identifier, float w, float h, const std::string& title = "")
                : UltraCanvasCollapsiblePanel(identifier, -1, -1, w, h, title) {}
        explicit UltraCanvasCollapsiblePanel(const std::string& identifier, const std::string& title = "")
                : UltraCanvasCollapsiblePanel(identifier, -1, -1, -1, -1, title) {}

        // ----- header -----
        void SetTitle(const std::string& text);
        std::string GetTitle() const;
        void SetDetail(const std::string& text);          // secondary text after the title; empty hides it
        std::string GetDetail() const;
        // An element at the header's right end (a button, a badge, a switch).
        void AddHeaderAction(std::shared_ptr<UltraCanvasUIElement> element);
        // A real button there, wired to onClick; returned for styling.
        std::shared_ptr<UltraCanvasButton> AddHeaderButton(const std::string& id, const std::string& text,
                                                           std::function<void()> onClick, float minWidth = 64.0f);
        void ClearHeaderActions();
        std::shared_ptr<UltraCanvasLabel> GetTitleLabel() const { return titleLabel; }

        // ----- body -----
        std::shared_ptr<UltraCanvasContainer> GetBody() const { return body; }

        // ----- fold -----
        void SetExpanded(bool expanded);
        bool IsExpanded() const { return expanded; }
        void Toggle() { SetExpanded(!expanded); }
        // The panel's height while expanded; 0 (the default) makes it as tall
        // as its body wants.
        void SetExpandedHeight(float height);
        float GetExpandedHeight() const { return expandedHeight; }
        // Whether dragging the top edge changes the expanded height (for a
        // panel docked at the bottom). On by default.
        void SetResizable(bool resizable) { resizable = resizable; }
        bool IsResizable() const { return resizable; }
        // Whether the header is a toggle at all (off: a fixed panel with a
        // header strip). On by default.
        void SetCollapsible(bool collapsible);
        bool IsCollapsible() const { return collapsible; }

        // ----- look -----
        void SetStyle(const CollapsiblePanelStyle& newStyle);
        const CollapsiblePanelStyle& GetStyle() const { return style; }
        CollapsiblePanelStyle& GetStyle() { return style; }

        // ----- callbacks -----
        std::function<void(bool expanded)> onExpandedChanged;
        std::function<void(float height)> onExpandedHeightChanged;   // after a drag

        // ----- framework -----
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

        // The header strip's rectangle, local.
        Rect2Df HeaderRect() const;

    private:
        void Build();
        void ApplyHeight();
        void ApplyFonts();
        void DrawChevron(IRenderContext* ctx, const Rect2Df& rect) const;
        bool InGrip(const Point2Df& p) const;

        CollapsiblePanelStyle style;
        std::shared_ptr<UltraCanvasContainer> header;
        std::shared_ptr<UltraCanvasLabel> titleLabel;
        std::shared_ptr<UltraCanvasLabel> detailLabel;
        std::shared_ptr<UltraCanvasContainer> headerActions;
        std::shared_ptr<UltraCanvasContainer> body;
        bool expanded = true;
        bool collapsible = true;
        bool resizable = true;
        float expandedHeight = 0.0f;
        bool headerHovered = false;
        bool gripHovered = false;
        bool dragging = false;
        float dragStartY = 0.0f;
        float dragStartHeight = 0.0f;
    };

    inline std::shared_ptr<UltraCanvasCollapsiblePanel> CreateCollapsiblePanel(const std::string& identifier,
                                                                               const std::string& title = "") {
        return std::make_shared<UltraCanvasCollapsiblePanel>(identifier, title);
    }
    inline std::shared_ptr<UltraCanvasCollapsiblePanel> CreateCollapsiblePanel(const std::string& identifier,
                                                                               float x, float y, float w, float h,
                                                                               const std::string& title = "") {
        return std::make_shared<UltraCanvasCollapsiblePanel>(identifier, x, y, w, h, title);
    }

} // namespace UltraCanvas
