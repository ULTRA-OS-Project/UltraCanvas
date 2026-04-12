// libspecific/Cairo/UCTextLayout.cpp
// Pango text layout wrapper for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2026-04-07
// Author: UltraCanvas Framework

#include "UCTextLayout.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    // ===== HELPER CONVERSION FUNCTIONS =====

    static PangoWeight ToPangoWeight(FontWeight fw) {
        switch (fw) {
            case FontWeight::Light:     return PANGO_WEIGHT_LIGHT;
            case FontWeight::Bold:      return PANGO_WEIGHT_BOLD;
            case FontWeight::ExtraBold: return PANGO_WEIGHT_ULTRABOLD;
            default:                    return PANGO_WEIGHT_NORMAL;
        }
    }

    static PangoStyle ToPangoStyle(FontSlant fs) {
        switch (fs) {
            case FontSlant::Italic:  return PANGO_STYLE_ITALIC;
            case FontSlant::Oblique: return PANGO_STYLE_OBLIQUE;
            default:                 return PANGO_STYLE_NORMAL;
        }
    }

    static PangoAlignment ToPangoAlignment(TextAlignment align) {
        switch (align) {
            case TextAlignment::Center: return PANGO_ALIGN_CENTER;
            case TextAlignment::Right:  return PANGO_ALIGN_RIGHT;
            default:                    return PANGO_ALIGN_LEFT;
        }
    }

    static TextAlignment FromPangoAlignment(PangoAlignment align) {
        switch (align) {
            case PANGO_ALIGN_CENTER: return TextAlignment::Center;
            case PANGO_ALIGN_RIGHT:  return TextAlignment::Right;
            default:                 return TextAlignment::Left;
        }
    }

    static PangoEllipsizeMode ToPangoEllipsize(EllipsizeMode mode) {
        switch (mode) {
            case EllipsizeMode::EllipsizeStart:  return PANGO_ELLIPSIZE_START;
            case EllipsizeMode::EllipsizeMiddle: return PANGO_ELLIPSIZE_MIDDLE;
            case EllipsizeMode::EllipsizeEnd:    return PANGO_ELLIPSIZE_END;
            default:                               return PANGO_ELLIPSIZE_NONE;
        }
    }

    static EllipsizeMode FromPangoEllipsize(PangoEllipsizeMode mode) {
        switch (mode) {
            case PANGO_ELLIPSIZE_START:  return EllipsizeMode::EllipsizeStart;
            case PANGO_ELLIPSIZE_MIDDLE: return EllipsizeMode::EllipsizeMiddle;
            case PANGO_ELLIPSIZE_END:    return EllipsizeMode::EllipsizeEnd;
            default:                     return EllipsizeMode::EllipsizeNone;
        }
    }

    static PangoWrapMode ToPangoWrap(TextWrap wrap) {
        switch (wrap) {
            case TextWrap::WrapWord:     return PANGO_WRAP_WORD;
            case TextWrap::WrapChar:     return PANGO_WRAP_CHAR;
            case TextWrap::WrapWordChar: return PANGO_WRAP_WORD_CHAR;
            default:                     return PANGO_WRAP_WORD;
        }
    }

    static TextWrap FromPangoWrap(PangoWrapMode mode) {
        switch (mode) {
            case PANGO_WRAP_WORD:      return TextWrap::WrapWord;
            case PANGO_WRAP_CHAR:      return TextWrap::WrapChar;
            case PANGO_WRAP_WORD_CHAR: return TextWrap::WrapWordChar;
            default:                   return TextWrap::WrapWord;
        }
    }

    static PangoUnderline ToPangoUnderline(UCUnderlineType type) {
        switch (type) {
            case UCUnderlineType::UnderlineSingle: return PANGO_UNDERLINE_SINGLE;
            case UCUnderlineType::UnderlineDouble: return PANGO_UNDERLINE_DOUBLE;
            case UCUnderlineType::UnderlineLow:    return PANGO_UNDERLINE_LOW;
            case UCUnderlineType::UnderlineError:  return PANGO_UNDERLINE_ERROR;
            default:                               return PANGO_UNDERLINE_NONE;
        }
    }

    static PangoVariant ToPangoVariant(UCFontVariant variant) {
        switch (variant) {
            case UCFontVariant::VariantSmallCaps: return PANGO_VARIANT_SMALL_CAPS;
            default:                              return PANGO_VARIANT_NORMAL;
        }
    }

    static PangoStretch ToPangoStretch(UCFontStretch stretch) {
        switch (stretch) {
            case UCFontStretch::UltraCondensed: return PANGO_STRETCH_ULTRA_CONDENSED;
            case UCFontStretch::ExtraCondensed: return PANGO_STRETCH_EXTRA_CONDENSED;
            case UCFontStretch::Condensed:      return PANGO_STRETCH_CONDENSED;
            case UCFontStretch::SemiCondensed:  return PANGO_STRETCH_SEMI_CONDENSED;
            case UCFontStretch::SemiExpanded:   return PANGO_STRETCH_SEMI_EXPANDED;
            case UCFontStretch::Expanded:       return PANGO_STRETCH_EXPANDED;
            case UCFontStretch::ExtraExpanded:  return PANGO_STRETCH_EXTRA_EXPANDED;
            case UCFontStretch::UltraExpanded:  return PANGO_STRETCH_ULTRA_EXPANDED;
            default:                            return PANGO_STRETCH_NORMAL;
        }
    }

    // Creates a PangoFontDescription from FontStyle (mirrors RenderContextCairo::CreatePangoFont)
    static PangoFontDescription* CreatePangoFontDesc(const FontStyle& style) {
        PangoFontDescription* desc = pango_font_description_new();
        if (!desc) return nullptr;

        // Resolve font family
        if (style.fontFamily.empty()) {
            auto* app = UltraCanvasApplication::GetInstance();
            std::string resolved = app ? app->GetSystemFontStyle().fontFamily : "Sans";
            pango_font_description_set_family(desc, resolved.c_str());
        } else {
            pango_font_description_set_family(desc, style.fontFamily.c_str());
        }

        double fontSize = (style.fontSize > 0) ? style.fontSize : 12.0;
        pango_font_description_set_size(desc, static_cast<int>(fontSize * PANGO_SCALE));
        pango_font_description_set_weight(desc, ToPangoWeight(style.fontWeight));
        pango_font_description_set_style(desc, ToPangoStyle(style.fontSlant));

        return desc;
    }

    // Convert UltraCanvas Color (0-255) to Pango color component (0-65535)
    static guint16 ColorToPango(uint8_t c) {
        return static_cast<guint16>(c) * 257;
    }

    // =========================================================================
    // UCTextAttribute
    // =========================================================================

    UCTextAttribute::~UCTextAttribute() {
        if (attr) {
            pango_attribute_destroy(attr);
        }
    }

    UCTextAttribute::UCTextAttribute(UCTextAttribute&& other) noexcept
        : attr(other.attr) {
        other.attr = nullptr;
    }

    UCTextAttribute& UCTextAttribute::operator=(UCTextAttribute&& other) noexcept {
        if (this != &other) {
            if (attr) pango_attribute_destroy(attr);
            attr = other.attr;
            other.attr = nullptr;
        }
        return *this;
    }

    ITextAttribute& UCTextAttribute::SetRange(int startIndex, int endIndex) {
        if (attr) {
            attr->start_index = static_cast<guint>(startIndex);
            attr->end_index = static_cast<guint>(endIndex);
        }
        return *this;
    }

    void* UCTextAttribute::Release() {
        PangoAttribute* released = attr;
        attr = nullptr;
        return released;
    }

    TextAttributeType UCTextAttribute::GetType() {
        if (attr) {
            return static_cast<TextAttributeType>(attr->klass->type);
        }
        return TextAttributeType::INVALID;
    }


    // --- Factory methods ---
    namespace TextAttributeFactory {
        std::unique_ptr<ITextAttribute> CreateFontStyle(const FontStyle &fontStyle) {
            PangoFontDescription *desc = CreatePangoFontDesc(fontStyle);
            if (!desc) return std::make_unique<UCTextAttribute>(nullptr);
            PangoAttribute *a = pango_attr_font_desc_new(desc);
            pango_font_description_free(desc);
            return std::make_unique<UCTextAttribute>(a);
        }

        std::unique_ptr<ITextAttribute> CreateFontDescFromPango(const PangoFontDescription *desc) {
            if (!desc) return std::make_unique<UCTextAttribute>(nullptr);
            return std::make_unique<UCTextAttribute>(pango_attr_font_desc_new(desc));
        }

        std::unique_ptr<ITextAttribute> CreateFamily(const std::string &family) {
            return std::make_unique<UCTextAttribute>(pango_attr_family_new(family.c_str()));
        }

        std::unique_ptr<ITextAttribute> CreateSize(float sizeInPoints) {
            return std::make_unique<UCTextAttribute>(pango_attr_size_new(static_cast<int>(sizeInPoints * PANGO_SCALE)));
        }

        std::unique_ptr<ITextAttribute> CreateAbsoluteSize(float sizeInPixels) {
            return std::make_unique<UCTextAttribute>(pango_attr_size_new_absolute(static_cast<int>(sizeInPixels * PANGO_SCALE)));
        }

        std::unique_ptr<ITextAttribute> CreateWeight(FontWeight weight) {
            return std::make_unique<UCTextAttribute>(pango_attr_weight_new(ToPangoWeight(weight)));
        }

        std::unique_ptr<ITextAttribute> CreateStyle(FontSlant slant) {
            return std::make_unique<UCTextAttribute>(pango_attr_style_new(ToPangoStyle(slant)));
        }

        std::unique_ptr<ITextAttribute> CreateVariant(UCFontVariant variant) {
            return std::make_unique<UCTextAttribute>(pango_attr_variant_new(ToPangoVariant(variant)));
        }

        std::unique_ptr<ITextAttribute> CreateStretch(UCFontStretch stretch) {
            return std::make_unique<UCTextAttribute>(pango_attr_stretch_new(ToPangoStretch(stretch)));
        }

        std::unique_ptr<ITextAttribute> CreateUnderline(UCUnderlineType type) {
            return std::make_unique<UCTextAttribute>(pango_attr_underline_new(ToPangoUnderline(type)));
        }

        std::unique_ptr<ITextAttribute> CreateUnderlineColor(const Color &color) {
            return std::make_unique<UCTextAttribute>(pango_attr_underline_color_new(
                    ColorToPango(color.r), ColorToPango(color.g), ColorToPango(color.b)));
        }

        std::unique_ptr<ITextAttribute> CreateStrikethrough(bool enabled) {
            return std::make_unique<UCTextAttribute>(pango_attr_strikethrough_new(enabled ? TRUE : FALSE));
        }

        std::unique_ptr<ITextAttribute> CreateStrikethroughColor(const Color &color) {
            return std::make_unique<UCTextAttribute>(pango_attr_strikethrough_color_new(
                    ColorToPango(color.r), ColorToPango(color.g), ColorToPango(color.b)));
        }

        std::unique_ptr<ITextAttribute> CreateForeground(const Color &color) {
            return std::make_unique<UCTextAttribute>(pango_attr_foreground_new(
                    ColorToPango(color.r), ColorToPango(color.g), ColorToPango(color.b)));
        }

        std::unique_ptr<ITextAttribute> CreateBackground(const Color &color) {
            return std::make_unique<UCTextAttribute>(pango_attr_background_new(
                    ColorToPango(color.r), ColorToPango(color.g), ColorToPango(color.b)));
        }

        std::unique_ptr<ITextAttribute> CreateForegroundAlpha(uint16_t alpha) {
            return std::make_unique<UCTextAttribute>(pango_attr_foreground_alpha_new(alpha));
        }

        std::unique_ptr<ITextAttribute> CreateBackgroundAlpha(uint16_t alpha) {
            return std::make_unique<UCTextAttribute>(pango_attr_background_alpha_new(alpha));
        }

        std::unique_ptr<ITextAttribute> CreateLetterSpacing(int spacingInPixels) {
            return std::make_unique<UCTextAttribute>(pango_attr_letter_spacing_new(spacingInPixels * PANGO_SCALE));
        }

        std::unique_ptr<ITextAttribute> CreateRise(int riseInPixels) {
            return std::make_unique<UCTextAttribute>(pango_attr_rise_new(riseInPixels * PANGO_SCALE));
        }

        std::unique_ptr<ITextAttribute> CreateScale(double scaleFactor) {
            return std::make_unique<UCTextAttribute>(pango_attr_scale_new(scaleFactor));
        }

        std::unique_ptr<ITextAttribute> CreateFallback(bool enable) {
            return std::make_unique<UCTextAttribute>(pango_attr_fallback_new(enable ? TRUE : FALSE));
        }

        std::unique_ptr<ITextAttribute> CreateLanguage(const std::string &lang) {
            PangoLanguage *language = pango_language_from_string(lang.c_str());
            return std::make_unique<UCTextAttribute>(pango_attr_language_new(language));
        }
    }

    // =========================================================================
    // UCTextAttributeList
    // =========================================================================

