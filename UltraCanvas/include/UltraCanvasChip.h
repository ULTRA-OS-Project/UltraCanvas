// include/UltraCanvasChip.h
// Chips and a tag-input field for UltraCanvas.
//
//   * UltraCanvasChip     — a single compact "chip": a pill with a label, an
//                           optional leading icon (vector/image), an optional
//                           remove (×) button, and optional selectable state
//                           (filter / choice chips). Filled or outlined.
//
//   * UltraCanvasTagInput — a token field: type text and press Enter (or comma)
//                           to add a removable chip; Backspace on an empty field
//                           removes the last chip. Chips wrap across rows.
//
// Both are self-rendered (no child widgets) and follow the standard element
// conventions.
//
// Version: 1.0.1
// Last Modified: 2026-07-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include <optional>

namespace UltraCanvas {

// ===== CHIP VARIANT =====
    enum class ChipVariant {
        Filled,
        Outlined
    };

// ===== CHIP STYLE =====
    struct ChipStyle {
        Color fillColor        = Color(233, 236, 239, 255);
        Color textColor        = Color(40, 40, 46, 255);
        Color borderColor      = Color(196, 200, 206, 255);
        Color hoverFillColor   = Color(222, 226, 231, 255);
        Color pressedFillColor = Color(208, 213, 219, 255);

        Color selectedFillColor = Colors::Selection;       // 0,120,215
        Color selectedTextColor = Colors::White;

        Color disabledFillColor = Color(240, 240, 242, 255);
        Color disabledTextColor = Color(180, 180, 186, 255);

        Color closeColor        = Color(110, 110, 116, 255);
        Color closeHoverColor   = Color(210, 60, 60, 255);
        Color closeHoverBg      = Color(0, 0, 0, 30);

        float height        = 28.0f;
        float cornerRadius  = 14.0f;     // pill; set < height/2 for rounded-rect
        float paddingH      = 10.0f;
        float iconSize      = 18.0f;
        float closeSize     = 16.0f;
        float spacing       = 6.0f;      // gap between icon / label / close
        float borderWidth   = 1.0f;

        FontStyle fontStyle;
    };

// ===== SINGLE CHIP =====
    class UltraCanvasChip : public UltraCanvasUIElement {
    private:
        std::string label;
        std::string iconPath;
        std::shared_ptr<UCImage> iconImage;

        bool closable   = false;
        bool selectable = false;
        bool selected   = false;
        ChipVariant variant = ChipVariant::Filled;
        bool autoWidth  = true;    // size own width to content in Render

        ChipStyle style;

        bool closeHovered = false;
        mutable float cachedCloseX = -1.0f;   // element-local x of the close hit area

    public:
        UltraCanvasChip(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasChip(const std::string& identifier, float w, float h)
            : UltraCanvasChip(identifier, -1, -1, w, h) {}
        explicit UltraCanvasChip(const std::string& identifier)
            : UltraCanvasChip(identifier, -1, -1, -1, -1) {}

        // ===== CONTENT =====
        void SetLabel(const std::string& text) { label = text; InvalidateLayout(); RequestRedraw(); }
        const std::string& GetLabel() const { return label; }

        void SetIcon(const std::string& path);

        void SetClosable(bool c)   { closable = c; InvalidateLayout(); RequestRedraw(); }
        bool IsClosable() const    { return closable; }

        void SetSelectable(bool s) { selectable = s; RequestRedraw(); }
        bool IsSelectable() const  { return selectable; }

        void SetSelected(bool s);
        bool IsSelected() const    { return selected; }

        void SetVariant(ChipVariant v) { variant = v; RequestRedraw(); }
        ChipVariant GetVariant() const { return variant; }

        void SetAutoWidth(bool a) { autoWidth = a; }

        ChipStyle& GetStyle() { return style; }
        const ChipStyle& GetStyle() const { return style; }
        void SetStyle(const ChipStyle& s) { style = s; InvalidateLayout(); RequestRedraw(); }

        float GetPreferredWidth(IRenderContext* ctx) const;

        // ===== OVERRIDES =====
        bool AcceptsFocus() const override { return selectable; }
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

        // ===== ENGINE-DRIVEN LAYOUT =====
        // Publish the chip's intrinsic size so the CSS engine can place a chip
        // created with auto (0) width/height. Without these the engine leaves
        // the chip zero-sized and the parent container culls it before Render
        // (where the legacy autoWidth sizing lives) ever runs.
        Size2Df MeasureOwnContent(std::optional<float> definiteContentWidth,
                                  const CSSLayout::LayoutContext& ctx) override;
        void ComputeIntrinsicSizes(const CSSLayout::LayoutContext& ctx) override;

        // ===== CALLBACKS =====
        std::function<void()> onClick;
        std::function<void()> onClose;              // × pressed (owner should remove it)
        std::function<void(bool)> onSelectedChanged;

    private:
        Rect2Df CloseRect() const;  // element-local; empty when not closable
        // Preferred content width via the window's render context when
        // available, or a rough font-size based estimate before the first
        // frame (so the chip is never measured 0-wide).
        float ContentWidthForLayout() const;
    };

// ===== TAG INPUT STYLE =====
    struct TagInputStyle {
        Color backgroundColor  = Colors::White;
        Color borderColor      = Color(180, 180, 186, 255);
        Color focusBorderColor = Colors::Selection;
        Color textColor        = Color(30, 30, 34, 255);
        Color placeholderColor = Color(160, 160, 166, 255);
        Color caretColor       = Color(0, 120, 215, 255);

