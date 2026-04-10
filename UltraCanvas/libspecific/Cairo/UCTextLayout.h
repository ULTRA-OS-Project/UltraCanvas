// libspecific/Cairo/UCTextLayout.h
// Pango text layout wrapper for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2026-04-07
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <string>
#include <memory>
#include <functional>
#include <cstdint>

namespace UltraCanvas {

    // ===== ENUMS =====

//    enum class EllipsizeMode {
//        EllipsizeNone,
//        EllipsizeStart,
//        EllipsizeMiddle,
//        EllipsizeEnd
//    };
//
//    enum class UCUnderlineType {
//        UnderlineNone,
//        UnderlineSingle,
//        UnderlineDouble,
//        UnderlineLow,
//        UnderlineError
//    };
//
//    enum class UCFontVariant {
//        VariantNormal,
//        VariantSmallCaps
//    };
//
//    enum class UCFontStretch {
//        UltraCondensed,
//        ExtraCondensed,
//        Condensed,
//        SemiCondensed,
//        Normal,
//        SemiExpanded,
//        Expanded,
//        ExtraExpanded,
//        UltraExpanded
//    };
//
//    // ===== RESULT STRUCTS =====
//
//    struct UCTextExtents {
//        Rect2Di ink;
//        Rect2Di logical;
//    };
//
//    struct UCTextHitResult {
//        int index;
//        int trailing;
//        bool inside;
//    };
//
//    struct UCCursorPos {
//        Rect2Di strongPos;
//        Rect2Di weakPos;
//    };
//
//    struct UCLineXResult {
//        int line;
//        int xPos;
//    };
//
//    struct UCCursorMoveResult {
//        int newIndex;
//        int newTrailing;
//    };

    // ===== LAYOUT ITERATOR =====
//
//    struct PangoLayoutIterDeleter {
//        void operator()(PangoLayoutIter* iter) const {
//            if (iter) pango_layout_iter_free(iter);
//        }
//    };
//    using UCTextLayoutIter = std::unique_ptr<PangoLayoutIter, PangoLayoutIterDeleter>;

    // ===== UCTextAttribute =====

    class UCTextAttribute : public ITextAttribute {
    private:
        PangoAttribute* attr = nullptr;

    public:
        explicit UCTextAttribute(PangoAttribute* a) : attr(a) {}
        UCTextAttribute() = default;

        ~UCTextAttribute() override;

        // Move-only
        UCTextAttribute(UCTextAttribute&& other) noexcept;
        UCTextAttribute& operator=(UCTextAttribute&& other) noexcept;
        UCTextAttribute(const UCTextAttribute&) = delete;
        UCTextAttribute& operator=(const UCTextAttribute&) = delete;

        ITextAttribute& SetRange(int startIndex, int endIndex) override;
        TextAttributeType GetType() override;
        void* Release() override;
    };

    // ===== UCTextAttributeList =====

//    class UCTextAttributeList {
//    private:
//        PangoAttrList* attrList = nullptr;
//
//    public:
//        UCTextAttributeList();
//        ~UCTextAttributeList();
//
//        // Move-only
//        UCTextAttributeList(UCTextAttributeList&& other) noexcept;
//        UCTextAttributeList& operator=(UCTextAttributeList&& other) noexcept;
//        UCTextAttributeList(const UCTextAttributeList&) = delete;
//        UCTextAttributeList& operator=(const UCTextAttributeList&) = delete;
//
//        // Insert attribute (takes ownership from UCTextAttribute)
//        void Insert(UCTextAttribute& attr);
//        void InsertBefore(UCTextAttribute& attr);
//        void Change(UCTextAttribute& attr);
//
//        PangoAttrList* GetHandle() const;
//        bool IsValid() const;
//
//        // Serialize to string representation
//        std::string ToString() const;
//        // Create from string representation (replaces current list)
//        static UCTextAttributeList FromString(const std::string& str);
//        // Filter attributes, returns a new list containing attributes for which
//        // the predicate returns true. Filtered attributes are removed from this list.
//        UCTextAttributeList Filter(std::function<bool(const PangoAttribute*)> predicate);
//    };

    // ===== UCTextLayout =====