//    UCTextAttributeList::UCTextAttributeList() {
//        attrList = pango_attr_list_new();
//    }
//
//    UCTextAttributeList::~UCTextAttributeList() {
//        if (attrList) {
//            pango_attr_list_unref(attrList);
//        }
//    }
//
//    UCTextAttributeList::UCTextAttributeList(UCTextAttributeList&& other) noexcept
//        : attrList(other.attrList) {
//        other.attrList = nullptr;
//    }
//
//    UCTextAttributeList& UCTextAttributeList::operator=(UCTextAttributeList&& other) noexcept {
//        if (this != &other) {
//            if (attrList) pango_attr_list_unref(attrList);
//            attrList = other.attrList;
//            other.attrList = nullptr;
//        }
//        return *this;
//    }
//
//    void UCTextAttributeList::Insert(UCTextAttribute& attr) {
//        if (attrList) {
//            PangoAttribute* raw = attr.Release();
//            if (raw) pango_attr_list_insert(attrList, raw);
//        }
//    }
//
//    void UCTextAttributeList::InsertBefore(UCTextAttribute& attr) {
//        if (attrList) {
//            PangoAttribute* raw = attr.Release();
//            if (raw) pango_attr_list_insert_before(attrList, raw);
//        }
//    }
//
//    void UCTextAttributeList::Change(UCTextAttribute& attr) {
//        if (attrList) {
//            PangoAttribute* raw = attr.Release();
//            if (raw) pango_attr_list_change(attrList, raw);
//        }
//    }
//
//    PangoAttrList* UCTextAttributeList::GetHandle() const {
//        return attrList;
//    }
//
//    bool UCTextAttributeList::IsValid() const {
//        return attrList != nullptr;
//    }
//
//    std::string UCTextAttributeList::ToString() const {
//        if (!attrList) return {};
//        char* str = pango_attr_list_to_string(attrList);
//        if (!str) return {};
//        std::string result(str);
//        g_free(str);
//        return result;
//    }
//
//    UCTextAttributeList UCTextAttributeList::FromString(const std::string& str) {
//        UCTextAttributeList list;
//        if (list.attrList) {
//            pango_attr_list_unref(list.attrList);
//        }
//        list.attrList = pango_attr_list_from_string(str.c_str());
//        return list;
//    }
//
//    // Trampoline for pango_attr_list_filter: forwards to the std::function stored in user_data
//    static gboolean AttrFilterFunc(PangoAttribute* attr, gpointer user_data) {
//        auto* predicate = static_cast<std::function<bool(const PangoAttribute*)>*>(user_data);
//        return (*predicate)(attr) ? TRUE : FALSE;
//    }
//
//    UCTextAttributeList UCTextAttributeList::Filter(std::function<bool(const PangoAttribute*)> predicate) {
//        UCTextAttributeList result;
//        if (!attrList) return result;
//        if (result.attrList) {
//            pango_attr_list_unref(result.attrList);
//        }
//        result.attrList = pango_attr_list_filter(attrList, AttrFilterFunc, &predicate);
//        return result;
//    }

    // =========================================================================
    // UCTextLayout
    // =========================================================================

    UCTextLayout::UCTextLayout(PangoContext *pangoCtx) {
        if (pangoCtx) {
            layout = pango_layout_new(pangoCtx);
        }
        if (!layout) {
            throw std::runtime_error("ERROR: UCTextLayout - Failed to create Pango layout");
        }
    }

    UCTextLayout::~UCTextLayout() {
        if (attrsList) {
            pango_attr_list_unref(attrsList);
        }
        if (layout) {
            g_object_unref(layout);
        }
    }