        ChipStyle chipStyle;

        float borderWidth   = 1.0f;
        float cornerRadius  = 4.0f;
        float padding       = 6.0f;
        float chipSpacing   = 6.0f;
        float rowSpacing    = 6.0f;
        float minInputWidth = 60.0f;

        FontStyle fontStyle;
    };

// ===== TAG INPUT (token field) =====
    class UltraCanvasTagInput : public UltraCanvasUIElement {
    private:
        std::vector<std::string> tags;
        std::string editBuffer;
        std::string placeholder = "Add tag…";
        bool allowDuplicates = false;
        int  maxTags = 0;          // 0 = unlimited
        bool autoHeight = true;    // grow height to fit wrapped rows

        TagInputStyle style;

        // Layout cache (computed in Render, reused for hit-testing).
        struct ChipBox { int index; Rect2Df rect; Rect2Df closeRect; };
        mutable std::vector<ChipBox> chipBoxes;
        mutable Rect2Df inputRect;
        mutable float contentHeight = 0.0f;

        int hoveredChip = -1;   // chip whose × is hovered

    public:
        UltraCanvasTagInput(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasTagInput(const std::string& identifier, float w, float h)
            : UltraCanvasTagInput(identifier, -1, -1, w, h) {}
        explicit UltraCanvasTagInput(const std::string& identifier)
            : UltraCanvasTagInput(identifier, -1, -1, -1, -1) {}

        // ===== TAGS =====
        bool AddTag(const std::string& tag, bool runCallback = true);
        void RemoveTag(int index, bool runCallback = true);
        bool RemoveTag(const std::string& tag, bool runCallback = true);
        void ClearTags(bool runCallback = true);
        void SetTags(const std::vector<std::string>& newTags);
        const std::vector<std::string>& GetTags() const { return tags; }
        int  GetTagCount() const { return static_cast<int>(tags.size()); }
        bool HasTag(const std::string& tag) const;

        // ===== CONFIG =====
        void SetPlaceholder(const std::string& p) { placeholder = p; RequestRedraw(); }
        void SetAllowDuplicates(bool a) { allowDuplicates = a; }
        bool GetAllowDuplicates() const { return allowDuplicates; }
        void SetMaxTags(int n) { maxTags = std::max(0, n); }
        int  GetMaxTags() const { return maxTags; }
        void SetAutoHeight(bool a) { autoHeight = a; }

        // Current text in the inline editor.
        const std::string& GetInputText() const { return editBuffer; }
        void SetInputText(const std::string& t) { editBuffer = t; RequestRedraw(); }

        TagInputStyle& GetStyle() { return style; }
        const TagInputStyle& GetStyle() const { return style; }
        void SetStyle(const TagInputStyle& s) { style = s; RequestRedraw(); }

        float GetContentHeight() const { return contentHeight; }

        // ===== OVERRIDES =====
        bool AcceptsFocus() const override { return true; }
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

        // ===== CALLBACKS =====
        std::function<void(const std::vector<std::string>&)> onTagsChanged;
        std::function<void(const std::string&)> onTagAdded;
        std::function<void(const std::string&)> onTagRemoved;
        // Return false to reject a candidate tag (e.g. failed validation).
        std::function<bool(const std::string&)> validator;

    private:
        bool CommitBuffer();
        std::string Trim(const std::string& s) const;
        void ComputeLayout(IRenderContext* ctx) const;   // fills chipBoxes + inputRect
        void FireChanged();

        bool HandleKeyDown(const UCEvent& event);
        // Printable text arrives on KeyDown (event.character / event.text);
        // the platform layers never emit a separate KeyChar event.
        bool HandleCharacterInput(const UCEvent& event);
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasChip> CreateChip(
            const std::string& identifier, float x, float y, const std::string& label,
            bool closable = false) {
        auto c = std::make_shared<UltraCanvasChip>(identifier, x, y, 0, 0);
        c->SetLabel(label);
        c->SetClosable(closable);
        return c;
    }

    inline std::shared_ptr<UltraCanvasChip> CreateFilterChip(
            const std::string& identifier, float x, float y, const std::string& label,
            bool selected = false) {
        auto c = std::make_shared<UltraCanvasChip>(identifier, x, y, 0, 0);
        c->SetLabel(label);
        c->SetSelectable(true);
        c->SetSelected(selected);
        return c;
    }

    inline std::shared_ptr<UltraCanvasTagInput> CreateTagInput(
            const std::string& identifier, float x, float y, float w, float h = 36) {
        return std::make_shared<UltraCanvasTagInput>(identifier, x, y, w, h);
    }

} // namespace UltraCanvas