    class UCTextLayout : public ITextLayout {
    private:
        PangoLayout* layout = nullptr;
        PangoAttrList *attrsList = nullptr;
        int explicitHeight = 0;
        VerticalAlignment valign = VerticalAlignment::Top;
        UCTextExtents extents;
        bool extentsValid = false;
    public:
        explicit UCTextLayout(PangoContext* ctx);
        ~UCTextLayout() override;

        // Move-only
//        UCTextLayout(UCTextLayout&& other) noexcept;
//        UCTextLayout& operator=(UCTextLayout&& other) noexcept;

        UCTextLayout(const UCTextLayout&) = delete;
        UCTextLayout& operator=(const UCTextLayout&) = delete;

        bool IsValid() const override;
        void* GetHandle() const override;

        // ===== TEXT CONTENT =====
        void SetText(const std::string& text) override;
        std::string GetText() const override;
        void SetMarkup(const std::string& markup) override;

        // ===== FONT =====
        void SetFontStyle(const FontStyle& fontStyle) override;
//        void SetFontDescriptionFromPango(const PangoFontDescription* desc) override;

        // ===== DIMENSIONS (pixel API) =====
        void SetExplicitWidth(int widthPixels) override;       // -1 to unset
        int GetExplicitWidth() const override;
        void SetExplicitHeight(int heightPixels) override;     // -1 to unset
        int GetExplicitHeight() const override;

        // ===== ALIGNMENT & JUSTIFICATION =====
        void SetAlignment(TextAlignment align) override;
        TextAlignment GetAlignment() const override;
        void SetVerticalAlignment(VerticalAlignment align) override;
        VerticalAlignment GetVerticalAlignment() override { return valign; };

        // ===== WRAPPING & ELLIPSIZATION =====
        void SetWrap(TextWrap wrap) override;
        TextWrap GetWrap() const override;
        void SetEllipsize(EllipsizeMode mode) override;
        EllipsizeMode GetEllipsize() const override;

        // ===== SPACING & INDENTATION (pixels) =====
        void SetIndent(int indentPixels) override;
        int GetIndent() const override;
        void SetLineSpacing(float factor) override;
        float GetLineSpacing() const override;
        void SetSpacing(int spacingPixels) override;   // Inter-paragraph spacing
        int GetSpacing() const override;

        // ===== MODE =====
        void SetSingleParagraphMode(bool single) override;
        bool GetSingleParagraphMode() const override;
        void SetAutoDir(bool autoDir) override;
        bool GetAutoDir() const override;

        // ===== ATTRIBUTES =====
//        void SetAttributes(const UCTextAttributeList& attrs) override;
//        PangoAttrList* GetAttributesHandle() const override;
        void InsertAttribute(std::unique_ptr<ITextAttribute> attr) override;
        void ChangeAttribute(std::unique_ptr<ITextAttribute> attr) override;
        void SetAttributesFromString(const std::string& str) override;
        void RemoveAllAttributes() override;

        // ===== TABS =====
//        void SetTabs(PangoTabArray* tabs) override;
//        PangoTabArray* GetTabs() const override;

        // ===== MEASUREMENT =====
        UCTextExtents GetLayoutExtents() override;
        int GetLayoutVerticalOffset() override;

        int GetBaseline() const override;
        int GetLineCount() const override;

        // ===== HIT TESTING & POSITION =====
        UCTextHitResult XYToIndex(int pixelX, int pixelY) const override;
        Rect2Di IndexToPos(int byteIndex) const override;
        UCLineXResult IndexToLineX(int byteIndex, bool trailing) const override;
        UCCursorPos GetCursorPos(int byteIndex) const override;
        UCCursorMoveResult MoveCursorVisually(bool strongCursor, int oldIndex,
                                              int oldTrailing, int direction) const override;

        // ===== LINE ACCESS =====
//        PangoLayoutLine* GetLine(int lineIndex) const override;
//        PangoLayoutLine* GetLineReadonly(int lineIndex) const override;
//        GSList* GetLines() const override;
//        GSList* GetLinesReadonly() const override;
//
//        // ===== ITERATOR =====
//        UCTextLayoutIter GetIter() const override;
//
//        // ===== RENDERING =====
//        void Show(IRenderContext* ctx) const override;
//        void ShowAt(IRenderContext* ctx, float x, float y) const override;
    };

} // namespace UltraCanvas