//    UCTextLayout::UCTextLayout(UCTextLayout&& other) noexcept
//        : layout(other.layout) {
//        other.layout = nullptr;
//    }
//
//    UCTextLayout& UCTextLayout::operator=(UCTextLayout&& other) noexcept {
//        if (this != &other) {
//            if (layout) g_object_unref(layout);
//            layout = other.layout;
//            other.layout = nullptr;
//        }
//        return *this;
//    }

    bool UCTextLayout::IsValid() const {
        return layout != nullptr;
    }

    void* UCTextLayout::GetHandle() const {
        return layout;
    }

    // ===== TEXT CONTENT =====

    void UCTextLayout::SetText(const std::string& text) {
        extentsValid = false;
        pango_layout_set_text(layout, text.c_str(), static_cast<int>(text.length()));
    }

    std::string UCTextLayout::GetText() const {
        if (!layout) return {};
        const char* text = pango_layout_get_text(layout);
        return text ? std::string(text) : std::string();
    }

    void UCTextLayout::SetMarkup(const std::string& markup) {
        extentsValid = false;
        pango_layout_set_markup(layout, markup.c_str(), static_cast<int>(markup.length()));
    }

    // ===== FONT =====

    void UCTextLayout::SetFontStyle(const FontStyle& fontStyle) {
        PangoFontDescription* desc = CreatePangoFontDesc(fontStyle);
        if (desc) {
            extentsValid = false;
            pango_layout_set_font_description(layout, desc);
            pango_font_description_free(desc);
        }
    }

