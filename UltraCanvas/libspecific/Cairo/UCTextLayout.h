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

    enum class UCEllipsizeMode {
        EllipsizeNone,
        EllipsizeStart,
        EllipsizeMiddle,
        EllipsizeEnd
    };

    enum class UCUnderlineType {
        UnderlineNone,
        UnderlineSingle,
        UnderlineDouble,
        UnderlineLow,
        UnderlineError
    };

    enum class UCFontVariant {
        VariantNormal,
        VariantSmallCaps
    };

    enum class UCFontStretch {
        UltraCondensed,
        ExtraCondensed,
        Condensed,
        SemiCondensed,
        Normal,
        SemiExpanded,
        Expanded,
        ExtraExpanded,
        UltraExpanded
    };

    // ===== RESULT STRUCTS =====

    struct UCTextExtents {
        Rect2Di ink;
        Rect2Di logical;
    };

    struct UCTextHitResult {
        int index;
        int trailing;
        bool inside;
    };

    struct UCCursorPos {
        Rect2Di strongPos;
        Rect2Di weakPos;
    };

    struct UCLineXResult {
        int line;
        int xPos;
    };

    struct UCCursorMoveResult {
        int newIndex;
        int newTrailing;
    };

    // ===== LAYOUT ITERATOR =====

    struct PangoLayoutIterDeleter {
        void operator()(PangoLayoutIter* iter) const {
            if (iter) pango_layout_iter_free(iter);
        }
    };
    using UCTextLayoutIter = std::unique_ptr<PangoLayoutIter, PangoLayoutIterDeleter>;

    // ===== UCTextAttribute =====

    class UCTextAttribute {
    private:
        PangoAttribute* attr = nullptr;

        explicit UCTextAttribute(PangoAttribute* a) : attr(a) {}

    public:
        UCTextAttribute() = default;

        ~UCTextAttribute();

        // Move-only
        UCTextAttribute(UCTextAttribute&& other) noexcept;
        UCTextAttribute& operator=(UCTextAttribute&& other) noexcept;
        UCTextAttribute(const UCTextAttribute&) = delete;
        UCTextAttribute& operator=(const UCTextAttribute&) = delete;

        void SetRange(int startIndex, int endIndex);
        PangoAttribute* Release();
        bool IsValid() const;

        // ===== FACTORY METHODS =====

        // Font description (from UltraCanvas FontStyle)
        static UCTextAttribute CreateFontDesc(const FontStyle& fontStyle);
        // Font description (from raw PangoFontDescription)
        static UCTextAttribute CreateFontDescFromPango(const PangoFontDescription* desc);

        // Font family
        static UCTextAttribute CreateFamily(const std::string& family);

        // Font size in points (internally converted to Pango units)
        static UCTextAttribute CreateSize(float sizeInPoints);

        // Absolute font size in pixels (internally converted to Pango units)
        static UCTextAttribute CreateAbsoluteSize(float sizeInPixels);

        // Weight
        static UCTextAttribute CreateWeight(FontWeight weight);

        // Style / slant
        static UCTextAttribute CreateStyle(FontSlant slant);

        // Variant
        static UCTextAttribute CreateVariant(UCFontVariant variant);

        // Stretch
        static UCTextAttribute CreateStretch(UCFontStretch stretch);

        // Underline
        static UCTextAttribute CreateUnderline(UCUnderlineType type);
        static UCTextAttribute CreateUnderlineColor(const Color& color);

        // Strikethrough
        static UCTextAttribute CreateStrikethrough(bool enabled);
        static UCTextAttribute CreateStrikethroughColor(const Color& color);

        // Colors
        static UCTextAttribute CreateForeground(const Color& color);
        static UCTextAttribute CreateBackground(const Color& color);

        // Alpha (0-65535)
        static UCTextAttribute CreateForegroundAlpha(uint16_t alpha);
        static UCTextAttribute CreateBackgroundAlpha(uint16_t alpha);

        // Letter spacing in pixels (internally converted to Pango units)
        static UCTextAttribute CreateLetterSpacing(int spacingInPixels);

        // Rise (superscript/subscript displacement) in pixels
        static UCTextAttribute CreateRise(int riseInPixels);

        // Scale factor (e.g. 0.5 for half, 2.0 for double)
        static UCTextAttribute CreateScale(double scaleFactor);

        // Fallback font
        static UCTextAttribute CreateFallback(bool enable);

        // Language tag (e.g. "en-US")
        static UCTextAttribute CreateLanguage(const std::string& lang);
    };

    // ===== UCTextAttributeList =====

    class UCTextAttributeList {
    private:
        PangoAttrList* attrList = nullptr;

    public:
        UCTextAttributeList();
        ~UCTextAttributeList();

        // Move-only
        UCTextAttributeList(UCTextAttributeList&& other) noexcept;
        UCTextAttributeList& operator=(UCTextAttributeList&& other) noexcept;
        UCTextAttributeList(const UCTextAttributeList&) = delete;
        UCTextAttributeList& operator=(const UCTextAttributeList&) = delete;

        // Insert attribute (takes ownership from UCTextAttribute)
        void Insert(UCTextAttribute& attr);
        void InsertBefore(UCTextAttribute& attr);
        void Change(UCTextAttribute& attr);

        PangoAttrList* GetHandle() const;
        bool IsValid() const;

        // Serialize to string representation
        std::string ToString() const;
        // Create from string representation (replaces current list)
        static UCTextAttributeList FromString(const std::string& str);
        // Filter attributes, returns a new list containing attributes for which
        // the predicate returns true. Filtered attributes are removed from this list.
        UCTextAttributeList Filter(std::function<bool(const PangoAttribute*)> predicate);
    };

    // ===== UCTextLayout =====

    class UCTextLayout {
    private:
        PangoLayout* layout = nullptr;

    public:
        explicit UCTextLayout(PangoContext* pangoContext);
        ~UCTextLayout();

        // Move-only
        UCTextLayout(UCTextLayout&& other) noexcept;
        UCTextLayout& operator=(UCTextLayout&& other) noexcept;
        UCTextLayout(const UCTextLayout&) = delete;
        UCTextLayout& operator=(const UCTextLayout&) = delete;

        bool IsValid() const;
        PangoLayout* GetHandle() const;

        void Update(const IRenderContext& ctx);

        // ===== TEXT CONTENT =====
        void SetText(const std::string& text);
        std::string GetText() const;
        void SetMarkup(const std::string& markup);

        // ===== FONT =====
        void SetFontDescription(const FontStyle& fontStyle);
        void SetFontDescriptionFromPango(const PangoFontDescription* desc);

        // ===== DIMENSIONS (pixel API) =====
        void SetWidth(int widthPixels);       // -1 to unset
        int GetWidth() const;
        void SetHeight(int heightPixels);     // -1 to unset
        int GetHeight() const;

        // ===== ALIGNMENT & JUSTIFICATION =====
        void SetAlignment(TextAlignment align);
        TextAlignment GetAlignment() const;
        void SetJustify(bool justify);
        bool GetJustify() const;

        // ===== WRAPPING & ELLIPSIZATION =====
        void SetWrap(TextWrap wrap);
        TextWrap GetWrap() const;
        void SetEllipsize(UCEllipsizeMode mode);
        UCEllipsizeMode GetEllipsize() const;

        // ===== SPACING & INDENTATION (pixels) =====
        void SetIndent(int indentPixels);
        int GetIndent() const;
        void SetLineSpacing(float factor);
        float GetLineSpacing() const;
        void SetSpacing(int spacingPixels);   // Inter-paragraph spacing
        int GetSpacing() const;

        // ===== MODE =====
        void SetSingleParagraphMode(bool single);
        bool GetSingleParagraphMode() const;
        void SetAutoDir(bool autoDir);
        bool GetAutoDir() const;

        // ===== ATTRIBUTES =====
        void SetAttributes(const UCTextAttributeList& attrs);
        PangoAttrList* GetAttributesHandle() const;

        // ===== TABS =====
        void SetTabs(PangoTabArray* tabs);
        PangoTabArray* GetTabs() const;

        // ===== MEASUREMENT =====
        UCTextExtents GetPixelExtents() const;
        Size2Di GetPixelSize() const;
        void GetSize(int& widthPangoUnits, int& heightPangoUnits) const;
        int GetBaseline() const;
        int GetBaselinePangoUnits() const;
        int GetLineCount() const;

        // ===== HIT TESTING & POSITION =====
        UCTextHitResult XYToIndex(int pixelX, int pixelY) const;
        Rect2Di IndexToPos(int byteIndex) const;
        UCLineXResult IndexToLineX(int byteIndex, bool trailing) const;
        UCCursorPos GetCursorPos(int byteIndex) const;
        UCCursorMoveResult MoveCursorVisually(bool strongCursor, int oldIndex,
                                              int oldTrailing, int direction) const;

        // ===== LINE ACCESS =====
        PangoLayoutLine* GetLine(int lineIndex) const;
        PangoLayoutLine* GetLineReadonly(int lineIndex) const;
        GSList* GetLines() const;
        GSList* GetLinesReadonly() const;

        // ===== ITERATOR =====
        UCTextLayoutIter GetIter() const;

        // ===== RENDERING =====
        void Show(cairo_t* cr) const;
        void ShowAt(cairo_t* cr, float x, float y) const;
    };

} // namespace UltraCanvas