//    void UCTextLayout::SetFontDescriptionFromPango(const PangoFontDescription* desc) {
//        if (desc) {
//            pango_layout_set_font_description(layout, desc);
//        }
//    }

    // ===== DIMENSIONS =====

    void UCTextLayout::SetExplicitWidth(int widthPixels) {
        extentsValid = false;
        pango_layout_set_width(layout, (widthPixels < 0) ? -1 : widthPixels * PANGO_SCALE);
    }

    int UCTextLayout::GetExplicitWidth() const {
        if (!layout) return -1;
        int w = pango_layout_get_width(layout);
        return (w < 0) ? -1 : w / PANGO_SCALE;
    }

    void UCTextLayout::SetExplicitHeight(int heightPixels) {
        extentsValid = false;
        explicitHeight = heightPixels;
        pango_layout_set_height(layout, (heightPixels < 0) ? -1 : heightPixels * PANGO_SCALE);
    }

    int UCTextLayout::GetExplicitHeight() const {
        if (!layout) return -1;
        int h = pango_layout_get_height(layout);
        return (h < 0) ? -1 : h / PANGO_SCALE;
    }

    int UCTextLayout::GetLayoutVerticalOffset()  {
        auto ext = GetLayoutExtents();
        int offset = -ext.logical.y;
        if (explicitHeight > 0 && explicitHeight > ext.logical.height) {
            if (valign == VerticalAlignment::Middle) {
                offset = static_cast<int>(round(static_cast<float>(explicitHeight - ext.logical.height) / 2.0f)) - ext.logical.y;
            } else if (valign == VerticalAlignment::Bottom) {
                offset = explicitHeight - ext.logical.height - ext.logical.y;
            }
        }
        return offset;
    }

    // ===== ALIGNMENT & JUSTIFICATION =====

    void UCTextLayout::SetAlignment(TextAlignment align) {
        extentsValid = false;
        if (align == TextAlignment::Justify) {
            pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
            pango_layout_set_justify(layout, TRUE);
        } else {
            pango_layout_set_justify(layout, FALSE);
            pango_layout_set_alignment(layout, ToPangoAlignment(align));
        }
    }

    TextAlignment UCTextLayout::GetAlignment() const {
        if (!layout) return TextAlignment::Left;
        if (pango_layout_get_justify(layout)) {
            return TextAlignment::Justify;
        }
        return FromPangoAlignment(pango_layout_get_alignment(layout));
    }

    void UCTextLayout::SetVerticalAlignment(VerticalAlignment align) {
        valign = align;
    }

    // ===== WRAPPING & ELLIPSIZATION =====

    void UCTextLayout::SetWrap(TextWrap wrap) {
        extentsValid = false;
        if (wrap == TextWrap::WrapNone) {
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        } else {
            pango_layout_set_wrap(layout, ToPangoWrap(wrap));
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
        }
    }

    TextWrap UCTextLayout::GetWrap() const {
        if (!layout) return TextWrap::WrapWord;
        if (pango_layout_get_ellipsize(layout) == PANGO_ELLIPSIZE_END &&
            pango_layout_get_width(layout) >= 0) {
            return TextWrap::WrapNone;
        }
        return FromPangoWrap(pango_layout_get_wrap(layout));
    }

    void UCTextLayout::SetEllipsize(EllipsizeMode mode) {
        extentsValid = false;
        pango_layout_set_ellipsize(layout, ToPangoEllipsize(mode));
    }

    EllipsizeMode UCTextLayout::GetEllipsize() const {
        if (!layout) return EllipsizeMode::EllipsizeNone;
        return FromPangoEllipsize(pango_layout_get_ellipsize(layout));
    }

    // ===== SPACING & INDENTATION =====

    void UCTextLayout::SetIndent(int indentPixels) {
        extentsValid = false;
        pango_layout_set_indent(layout, indentPixels * PANGO_SCALE);
    }

    int UCTextLayout::GetIndent() const {
        if (!layout) return 0;
        return pango_layout_get_indent(layout) / PANGO_SCALE;
    }

    void UCTextLayout::SetLineSpacing(float factor) {
        extentsValid = false;
        pango_layout_set_line_spacing(layout, factor);
    }

    float UCTextLayout::GetLineSpacing() const {
        if (!layout) return 1.0f;
        return pango_layout_get_line_spacing(layout);
    }

    void UCTextLayout::SetSpacing(int spacingPixels) {
        extentsValid = false;
        pango_layout_set_spacing(layout, spacingPixels * PANGO_SCALE);
    }

    int UCTextLayout::GetSpacing() const {
        if (!layout) return 0;
        return pango_layout_get_spacing(layout) / PANGO_SCALE;
    }

    // ===== MODE =====

    void UCTextLayout::SetSingleParagraphMode(bool single) {
        extentsValid = false;
        pango_layout_set_single_paragraph_mode(layout, single ? TRUE : FALSE);
    }

    bool UCTextLayout::GetSingleParagraphMode() const {
        if (!layout) return false;
        return pango_layout_get_single_paragraph_mode(layout) != FALSE;
    }

    void UCTextLayout::SetAutoDir(bool autoDir) {
        extentsValid = false;
        pango_layout_set_auto_dir(layout, autoDir ? TRUE : FALSE);
    }

    bool UCTextLayout::GetAutoDir() const {
        if (!layout) return true;
        return pango_layout_get_auto_dir(layout) != FALSE;
    }

    // ===== ATTRIBUTES =====

    void UCTextLayout::InsertAttribute(std::unique_ptr<ITextAttribute> attr) {
        auto atr = static_cast<PangoAttribute *>(attr->Release());
        if (!attrsList) {
            attrsList = pango_attr_list_new();
            pango_layout_set_attributes(layout, attrsList);
        }
        pango_attr_list_insert(attrsList, atr);
        extentsValid = false;
    }

    void UCTextLayout::ChangeAttribute(std::unique_ptr<ITextAttribute> attr) {
        auto atr = static_cast<PangoAttribute *>(attr->Release());
        if (!attrsList) {
            attrsList = pango_attr_list_new();
            pango_layout_set_attributes(layout, attrsList);
        }
        pango_attr_list_change(attrsList, atr);
        extentsValid = false;
    }

    void UCTextLayout::SetAttributesFromString(const std::string& str) {
        if (attrsList) {
            pango_attr_list_unref(attrsList);
        }
        attrsList = pango_attr_list_from_string(str.c_str());
        pango_layout_set_attributes(layout, attrsList);
        extentsValid = false;
    }

    void UCTextLayout::RemoveAllAttributes() {
        pango_attr_list_unref(attrsList);
        attrsList = nullptr;
        pango_layout_set_attributes(layout, nullptr);
        extentsValid = false;
    }

//    void UCTextLayout::SetAttributes(const UCTextAttributeList& attrs) {
//        if (layout && attrs.IsValid()) {
//            pango_layout_set_attributes(layout, attrs.GetHandle());
//        }
//    }
//
//    PangoAttrList* UCTextLayout::GetAttributesHandle() const {
//        if (!layout) return nullptr;
//        return pango_layout_get_attributes(layout);
//    }

    // ===== TABS =====

    void UCTextLayout::ResetTabs() {
        if (layout) {
            pango_layout_set_tabs(layout, nullptr);
        }
    }

    void UCTextLayout::SetTabs(const std::vector<UCLayoutTabPos>& tabs) {
        if (layout) {
            if (tabs.empty()) {
                pango_layout_set_tabs(layout, nullptr);
            } else {
                PangoTabArray *tabArray = pango_tab_array_new(tabs.size(), true);
                for(int i = 0; i < tabs.size(); i++) {
                    pango_tab_array_set_tab(tabArray, i, static_cast<PangoTabAlign>(tabs[i].align), tabs[i].xPos);
                }
                pango_layout_set_tabs(layout, tabArray);
                pango_tab_array_free(tabArray);
            }
        }
    }

    std::vector<UCLayoutTabPos> UCTextLayout::GetTabs() {
        std::vector<UCLayoutTabPos> tabs;
        if (!layout) return tabs;

        PangoTabArray *tabArray = pango_layout_get_tabs(layout);
        if (!tabArray) return tabs;

        gint tabsSize = pango_tab_array_get_size(tabArray);
        for(gint i = 0; i < tabsSize; i++) {
            UCLayoutTabPos tab;
            pango_tab_array_get_tab(tabArray, i, reinterpret_cast<PangoTabAlign *>(&tab.align), &tab.xPos);
            tabs.push_back(tab);
        }
        pango_tab_array_free(tabArray);
        return tabs;
    }

    // ===== MEASUREMENT =====

    UCLayoutExtents UCTextLayout::GetLayoutExtents() {
        if (!extentsValid) {
            PangoRectangle ink, logical;
            pango_layout_get_pixel_extents(layout, &ink, &logical);
            extents.ink = Rect2Di(ink.x, ink.y, ink.width, ink.height);
            extents.logical = Rect2Di(logical.x, logical.y, logical.width, logical.height);
            extentsValid = true;
        }
        return extents;
    }

//    void UCTextLayout::GetSize(int& widthPangoUnits, int& heightPangoUnits) const {
//        if (!layout) {
//            widthPangoUnits = 0;
//            heightPangoUnits = 0;
//            return;
//        }
//        pango_layout_get_size(layout, &widthPangoUnits, &heightPangoUnits);
//    }

    int UCTextLayout::GetBaseline() const {
        return pango_layout_get_baseline(layout) / PANGO_SCALE;
    }

//    int UCTextLayout::GetBaselinePangoUnits() const {
//        if (!layout) return 0;
//        return pango_layout_get_baseline(layout);
//    }

    int UCTextLayout::GetLineCount() const {
        return pango_layout_get_line_count(layout);
    }

    // ===== HIT TESTING & POSITION =====

    UCLayoutHitResult UCTextLayout::XYToIndex(int pixelX, int pixelY) const {
        UCLayoutHitResult result{-1, 0, false};
        if (!layout) return result;
        int index, trailing;
        gboolean inside = pango_layout_xy_to_index(layout,
            pixelX * PANGO_SCALE, pixelY * PANGO_SCALE, &index, &trailing);
        result.index = index;
        result.trailing = trailing;
        result.inside = inside != FALSE;
        return result;
    }

    Rect2Di UCTextLayout::IndexToPos(int byteIndex) const {
        if (!layout) return {};
        PangoRectangle pos;
        pango_layout_index_to_pos(layout, byteIndex, &pos);
        return Rect2Di(pos.x / PANGO_SCALE, pos.y / PANGO_SCALE,
                       pos.width / PANGO_SCALE, pos.height / PANGO_SCALE);
    }

    UCLayoutLineXPos UCTextLayout::IndexToLineX(int byteIndex, bool trailing) const {
        UCLayoutLineXPos result{0, 0};
        if (!layout) return result;
        int line, xPos;
        pango_layout_index_to_line_x(layout, byteIndex, trailing ? TRUE : FALSE, &line, &xPos);
        result.line = line;
        result.xPos = xPos / PANGO_SCALE;
        return result;
    }

    UCCursorPos UCTextLayout::GetCursorPos(int byteIndex) const {
        UCCursorPos result{};
        if (!layout) return result;
        PangoRectangle strong, weak;
        pango_layout_get_cursor_pos(layout, byteIndex, &strong, &weak);
        result.strongPos = Rect2Di(strong.x / PANGO_SCALE, strong.y / PANGO_SCALE,
                                   strong.width / PANGO_SCALE, strong.height / PANGO_SCALE);
        result.weakPos = Rect2Di(weak.x / PANGO_SCALE, weak.y / PANGO_SCALE,
                                 weak.width / PANGO_SCALE, weak.height / PANGO_SCALE);
        return result;
    }

    UCCursorMoveResult UCTextLayout::MoveCursorVisually(bool strongCursor, int oldIndex,
                                                        int oldTrailing, int direction) const {
        UCCursorMoveResult result{oldIndex, oldTrailing};
        if (!layout) return result;
        int newIndex, newTrailing;
        pango_layout_move_cursor_visually(layout, strongCursor ? TRUE : FALSE,
            oldIndex, oldTrailing, direction, &newIndex, &newTrailing);
        result.newIndex = newIndex;
        result.newTrailing = newTrailing;
        return result;
    }

    // ===== LINE ACCESS =====

//    PangoLayoutLine* UCTextLayout::GetLine(int lineIndex) const {
//        if (!layout) return nullptr;
//        return pango_layout_get_line(layout, lineIndex);
//    }
//
//    PangoLayoutLine* UCTextLayout::GetLineReadonly(int lineIndex) const {
//        if (!layout) return nullptr;
//        return pango_layout_get_line_readonly(layout, lineIndex);
//    }
//
//    GSList* UCTextLayout::GetLines() const {
//        if (!layout) return nullptr;
//        return pango_layout_get_lines(layout);
//    }
//
//    GSList* UCTextLayout::GetLinesReadonly() const {
//        if (!layout) return nullptr;
//        return pango_layout_get_lines_readonly(layout);
//    }
//
//    // ===== ITERATOR =====
//
//    UCTextLayoutIter UCTextLayout::GetIter() const {
//        if (!layout) return UCTextLayoutIter(nullptr);
//        return UCTextLayoutIter(pango_layout_get_iter(layout));
//    }
//
//    // ===== RENDERING =====
//
//    void UCTextLayout::Show(cairo_t* cr) const {
//        if (layout && cr) {
//            pango_cairo_update_layout(cr, layout);
//            pango_cairo_show_layout(cr, layout);
//        }
//    }
//
//    void UCTextLayout::ShowAt(cairo_t* cr, float x, float y) const {
//        if (layout && cr) {
//            cairo_save(cr);
//            cairo_move_to(cr, x, y);
//            pango_cairo_update_layout(cr, layout);
//            pango_cairo_show_layout(cr, layout);
//            cairo_restore(cr);
//        }
//    }

} // namespace UltraCanvas
