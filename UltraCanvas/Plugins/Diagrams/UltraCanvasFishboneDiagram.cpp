// Plugins/Diagrams/UltraCanvasFishboneDiagram.cpp
// Fishbone (Ishikawa) cause-and-effect diagram with eight design presets
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasFishboneDiagram.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <cmath>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

// =============================================================================
// LOCAL HELPERS
// =============================================================================

    namespace {

        Color MixColors(const Color& a, const Color& b, float t) {
            auto mix = [t](int ca, int cb) {
                return static_cast<uint8_t>(std::clamp(
                        static_cast<int>(ca + (cb - ca) * t + 0.5f), 0, 255));
            };
            return Color(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), 255);
        }

        Color DarkenColor(const Color& c, float t) {
            return MixColors(c, Color(0, 0, 0, 255), t);
        }

        Color WithAlpha(const Color& c, int alpha) {
            return Color(c.r, c.g, c.b, static_cast<uint8_t>(std::clamp(alpha, 0, 255)));
        }

        struct PaletteTable {
            const Color* colors;
            size_t count;
        };

        PaletteTable PaletteFor(FishbonePalette palette) {
            static const Color corporateBlue[] = {
                    Color(21, 101, 192, 255), Color(30, 136, 229, 255), Color(66, 165, 245, 255),
                    Color(13, 71, 161, 255),  Color(3, 155, 229, 255),  Color(38, 198, 218, 255),
                    Color(92, 107, 192, 255), Color(121, 134, 203, 255)};
            static const Color vibrant[] = {
                    Color(41, 182, 246, 255), Color(255, 167, 38, 255), Color(239, 83, 80, 255),
                    Color(102, 187, 106, 255), Color(171, 71, 188, 255), Color(255, 213, 79, 255),
                    Color(38, 198, 218, 255), Color(255, 112, 67, 255)};
            static const Color pastel[] = {
                    Color(144, 202, 249, 255), Color(255, 204, 128, 255), Color(239, 154, 154, 255),
                    Color(165, 214, 167, 255), Color(206, 147, 216, 255), Color(255, 245, 157, 255),
                    Color(128, 222, 234, 255), Color(255, 171, 145, 255)};
            static const Color ocean[] = {
                    Color(0, 105, 148, 255),  Color(0, 137, 168, 255),  Color(0, 168, 176, 255),
                    Color(38, 198, 218, 255), Color(77, 208, 225, 255), Color(2, 119, 189, 255),
                    Color(1, 87, 155, 255),   Color(129, 212, 250, 255)};
            static const Color sunset[] = {
                    Color(244, 81, 30, 255),  Color(251, 140, 0, 255),  Color(253, 216, 53, 255),
                    Color(216, 67, 21, 255),  Color(239, 108, 0, 255),  Color(255, 179, 0, 255),
                    Color(191, 54, 12, 255),  Color(255, 138, 101, 255)};
            static const Color forest[] = {
                    Color(46, 125, 50, 255),  Color(85, 139, 47, 255),  Color(124, 179, 66, 255),
                    Color(27, 94, 32, 255),   Color(104, 159, 56, 255), Color(158, 157, 36, 255),
                    Color(0, 121, 107, 255),  Color(129, 199, 132, 255)};
            static const Color slate[] = {
                    Color(69, 90, 100, 255),  Color(84, 110, 122, 255), Color(96, 125, 139, 255),
                    Color(55, 71, 79, 255),   Color(120, 144, 156, 255), Color(38, 50, 56, 255),
                    Color(144, 164, 174, 255), Color(176, 190, 197, 255)};
            static const Color mono[] = {
                    Color(33, 37, 41, 255),   Color(73, 80, 87, 255),   Color(108, 117, 125, 255),
                    Color(52, 58, 64, 255),   Color(134, 142, 150, 255), Color(90, 98, 104, 255),
                    Color(160, 168, 175, 255), Color(120, 128, 135, 255)};

            switch (palette) {
                case FishbonePalette::CorporateBlue: return {corporateBlue, 8};
                case FishbonePalette::Pastel:        return {pastel, 8};
                case FishbonePalette::Ocean:         return {ocean, 8};
                case FishbonePalette::Sunset:        return {sunset, 8};
                case FishbonePalette::Forest:        return {forest, 8};
                case FishbonePalette::Slate:         return {slate, 8};
                case FishbonePalette::Mono:          return {mono, 8};
                case FishbonePalette::Custom:
                case FishbonePalette::Vibrant:
                default:                             return {vibrant, 8};
            }
        }

        // One laid-out text row: a cause or a sub-cause.
        struct CauseEntry {
            FishboneRef ref;
            std::string text;
            int    indent = 0;      // 0 = cause, 1 = sub-cause
            bool   highlighted = false;
            bool   rootCause = false;
            double weight = 0.0;
            Color  marker = Color(0, 0, 0, 0);
        };

        void CollectCauseEntries(const std::vector<FishboneCause>& causes,
                                 int categoryIndex, int parentCause, int indent,
                                 bool includeSubCauses, FishboneCauseMarker markerStyle,
                                 std::vector<CauseEntry>& out) {
            for (size_t i = 0; i < causes.size(); ++i) {
                const FishboneCause& cause = causes[i];

                CauseEntry entry;
                entry.ref.category = categoryIndex;
                if (indent == 0) {
                    entry.ref.cause = static_cast<int>(i);
                } else {
                    entry.ref.cause = parentCause;
                    entry.ref.subCause = static_cast<int>(i);
                }
                entry.text = cause.text;
                if (markerStyle == FishboneCauseMarker::Numbered && indent == 0) {
                    entry.text = std::to_string(i + 1) + ". " + cause.text;
                }
                entry.indent = indent;
                entry.highlighted = cause.highlighted;
                entry.rootCause = cause.isRootCause;
                entry.weight = cause.weight;
                entry.marker = cause.markerColor;
                out.push_back(std::move(entry));

                if (includeSubCauses && indent == 0 && !cause.subCauses.empty()) {
                    CollectCauseEntries(cause.subCauses, categoryIndex,
                                        static_cast<int>(i), 1, includeSubCauses,
                                        markerStyle, out);
                }
            }
        }

    } // namespace

// =============================================================================
// CONSTRUCTION
// =============================================================================

    UltraCanvasFishboneDiagram::UltraCanvasFishboneDiagram(
            const std::string& id, int x, int y, int w, int h)
            : UltraCanvasChartElementBase(id, x, y, w, h) {
        enableSelection = true;
        enableTooltips = true;
        showGrid = false;
        showAxes = false;
        backgroundColor = Color(250, 250, 252, 255);
    }

// =============================================================================
// DESIGN & STYLE
// =============================================================================

    void UltraCanvasFishboneDiagram::SetDesign(FishboneDesign d) {
        if (design == d) return;
        design = d;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetSidePolicy(FishboneSidePolicy policy) {
        if (sidePolicy == policy) return;
        sidePolicy = policy;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetHeadShape(FishboneHeadShape shape) {
        if (headShape == shape) return;
        headShape = shape;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetTailShape(FishboneTailShape shape) {
        if (tailShape == shape) return;
        tailShape = shape;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetEffectPlacement(FishboneEffectPlacement placement) {
        if (effectPlacement == placement) return;
        effectPlacement = placement;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetCauseMarker(FishboneCauseMarker marker) {
        if (causeMarker == marker) return;
        causeMarker = marker;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetLabelPlacement(FishboneLabelPlacement placement) {
        if (labelPlacement == placement) return;
        labelPlacement = placement;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetPalette(FishbonePalette p) {
        if (palette == p) return;
        palette = p;
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetCustomPalette(const std::vector<Color>& colors) {
        customPalette = colors;
        if (!customPalette.empty()) palette = FishbonePalette::Custom;
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetDarkTheme(bool dark) {
        if (darkTheme == dark) return;
        darkTheme = dark;
        backgroundColor = dark ? Color(29, 32, 41, 255) : Color(250, 250, 252, 255);
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetRibAngle(double degrees) {
        double clamped = std::clamp(degrees, 25.0, 80.0);
        if (std::abs(clamped - ribAngleDegrees) < 0.01) return;
        ribAngleDegrees = clamped;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetSpineThickness(double thickness) {
        double clamped = std::clamp(thickness, 1.0, 40.0);
        if (std::abs(clamped - spineThickness) < 0.01) return;
        spineThickness = clamped;
        RequestRedraw();
    }

// =============================================================================
// VISUAL TOGGLES
// =============================================================================

    void UltraCanvasFishboneDiagram::SetShowCategoryIcons(bool show) {
        if (showCategoryIcons == show) return;
        showCategoryIcons = show;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetShowCauses(bool show) {
        if (showCauses == show) return;
        showCauses = show;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetShowSubCauses(bool show) {
        if (showSubCauses == show) return;
        showSubCauses = show;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetShowSpineCaption(bool show) {
        if (showSpineCaption == show) return;
        showSpineCaption = show;
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetSpineCaption(const std::string& caption) {
        spineCaption = caption;
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetShowWaypoints(bool show) {
        if (showWaypoints == show) return;
        showWaypoints = show;
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetShowWeights(bool show) {
        if (showWeights == show) return;
        showWeights = show;
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetCauseFontSize(float size) {
        float clamped = std::clamp(size, 6.0f, 24.0f);
        if (std::abs(clamped - causeFontSize) < 0.01f) return;
        causeFontSize = clamped;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetCategoryFontSize(float size) {
        float clamped = std::clamp(size, 7.0f, 28.0f);
        if (std::abs(clamped - categoryFontSize) < 0.01f) return;
        categoryFontSize = clamped;
        InvalidateLayout();
        RequestRedraw();
    }

// =============================================================================
// DOCUMENT
// =============================================================================

    void UltraCanvasFishboneDiagram::SetDocument(const FishboneDocument& doc) {
        document = doc;
        hoveredItem = FishboneRef();
        selectedItem = FishboneRef();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetEffect(const std::string& text) {
        document.effect = text;
        InvalidateLayout();
        RequestRedraw();
    }

    size_t UltraCanvasFishboneDiagram::AddCategory(const std::string& title) {
        document.categories.emplace_back(title);
        InvalidateLayout();
        RequestRedraw();
        return document.categories.size() - 1;
    }

    size_t UltraCanvasFishboneDiagram::AddCategory(const FishboneCategory& category) {
        document.categories.push_back(category);
        InvalidateLayout();
        RequestRedraw();
        return document.categories.size() - 1;
    }

    void UltraCanvasFishboneDiagram::SetCategories(const std::vector<FishboneCategory>& categories) {
        document.categories = categories;
        hoveredItem = FishboneRef();
        selectedItem = FishboneRef();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::RemoveCategory(size_t index) {
        if (index >= document.categories.size()) return;
        document.categories.erase(document.categories.begin() + index);

        auto fix = [&](FishboneRef& ref) {
            if (ref.category < 0) return;
            if (ref.category == static_cast<int>(index)) ref = FishboneRef();
            else if (ref.category > static_cast<int>(index)) ref.category--;
        };
        fix(selectedItem);
        fix(hoveredItem);

        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::ClearCategories() {
        document.categories.clear();
        hoveredItem = FishboneRef();
        selectedItem = FishboneRef();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetCategoryTitle(size_t index, const std::string& title) {
        if (index >= document.categories.size()) return;
        document.categories[index].title = title;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetCategoryAccent(size_t index, const Color& color) {
        if (index >= document.categories.size()) return;
        document.categories[index].accentColor = color;
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetCategoryIcon(size_t index, const std::string& iconPath) {
        if (index >= document.categories.size()) return;
        document.categories[index].iconPath = iconPath;
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetCategoryGlyph(size_t index, const std::string& glyph) {
        if (index >= document.categories.size()) return;
        document.categories[index].iconGlyph = glyph;
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetCategorySide(size_t index, int side) {
        if (index >= document.categories.size()) return;
        document.categories[index].side = (side == 0) ? 0 : (side < 0 ? -1 : 1);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetCategoryCollapsed(size_t index, bool collapsed) {
        if (index >= document.categories.size()) return;
        if (document.categories[index].collapsed == collapsed) return;
        document.categories[index].collapsed = collapsed;
        InvalidateLayout();
        RequestRedraw();
    }

    bool UltraCanvasFishboneDiagram::GetCategoryCollapsed(size_t index) const {
        if (index >= document.categories.size()) return false;
        return document.categories[index].collapsed;
    }

    size_t UltraCanvasFishboneDiagram::AddCause(size_t categoryIndex, const std::string& text) {
        return AddCause(categoryIndex, FishboneCause(text));
    }

    size_t UltraCanvasFishboneDiagram::AddCause(size_t categoryIndex, const FishboneCause& cause) {
        if (categoryIndex >= document.categories.size()) return SIZE_MAX;
        document.categories[categoryIndex].causes.push_back(cause);
        InvalidateLayout();
        RequestRedraw();
        return document.categories[categoryIndex].causes.size() - 1;
    }

    size_t UltraCanvasFishboneDiagram::AddSubCause(size_t categoryIndex, size_t causeIndex,
                                                    const std::string& text) {
        if (categoryIndex >= document.categories.size()) return SIZE_MAX;
        auto& causes = document.categories[categoryIndex].causes;
        if (causeIndex >= causes.size()) return SIZE_MAX;
        causes[causeIndex].subCauses.emplace_back(text);
        InvalidateLayout();
        RequestRedraw();
        return causes[causeIndex].subCauses.size() - 1;
    }

    void UltraCanvasFishboneDiagram::RemoveCause(size_t categoryIndex, size_t causeIndex) {
        if (categoryIndex >= document.categories.size()) return;
        auto& causes = document.categories[categoryIndex].causes;
        if (causeIndex >= causes.size()) return;
        causes.erase(causes.begin() + causeIndex);

        auto fix = [&](FishboneRef& ref) {
            if (ref.category != static_cast<int>(categoryIndex) || ref.cause < 0) return;
            if (ref.cause == static_cast<int>(causeIndex)) ref = FishboneRef();
            else if (ref.cause > static_cast<int>(causeIndex)) ref.cause--;
        };
        fix(selectedItem);
        fix(hoveredItem);

        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::ClearCauses(size_t categoryIndex) {
        if (categoryIndex >= document.categories.size()) return;
        document.categories[categoryIndex].causes.clear();
        if (selectedItem.category == static_cast<int>(categoryIndex)) selectedItem = FishboneRef();
        if (hoveredItem.category == static_cast<int>(categoryIndex)) hoveredItem = FishboneRef();
        InvalidateLayout();
        RequestRedraw();
    }

    size_t UltraCanvasFishboneDiagram::CountCauses(size_t categoryIndex) const {
        if (categoryIndex >= document.categories.size()) return 0;
        return document.categories[categoryIndex].causes.size();
    }

    size_t UltraCanvasFishboneDiagram::CountAllCauses(bool includeSubCauses) const {
        return CountFishboneCauses(document.categories, includeSubCauses);
    }

    const FishboneCause* UltraCanvasFishboneDiagram::FindCause(const FishboneRef& ref) const {
        if (ref.category < 0 || ref.category >= static_cast<int>(document.categories.size())) {
            return nullptr;
        }
        const auto& causes = document.categories[ref.category].causes;
        if (ref.cause < 0 || ref.cause >= static_cast<int>(causes.size())) return nullptr;
        if (ref.subCause < 0) return &causes[ref.cause];

        const auto& subs = causes[ref.cause].subCauses;
        if (ref.subCause >= static_cast<int>(subs.size())) return nullptr;
        return &subs[ref.subCause];
    }

    void UltraCanvasFishboneDiagram::SetCauseHighlighted(const FishboneRef& ref, bool highlighted) {
        auto* cause = const_cast<FishboneCause*>(FindCause(ref));
        if (!cause) return;
        cause->highlighted = highlighted;
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetCauseRootCause(const FishboneRef& ref, bool isRootCause) {
        auto* cause = const_cast<FishboneCause*>(FindCause(ref));
        if (!cause) return;
        cause->isRootCause = isRootCause;
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::SetCauseWeight(const FishboneRef& ref, double weight) {
        auto* cause = const_cast<FishboneCause*>(FindCause(ref));
        if (!cause) return;
        cause->weight = weight;
        RequestRedraw();
    }

// =============================================================================
// PRESETS, SAMPLES & TEXT INTERCHANGE
// =============================================================================

    void UltraCanvasFishboneDiagram::LoadCategoryPreset(FishboneCategoryPreset preset) {
        document.categories = FishboneCategoriesFromPreset(preset);
        hoveredItem = FishboneRef();
        selectedItem = FishboneRef();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::LoadSampleData() {
        SetDocument(FishboneSamples::QualityControl());
    }

    bool UltraCanvasFishboneDiagram::LoadOutline(const std::string& text, std::string* error) {
        std::string parseError;
        FishboneDocument parsed = ParseFishboneOutline(text, &parseError);
        if (error) *error = parseError;
        if (parsed.categories.empty()) return false;
        SetDocument(parsed);
        return true;
    }

    std::string UltraCanvasFishboneDiagram::ExportOutline() const {
        return ExportFishboneOutline(document);
    }

    bool UltraCanvasFishboneDiagram::LoadMermaid(const std::string& text, std::string* error) {
        std::string parseError;
        FishboneDocument parsed = ParseMermaidIshikawa(text, &parseError);
        if (error) *error = parseError;
        if (parsed.categories.empty()) return false;
        SetDocument(parsed);
        return true;
    }

    std::string UltraCanvasFishboneDiagram::ExportMermaid() const {
        return ExportMermaidIshikawa(document);
    }

// =============================================================================
// SELECTION & GEOMETRY QUERIES
// =============================================================================

    void UltraCanvasFishboneDiagram::SetSelectedItem(const FishboneRef& ref) {
        if (selectedItem == ref) return;
        selectedItem = ref;
        if (onSelectionChange) onSelectionChange();
        RequestRedraw();
    }

    void UltraCanvasFishboneDiagram::ClearSelection() {
        if (!selectedItem.IsValid()) return;
        selectedItem = FishboneRef();
        RequestRedraw();
    }

    bool UltraCanvasFishboneDiagram::GetSpineLine(Point2Dd& start, Point2Dd& end) const {
        if (!layout.valid) return false;
        start = layout.spineStart;
        end = layout.spineEnd;
        return true;
    }

    bool UltraCanvasFishboneDiagram::GetCategoryLabelRect(size_t index, Rect2Dd& rect) const {
        if (!layout.valid) return false;
        for (const auto& slot : layout.categories) {
            if (slot.index == static_cast<int>(index)) {
                rect = slot.labelRect;
                return true;
            }
        }
        return false;
    }

    bool UltraCanvasFishboneDiagram::GetCategoryColor(size_t index, Color& color) const {
        if (index >= document.categories.size()) return false;
        color = CategoryAccent(index);
        return true;
    }

// =============================================================================
// THEME
// =============================================================================

    Color UltraCanvasFishboneDiagram::PaletteColor(size_t index) const {
        if (palette == FishbonePalette::Custom && !customPalette.empty()) {
            return customPalette[index % customPalette.size()];
        }
        PaletteTable table = PaletteFor(palette);
        return table.colors[index % table.count];
    }

    Color UltraCanvasFishboneDiagram::CategoryAccent(size_t index) const {
        if (index < document.categories.size() && document.categories[index].accentColor.a > 0) {
            return document.categories[index].accentColor;
        }
        return PaletteColor(index);
    }

    Color UltraCanvasFishboneDiagram::DiagramTextColor() const {
        return darkTheme ? Color(236, 239, 244, 255) : Color(33, 37, 41, 255);
    }

    Color UltraCanvasFishboneDiagram::MutedTextColor() const {
        return darkTheme ? Color(154, 162, 178, 255) : Color(108, 117, 125, 255);
    }

    Color UltraCanvasFishboneDiagram::SpineColor() const {
        return darkTheme ? Color(74, 82, 100, 255) : Color(52, 58, 64, 255);
    }

    Color UltraCanvasFishboneDiagram::SurfaceColor() const {
        return darkTheme ? Color(42, 46, 58, 255) : Color(255, 255, 255, 255);
    }

// =============================================================================
// TEXT HELPERS
// =============================================================================

    std::vector<std::string> UltraCanvasFishboneDiagram::WrapText(
            IRenderContext* ctx, const std::string& text, double maxWidth) const {
        std::vector<std::string> lines;
        if (maxWidth < 8.0) maxWidth = 8.0;

        std::stringstream paragraphs(text);
        std::string paragraph;
        while (std::getline(paragraphs, paragraph, '\n')) {
            std::stringstream words(paragraph);
            std::string word, current;
            while (words >> word) {
                std::string candidate = current.empty() ? word : current + " " + word;
                if (!current.empty() &&
                    ctx->GetTextLineDimensions(candidate).width > maxWidth) {
                    lines.push_back(current);
                    current = word;
                } else {
                    current = candidate;
                }
            }
            lines.push_back(current);
        }
        if (lines.empty()) lines.push_back("");
        return lines;
    }

    std::string UltraCanvasFishboneDiagram::ElideText(
            IRenderContext* ctx, const std::string& text, double maxWidth) const {
        if (text.empty() || maxWidth <= 0.0) return text;
        if (ctx->GetTextLineDimensions(text).width <= maxWidth) return text;

        std::string result = text;
        while (!result.empty()) {
            result.pop_back();
            if (ctx->GetTextLineDimensions(result + "...").width <= maxWidth) {
                return result + "...";
            }
        }
        return std::string();
    }

    void UltraCanvasFishboneDiagram::DrawTextLines(
            IRenderContext* ctx, const std::vector<std::string>& lines,
            const Rect2Dd& rect, bool rightAligned, double lineHeight) {
        double y = rect.y;
        for (const auto& line : lines) {
            double x = rect.x;
            if (rightAligned) {
                Size2Di size = ctx->GetTextLineDimensions(line);
                x = rect.x + rect.width - size.width;
            }
            ctx->DrawText(line, Point2Dd(x, y));
            y += lineHeight;
        }
    }

    std::string UltraCanvasFishboneDiagram::CauseLabelText(
            const FishboneRef& ref, const FishboneCause& cause, size_t ordinal) const {
        if (causeMarker == FishboneCauseMarker::Numbered && ref.subCause < 0) {
            return std::to_string(ordinal + 1) + ". " + cause.text;
        }
        return cause.text;
    }

    std::string UltraCanvasFishboneDiagram::BuildTooltip(const FishboneRef& ref) const {
        if (!ref.IsValid() || ref.category >= static_cast<int>(document.categories.size())) {
            return std::string();
        }
        const FishboneCategory& category = document.categories[ref.category];

        if (ref.IsCategory()) {
            std::ostringstream out;
            out << category.title << "\n"
                << category.causes.size() << " cause"
                << (category.causes.size() == 1 ? "" : "s");
            if (!document.effect.empty()) out << "\nEffect: " << document.effect;
            return out.str();
        }

        const FishboneCause* cause = FindCause(ref);
        if (!cause) return category.title;
        if (!cause->tooltip.empty()) return cause->tooltip;

        std::ostringstream out;
        out << category.title << "\n" << cause->text;
        if (cause->isRootCause) out << "\n[root cause]";
        if (cause->weight > 0.0) out << "\nWeight: " << cause->weight;
        if (!cause->subCauses.empty()) {
            out << "\n" << cause->subCauses.size() << " sub-cause"
                << (cause->subCauses.size() == 1 ? "" : "s");
        }
        return out.str();
    }

// =============================================================================
// LAYOUT - SHARED
// =============================================================================

    int UltraCanvasFishboneDiagram::ResolveSide(size_t index) const {
        switch (sidePolicy) {
            case FishboneSidePolicy::AllAbove: return -1;
            case FishboneSidePolicy::AllBelow: return 1;
            case FishboneSidePolicy::PerCategory:
                if (index < document.categories.size() && document.categories[index].side != 0) {
                    return document.categories[index].side;
                }
                return (index % 2 == 0) ? -1 : 1;
            case FishboneSidePolicy::Alternate:
            default:
                return (index % 2 == 0) ? -1 : 1;
        }
    }

    double UltraCanvasFishboneDiagram::HeadReserve(IRenderContext* ctx) const {
        double reserve = 0.0;
        switch (headShape) {
            case FishboneHeadShape::Hidden:   reserve = 10.0; break;
            case FishboneHeadShape::Arrow:    reserve = 26.0; break;
            case FishboneHeadShape::Triangle: reserve = 52.0; break;
            case FishboneHeadShape::FishHead: reserve = 54.0; break;
            case FishboneHeadShape::Box:      reserve = 90.0; break;
        }
        if (effectPlacement == FishboneEffectPlacement::HeadBox && ctx &&
            !document.effect.empty()) {
            ctx->SetFontSize(effectFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            Size2Di size = ctx->GetTextLineDimensions(document.effect);
            ctx->SetFontWeight(FontWeight::Normal);
            // The head box wraps to at most a third of the element width
            double boxWidth = std::min<double>(size.width + 26.0, GetWidth() * 0.32);
            reserve = std::max(reserve, boxWidth + 12.0);
        }
        return reserve;
    }

    double UltraCanvasFishboneDiagram::TailReserve() const {
        switch (tailShape) {
            case FishboneTailShape::Hidden:   return 8.0;
            case FishboneTailShape::Chevron:  return 22.0;
            case FishboneTailShape::Triangle: return 26.0;
            case FishboneTailShape::FishTail: return 36.0;
        }
        return 8.0;
    }

    void UltraCanvasFishboneDiagram::UpdateLayout(IRenderContext* ctx) {
        const double top = (effectPlacement == FishboneEffectPlacement::Title &&
                            !document.effect.empty()) ? 48.0 : 14.0;

        layout = FishboneLayout();
        layout.causeFont = causeFontSize;
        layout.diagramArea = Rect2Dd(
                14.0, top,
                std::max(60.0, GetWidth() - 28.0),
                std::max(60.0, GetHeight() - top - 14.0));

        switch (design) {
            case FishboneDesign::Vertical:     SolveVerticalLayout(ctx); break;
            case FishboneDesign::ChevronSpine: SolveLeaderLayout(ctx); break;
            case FishboneDesign::Columns:      SolveColumnLayout(ctx); break;
            default:                           SolveRibLayout(ctx); break;
        }
        layout.valid = true;
    }

// =============================================================================
// LAYOUT - CAUSE PLACEMENT
// =============================================================================

    double UltraCanvasFishboneDiagram::BuildRibCauses(
            IRenderContext* ctx, CategorySlot& slot, size_t categoryIndex,
            const Point2Dd& ribDir, double ribLength, double labelWidth,
            bool labelsRightAligned) {
        slot.causes.clear();
        slot.hiddenCauses = 0;

        if (!showCauses || categoryIndex >= document.categories.size()) return 0.0;
        const FishboneCategory& category = document.categories[categoryIndex];
        if (category.collapsed || category.causes.empty()) return 0.0;

        std::vector<CauseEntry> entries;
        CollectCauseEntries(category.causes, static_cast<int>(categoryIndex), -1, 0,
                            showSubCauses, causeMarker, entries);
        if (entries.empty()) return 0.0;

        const double sinTheta = std::max(0.15, std::abs(ribDir.y));
        const double lineHeight = layout.causeFont * 1.32;
        const double gap = 5.0;
        // Text set toward the head needs a wider standoff: the rib runs back
        // through the rows below the marker, so a narrow gap puts the first
        // characters on the stroke.
        const double markerGap = labelsRightAligned ? 10.0 : 17.0;
        const double indentStep = 12.0;

        ctx->SetFontSize(layout.causeFont);

        // Reserve room at the outer end for the category chip and at the root
        // end for the badge / spine furniture.
        const double startOffset = 16.0;
        const double usableLength = std::max(30.0, ribLength - 8.0);

        double along = startOffset;
        for (size_t i = 0; i < entries.size(); ++i) {
            const CauseEntry& entry = entries[i];
            const double indent = entry.indent * indentStep;
            const double width = std::max(46.0, labelWidth - indent);

            std::vector<std::string> lines = WrapText(ctx, entry.text, width);
            const double height = lines.size() * lineHeight;
            const double advance = (height + gap) / sinTheta;

            if (along + advance > usableLength) {
                slot.hiddenCauses = entries.size() - i;
                break;
            }

            const double center = along + advance * 0.5;
            Point2Dd marker(slot.root.x + ribDir.x * center,
                            slot.root.y + ribDir.y * center);

            CauseSlot cause;
            cause.ref = entry.ref;
            cause.marker = marker;
            cause.lines = std::move(lines);
            cause.rightAligned = labelsRightAligned;

            if (labelsRightAligned) {
                cause.textRect = Rect2Dd(marker.x - markerGap - width,
                                         marker.y - height * 0.5, width, height);
            } else {
                cause.textRect = Rect2Dd(marker.x + markerGap + indent,
                                         marker.y - height * 0.5, width, height);
            }
            cause.hitRect = Rect2Dd(cause.textRect.x - 4, cause.textRect.y - 3,
                                    cause.textRect.width + markerGap + 10,
                                    cause.textRect.height + 6);
            if (labelsRightAligned) {
                cause.hitRect.x = cause.textRect.x - 4;
            } else {
                cause.hitRect.x = marker.x - 6;
            }

            slot.causes.push_back(std::move(cause));
            along += advance;
        }
        return along;
    }

    void UltraCanvasFishboneDiagram::BuildListCauses(
            IRenderContext* ctx, CategorySlot& slot, size_t categoryIndex,
            const Rect2Dd& area, bool centered) {
        slot.causes.clear();
        slot.hiddenCauses = 0;

        if (!showCauses || categoryIndex >= document.categories.size()) return;
        const FishboneCategory& category = document.categories[categoryIndex];
        if (category.collapsed || category.causes.empty()) return;

        std::vector<CauseEntry> entries;
        CollectCauseEntries(category.causes, static_cast<int>(categoryIndex), -1, 0,
                            showSubCauses, causeMarker, entries);
        if (entries.empty()) return;

        const double lineHeight = layout.causeFont * 1.32;
        const double gap = 6.0;
        const double markerGap = (causeMarker == FishboneCauseMarker::Hidden) ? 0.0 : 12.0;
        const double indentStep = 10.0;

        ctx->SetFontSize(layout.causeFont);

        double y = area.y;
        for (size_t i = 0; i < entries.size(); ++i) {
            const CauseEntry& entry = entries[i];
            const double indent = entry.indent * indentStep;
            const double width = std::max(40.0, area.width - markerGap - indent);

            std::vector<std::string> lines = WrapText(ctx, entry.text, width);
            const double height = lines.size() * lineHeight;

            if (y + height > area.y + area.height) {
                slot.hiddenCauses = entries.size() - i;
                break;
            }

            CauseSlot cause;
            cause.ref = entry.ref;
            cause.textRect = Rect2Dd(area.x + markerGap + indent, y, width, height);
            cause.marker = Point2Dd(area.x + indent + markerGap * 0.4, y + lineHeight * 0.5);
            cause.rightAligned = false;
            cause.lines = std::move(lines);

            if (centered) {
                // Keep the block centered in the column while leaving the
                // marker column where it is
                cause.textRect.x = area.x + markerGap + indent;
            }
            cause.hitRect = Rect2Dd(area.x - 3, y - 3, area.width + 6, height + 6);

            slot.causes.push_back(std::move(cause));
            y += height + gap;
        }
    }

// =============================================================================
// LAYOUT - RIB DESIGNS (Classic, SpineChips, Bracket, CrossedRibs, Compact)
// =============================================================================

    void UltraCanvasFishboneDiagram::SolveRibLayout(IRenderContext* ctx) {
        const Rect2Dd area = layout.diagramArea;
        const size_t count = document.categories.size();

        const bool compact = (design == FishboneDesign::Compact);
        const bool crossed = (design == FishboneDesign::CrossedRibs);
        const bool onSpineLabels =
                (labelPlacement == FishboneLabelPlacement::OnSpine) ||
                (labelPlacement == FishboneLabelPlacement::Auto &&
                 design == FishboneDesign::SpineChips);
        // Bracket and CrossedRibs set their cause text toward the head; the
        // other rib designs hang it outboard, ending at the twig marker.
        const bool labelsRightAligned = !(crossed || design == FishboneDesign::Bracket);

        const double headW = HeadReserve(ctx);
        const double tailW = TailReserve();

        const double spineY = compact
                ? area.y + area.height - 54.0
                : area.y + area.height * 0.5;

        layout.spineStart = Point2Dd(area.x + tailW, spineY);
        layout.spineEnd = Point2Dd(std::max(area.x + tailW + 40.0,
                                            area.x + area.width - headW), spineY);
        layout.headRect = Rect2Dd(layout.spineEnd.x, spineY - 26.0, headW, 52.0);

        if (count == 0) {
            layout.ribAngle = ribAngleDegrees * M_PI / 180.0;
            return;
        }

        const double chipHeight = categoryFontSize * 2.1;
        const double aboveRoom = std::max(30.0, spineY - area.y - chipHeight - 6.0);
        const double belowRoom = std::max(30.0, area.y + area.height - spineY - chipHeight - 6.0);

        // Root positions along the spine. CrossedRibs shares one root per pair.
        const size_t rootCount = crossed ? (count + 1) / 2 : count;
        const double span = layout.spineEnd.x - layout.spineStart.x;

        // Same-side pitch: with alternating sides, consecutive same-side ribs
        // are two steps apart - which is exactly why alternation is universal.
        const bool oneSided = compact ||
                              sidePolicy == FishboneSidePolicy::AllAbove ||
                              sidePolicy == FishboneSidePolicy::AllBelow;
        const double sideMultiplier = (oneSided || crossed) ? 1.0 : 2.0;

        const double leftLimit = area.x + 2.0;
        const double minLabel = 62.0;
        const double markerGap = 12.0;

        // Roots are laid out after the rib length is known (below); these are
        // the working values, refreshed once the rib has been trimmed.
        double rootStep = span / (static_cast<double>(rootCount) + 0.6);
        double rootStart = rootStep * 0.75;
        double sameSideStep = rootStep * sideMultiplier;

        auto rootXFor = [&](size_t categoryIndex) {
            const size_t rootIndex = crossed ? categoryIndex / 2 : categoryIndex;
            return layout.spineStart.x + rootStart + rootStep * static_cast<double>(rootIndex);
        };

        // Pick the rib angle: steepen it (smaller back-lean) until the label
        // budget is workable or the cap is reached.
        double theta = std::clamp(ribAngleDegrees, 25.0, 80.0) * M_PI / 180.0;
        double ribLength = 0.0;
        double labelBudget = 0.0;

        for (int attempt = 0; attempt < 12; ++attempt) {
            const double sinT = std::sin(theta);
            const double cosT = std::cos(theta);

            double heightRoom = oneSided
                    ? (compact ? aboveRoom : std::min(aboveRoom, belowRoom))
                    : std::min(aboveRoom, belowRoom);
            ribLength = heightRoom / std::max(0.2, sinT);

            // A rib may never lean back further than the span allows
            if (cosT > 0.01) {
                ribLength = std::min(ribLength, std::max(46.0, span * 0.45 / cosT));
            }

            labelBudget = sameSideStep - ribLength * cosT - markerGap - 6.0;
            if (labelBudget >= 96.0 || theta >= (79.0 * M_PI / 180.0)) break;
            theta = std::min(80.0 * M_PI / 180.0, theta + (5.0 * M_PI / 180.0));
        }

        const double ribLengthCap = ribLength;
        labelBudget = std::clamp(labelBudget, minLabel, 320.0);

        const double sinT = std::sin(theta);
        const double cosT = std::cos(theta);

        ctx->SetFontSize(categoryFontSize);
        ctx->SetFontWeight(FontWeight::Bold);

        // Pass 1: measure how much rib each category actually needs, so the rib
        // can be trimmed back to its content instead of always running the full
        // height. A rib far longer than its causes is what makes an
        // auto-generated fishbone look empty.
        double neededLength = 0.0;
        for (size_t i = 0; i < count; ++i) {
            CategorySlot probe;
            probe.root = Point2Dd(rootXFor(i), spineY);
            const int side = compact ? -1 : ResolveSide(i);
            const Point2Dd dir(-cosT, side * sinT);
            neededLength = std::max(neededLength,
                                    BuildRibCauses(ctx, probe, i, dir, ribLengthCap,
                                                   labelBudget, labelsRightAligned));
        }

        // Room for the chip past the last cause, when the chip sits at the tip
        const double endReserve = onSpineLabels ? 12.0 : chipHeight * 1.1;
        ribLength = std::clamp(neededLength * 1.12 + endReserve, 56.0, ribLengthCap);
        layout.ribAngle = theta;
        layout.ribLength = ribLength;

        // Re-lay the roots now that the rib length is known. The first root has
        // to clear its own rib plus a label; for designs whose cause text runs
        // toward the head it is the LAST root that needs the clearance instead.
        const double ribHorizontal = ribLength * cosT;
        {
            // 130 is the label width the first (or last) category is given at
            // the frame edge, where pitch cannot provide it.
            const double startPad = labelsRightAligned
                    ? std::min(span * 0.45, ribHorizontal + 130.0 + markerGap)
                    : std::min(span * 0.30, ribHorizontal + 14.0);
            const double endPad = labelsRightAligned
                    ? 18.0
                    : std::min(span * 0.34, 130.0 + markerGap + 14.0);
            const double usable = std::max(40.0, span - startPad - endPad);

            rootStart = startPad;
            rootStep = (rootCount > 1) ? usable / static_cast<double>(rootCount - 1) : 0.0;
            sameSideStep = (rootCount > 1) ? rootStep * sideMultiplier : usable;
        }
        labelBudget = std::clamp(sameSideStep - ribHorizontal - markerGap - 6.0,
                                 minLabel, 320.0);

        // Cause text that runs toward the head must stop at the head, not at
        // the frame edge, or it is drawn over the arrow.
        const double rightLimit = std::min(area.x + area.width - 4.0,
                                           layout.spineEnd.x + 6.0);

        // The per-category budget is what the final build uses, and it differs
        // from the pass-1 estimate (the frame edge bounds the outermost
        // categories). Measure once more with the real budgets so the trimmed
        // rib is long enough for the wrapping that actually happens.
        auto budgetFor = [&](size_t categoryIndex, double tipX, double rootX) {
            double budget = labelBudget;
            if (labelsRightAligned) {
                budget = std::min(budget, tipX - leftLimit - markerGap);
            } else {
                budget = std::min(budget, rightLimit - rootX - markerGap - 8.0);
            }
            (void)categoryIndex;
            return std::max(budget, 46.0);
        };

        neededLength = 0.0;
        for (size_t i = 0; i < count; ++i) {
            CategorySlot probe;
            probe.root = Point2Dd(rootXFor(i), spineY);
            const int side = compact ? -1 : ResolveSide(i);
            const Point2Dd dir(-cosT, side * sinT);
            const double tipX = probe.root.x + dir.x * ribLength;
            neededLength = std::max(neededLength,
                                    BuildRibCauses(ctx, probe, i, dir, ribLengthCap,
                                                   budgetFor(i, tipX, probe.root.x),
                                                   labelsRightAligned));
        }
        // Take the second measurement outright rather than the larger of the
        // two: pass 1 used a narrower budget, so its length reflects wrapping
        // that no longer happens, and keeping it leaves a long empty rib.
        ribLength = std::clamp(neededLength * 1.06 + endReserve, 56.0, ribLengthCap);
        layout.ribLength = ribLength;

        for (size_t i = 0; i < count; ++i) {
            CategorySlot slot;
            slot.index = static_cast<int>(i);
            slot.side = compact ? -1 : ResolveSide(i);
            slot.accent = CategoryAccent(i);
            slot.root = Point2Dd(rootXFor(i), spineY);

            const Point2Dd dir(-cosT, slot.side * sinT);
            slot.tip = Point2Dd(slot.root.x + dir.x * ribLength,
                                slot.root.y + dir.y * ribLength);

            // Keep the label block inside the frame: right-aligned text runs
            // from the twig marker back toward the tail, left-aligned text runs
            // forward toward the head.
            const double budget = budgetFor(i, slot.tip.x, slot.root.x);

            // Causes hang along the rib; the chip end is already reserved.
            const double causeLength = ribLength - endReserve * 0.5;
            BuildRibCauses(ctx, slot, i, dir, causeLength, budget, labelsRightAligned);

            // Category label
            const double chipH = chipHeight;
            const double badgeR = showCategoryIcons
                    ? (onSpineLabels ? chipH * 0.62 : chipH * 0.42) : 0.0;
            // A pill lying on the spine may only reach back to the previous
            // root; a chip at the rib tip has the whole same-side pitch.
            const double chipRoom = onSpineLabels
                    ? std::max(70.0, (i == 0 ? slot.root.x - leftLimit : rootStep) - 12.0)
                    : std::max(90.0, sameSideStep - ribHorizontal * 0.35);

            ctx->SetFontSize(categoryFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            const std::string& title = document.categories[i].title;
            const double titleWidth = ctx->GetTextLineDimensions(title).width;
            const double chipW = std::min(titleWidth + 24.0 + badgeR * 2.0, chipRoom);

            if (onSpineLabels) {
                // Pill lying on the spine, badge at its head-side end
                slot.badgeRadius = badgeR;
                slot.badgeCenter = slot.root;
                slot.labelRect = Rect2Dd(slot.root.x - chipW, spineY - chipH * 0.5,
                                         chipW, chipH);
                if (slot.labelRect.x < leftLimit) {
                    slot.labelRect.x = leftLimit;
                    slot.labelRect.width = std::max(40.0, slot.root.x - leftLimit);
                }
            } else {
                // Chip just beyond the rib tip
                const double offset = chipH * 0.6;
                Point2Dd chipCenter(slot.tip.x + dir.x * offset, slot.tip.y + dir.y * offset);
                slot.labelRect = Rect2Dd(chipCenter.x - chipW * 0.5,
                                         chipCenter.y - chipH * 0.5, chipW, chipH);
                slot.labelRect.x = std::clamp(slot.labelRect.x, leftLimit,
                                              std::max(leftLimit, rightLimit - chipW));
                slot.labelRect.y = std::clamp(slot.labelRect.y, area.y,
                                              area.y + area.height - chipH);
                slot.badgeRadius = badgeR;
                slot.badgeCenter = Point2Dd(slot.labelRect.x + badgeR + 5.0,
                                            slot.labelRect.y + chipH * 0.5);
            }

            layout.categories.push_back(std::move(slot));
        }

        ctx->SetFontWeight(FontWeight::Normal);
    }

// =============================================================================
// LAYOUT - VERTICAL
// =============================================================================

    void UltraCanvasFishboneDiagram::SolveVerticalLayout(IRenderContext* ctx) {
        const Rect2Dd area = layout.diagramArea;
        const size_t count = document.categories.size();

        layout.vertical = true;

        const double headH = (headShape == FishboneHeadShape::Hidden) ? 12.0 : 46.0;
        const double tailH = TailReserve();
        const double spineX = area.x + area.width * 0.46;

        layout.spineStart = Point2Dd(spineX, area.y + tailH);
        layout.spineEnd = Point2Dd(spineX, std::max(area.y + tailH + 40.0,
                                                    area.y + area.height - headH));
        layout.headRect = Rect2Dd(spineX - 40.0, layout.spineEnd.y, 80.0, headH);

        if (count == 0) return;

        const double span = layout.spineEnd.y - layout.spineStart.y;

        // Working values; the roots are re-laid once the rib length is known.
        double step = span / (static_cast<double>(count) + 0.6);
        double rootStart = step * 0.75;
        auto rootYFor = [&](size_t index) {
            return layout.spineStart.y + rootStart + step * static_cast<double>(index);
        };

        // Ribs lean back toward the tail (upward) as they go outward. Unlike the
        // horizontal designs the rib must stay mostly PARALLEL to the spine:
        // cause labels are horizontal text rows, and they can only stack if
        // successive twigs step down the page. So the angle's sine drives the
        // along-spine component here and its cosine the outward one - the
        // mirror of SolveRibLayout.
        const double theta = std::clamp(ribAngleDegrees, 25.0, 80.0) * M_PI / 180.0;
        const double sinT = std::sin(theta);
        const double cosT = std::cos(theta);

        const double chipHeight = categoryFontSize * 2.1;
        const double leftRoom = std::max(40.0, spineX - area.x - 8.0);
        const double rightRoom = std::max(40.0, area.x + area.width - spineX - 8.0);

        // The rib may not reach outside the frame horizontally, nor lean back
        // past the previous root.
        double ribLengthCap = (std::min(leftRoom, rightRoom) * 0.55) / std::max(0.15, cosT);
        ribLengthCap = std::min(ribLengthCap, step * 1.7 / std::max(0.2, sinT));
        ribLengthCap = std::max(50.0, ribLengthCap);

        double labelBudget = std::clamp(
                std::min(leftRoom, rightRoom) - ribLengthCap * cosT - 20.0, 56.0, 260.0);

        ctx->SetFontSize(categoryFontSize);
        ctx->SetFontWeight(FontWeight::Bold);

        // Pass 1: measure, then trim the ribs back to their content (see
        // SolveRibLayout for why).
        double neededLength = 0.0;
        for (size_t i = 0; i < count; ++i) {
            CategorySlot probe;
            probe.root = Point2Dd(spineX, rootYFor(i));
            const int side = ResolveSide(i);
            const Point2Dd dir(side * cosT, -sinT);
            neededLength = std::max(neededLength,
                                    BuildRibCauses(ctx, probe, i, dir, ribLengthCap,
                                                   labelBudget, side < 0));
        }

        // Reserve room past the last cause for the chip, which sits at the tip
        const double endReserve = chipHeight * 1.8;
        double ribLength = std::clamp(neededLength * 1.08 + endReserve, 50.0, ribLengthCap);
        layout.ribAngle = theta;
        layout.ribLength = ribLength;

        // Re-lay the roots: the first rib leans back toward the tail, so its
        // root has to sit far enough down the spine for the whole rib and its
        // chip to stay inside the frame.
        {
            const double back = ribLength * sinT + chipHeight * 1.2;
            rootStart = std::min(span * 0.5, back);
            const double usable = std::max(30.0, span - rootStart - 14.0);
            step = (count > 1) ? usable / static_cast<double>(count - 1) : 0.0;
        }

        labelBudget = std::clamp(std::min(leftRoom, rightRoom) - ribLength * cosT - 20.0,
                                 56.0, 260.0);

        for (size_t i = 0; i < count; ++i) {
            CategorySlot slot;
            slot.index = static_cast<int>(i);
            slot.side = ResolveSide(i);
            slot.accent = CategoryAccent(i);
            slot.root = Point2Dd(spineX, rootYFor(i));

            // Outward horizontally, backward (up) vertically
            const Point2Dd dir(slot.side * cosT, -sinT);
            slot.tip = Point2Dd(slot.root.x + dir.x * ribLength,
                                slot.root.y + dir.y * ribLength);

            // Labels hang outboard of the rib, so the budget is bounded by the
            // distance from the tip to the frame edge on that side.
            double budget = labelBudget;
            if (slot.side < 0) {
                budget = std::min(budget, slot.tip.x - area.x - 14.0);
            } else {
                budget = std::min(budget, area.x + area.width - slot.tip.x - 14.0);
            }
            budget = std::max(budget, 46.0);

            BuildRibCauses(ctx, slot, i, dir, ribLength - endReserve * 0.75, budget,
                           slot.side < 0);

            ctx->SetFontSize(categoryFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            const double badgeR = showCategoryIcons ? chipHeight * 0.42 : 0.0;
            const std::string& title = document.categories[i].title;
            const double titleWidth = ctx->GetTextLineDimensions(title).width;
            const double chipW = std::min(titleWidth + 24.0 + badgeR * 2.0,
                                          std::max(90.0, budget + 60.0));

            slot.labelRect = Rect2Dd(slot.tip.x - chipW * 0.5, slot.tip.y - chipHeight - 2.0,
                                     chipW, chipHeight);
            slot.labelRect.x = std::clamp(slot.labelRect.x, area.x,
                                          std::max(area.x, area.x + area.width - chipW));
            slot.labelRect.y = std::max(area.y, slot.labelRect.y);
            slot.badgeRadius = badgeR;
            slot.badgeCenter = Point2Dd(slot.labelRect.x + badgeR + 5.0,
                                        slot.labelRect.y + chipHeight * 0.5);

            layout.categories.push_back(std::move(slot));
        }

        ctx->SetFontWeight(FontWeight::Normal);
    }

// =============================================================================
// LAYOUT - CHEVRON SPINE (hairline leaders to floating pills)
// =============================================================================

    void UltraCanvasFishboneDiagram::SolveLeaderLayout(IRenderContext* ctx) {
        const Rect2Dd area = layout.diagramArea;
        const size_t count = document.categories.size();

        const double headW = HeadReserve(ctx);
        const double tailW = TailReserve();
        const double spineY = area.y + area.height * 0.5;

        layout.spineStart = Point2Dd(area.x + tailW, spineY);
        layout.spineEnd = Point2Dd(std::max(area.x + tailW + 40.0,
                                            area.x + area.width - headW), spineY);
        layout.headRect = Rect2Dd(layout.spineEnd.x, spineY - 26.0, headW, 52.0);

        if (count == 0) return;

        const double blockWidth = (layout.spineEnd.x - layout.spineStart.x) /
                                  static_cast<double>(count);
        const double blockHeight = std::min(48.0, area.height * 0.16);
        const double chipHeight = categoryFontSize * 2.1;
        const double leaderDrop = std::max(28.0, area.height * 0.10);

        ctx->SetFontSize(categoryFontSize);
        ctx->SetFontWeight(FontWeight::Bold);

        for (size_t i = 0; i < count; ++i) {
            CategorySlot slot;
            slot.index = static_cast<int>(i);
            slot.side = ResolveSide(i);
            slot.accent = CategoryAccent(i);
            slot.root = Point2Dd(layout.spineStart.x + blockWidth * (static_cast<double>(i) + 0.5),
                                 spineY);

            // The pill floats off the spine; the leader is a thin diagonal.
            const double chipCenterY = spineY + slot.side * (blockHeight * 0.5 + leaderDrop);
            const std::string& title = document.categories[i].title;
            Size2Di titleSize = ctx->GetTextLineDimensions(title);
            const double chipW = std::min<double>(titleSize.width + 26.0, blockWidth * 1.05);
            const double chipCenterX = slot.root.x - blockWidth * 0.10;

            slot.labelRect = Rect2Dd(chipCenterX - chipW * 0.5, chipCenterY - chipHeight * 0.5,
                                     chipW, chipHeight);
            slot.labelRect.x = std::clamp(slot.labelRect.x, area.x,
                                          std::max(area.x, area.x + area.width - chipW));
            slot.tip = Point2Dd(slot.labelRect.x + slot.labelRect.width * 0.5,
                                chipCenterY - slot.side * chipHeight * 0.5);
            slot.badgeRadius = 0.0;

            // Cause list stacked outboard of the pill
            const double listTop = (slot.side < 0)
                    ? area.y + 2.0
                    : slot.labelRect.y + chipHeight + 6.0;
            const double listBottom = (slot.side < 0)
                    ? slot.labelRect.y - 6.0
                    : area.y + area.height - 2.0;
            Rect2Dd listArea(slot.labelRect.x, listTop,
                             std::max(50.0, blockWidth * 0.98),
                             std::max(16.0, listBottom - listTop));

            BuildListCauses(ctx, slot, i, listArea, false);

            // Above-the-spine lists read better bottom-aligned against the pill
            if (slot.side < 0 && !slot.causes.empty()) {
                const double used = slot.causes.back().textRect.y +
                                    slot.causes.back().textRect.height - listArea.y;
                const double shift = std::max(0.0, listArea.height - used);
                for (auto& cause : slot.causes) {
                    cause.textRect.y += shift;
                    cause.hitRect.y += shift;
                    cause.marker.y += shift;
                }
            }

            layout.categories.push_back(std::move(slot));
        }

        ctx->SetFontWeight(FontWeight::Normal);
    }

// =============================================================================
// LAYOUT - COLUMNS (tinted panels straddling the spine)
// =============================================================================

    void UltraCanvasFishboneDiagram::SolveColumnLayout(IRenderContext* ctx) {
        const Rect2Dd area = layout.diagramArea;
        const size_t count = document.categories.size();

        const double headW = HeadReserve(ctx);
        const double tailW = TailReserve();
        const double spineY = area.y + area.height * 0.5;

        layout.spineStart = Point2Dd(area.x + tailW, spineY);
        layout.spineEnd = Point2Dd(std::max(area.x + tailW + 40.0,
                                            area.x + area.width - headW), spineY);
        layout.headRect = Rect2Dd(layout.spineEnd.x, spineY - 30.0, headW, 60.0);

        if (count == 0) return;

        const double span = layout.spineEnd.x - layout.spineStart.x;
        const double columnWidth = span / static_cast<double>(count);
        const double gutter = std::min(14.0, columnWidth * 0.14);
        const double straddle = 18.0;   // How far the panel crosses the spine
        const double panelHeight = std::max(60.0, area.height * 0.5 - 16.0) + straddle;
        const double badgeRadius = std::min(22.0, columnWidth * 0.18);

        ctx->SetFontSize(categoryFontSize);
        ctx->SetFontWeight(FontWeight::Bold);

        for (size_t i = 0; i < count; ++i) {
            CategorySlot slot;
            slot.index = static_cast<int>(i);
            slot.side = ResolveSide(i);
            slot.accent = CategoryAccent(i);
            slot.root = Point2Dd(layout.spineStart.x + columnWidth * (static_cast<double>(i) + 0.5),
                                 spineY);

            const double panelX = layout.spineStart.x + columnWidth * static_cast<double>(i) +
                                  gutter * 0.5;
            const double panelW = std::max(40.0, columnWidth - gutter);
            const double panelY = (slot.side < 0) ? spineY + straddle - panelHeight
                                                  : spineY - straddle;
            slot.panelRect = Rect2Dd(panelX, panelY, panelW, panelHeight);
            slot.tip = Point2Dd(panelX + panelW * 0.5,
                                (slot.side < 0) ? panelY : panelY + panelHeight);

            // Icon badge at the far end of the panel
            slot.badgeRadius = showCategoryIcons ? badgeRadius : 0.0;
            slot.badgeCenter = Point2Dd(panelX + panelW * 0.5,
                                        (slot.side < 0) ? panelY + badgeRadius + 10.0
                                                        : panelY + panelHeight - badgeRadius - 10.0);

            // Category name sits beside the spine, just inside the panel
            const double labelHeight = categoryFontSize * 1.6;
            slot.labelRect = Rect2Dd(panelX + 6.0,
                                     (slot.side < 0) ? spineY + 6.0
                                                     : spineY - 6.0 - labelHeight,
                                     panelW - 12.0, labelHeight);

            const double badgeBand = (slot.badgeRadius > 0.0) ? badgeRadius * 2.0 + 16.0 : 8.0;
            const double listTop = (slot.side < 0)
                    ? panelY + badgeBand
                    : panelY + straddle + 10.0;
            const double listBottom = (slot.side < 0)
                    ? panelY + panelHeight - straddle - 10.0
                    : panelY + panelHeight - badgeBand;

            Rect2Dd listArea(panelX + 10.0, listTop, panelW - 20.0,
                             std::max(16.0, listBottom - listTop));
            BuildListCauses(ctx, slot, i, listArea, true);

            layout.categories.push_back(std::move(slot));
        }

        ctx->SetFontWeight(FontWeight::Normal);
    }

// =============================================================================
// HIT TESTING
// =============================================================================

    FishboneRef UltraCanvasFishboneDiagram::FindItemAt(const Point2Di& pos) const {
        if (!layout.valid) return FishboneRef();
        const Point2Dd point(pos.x, pos.y);

        // Causes first - they are drawn on top of the ribs
        for (const auto& slot : layout.categories) {
            for (const auto& cause : slot.causes) {
                if (cause.hitRect.Contains(point)) return cause.ref;
            }
        }
        for (const auto& slot : layout.categories) {
            if (slot.labelRect.Contains(point) ||
                (slot.panelRect.width > 0 && slot.panelRect.Contains(point))) {
                FishboneRef ref;
                ref.category = slot.index;
                return ref;
            }
        }
        return FishboneRef();
    }

// =============================================================================
// DRAWING - SHARED DECORATIONS
// =============================================================================

    void UltraCanvasFishboneDiagram::DrawSpine(IRenderContext* ctx, bool solidArrow) {
        const Color spine = SpineColor();

        if (solidArrow) {
            // Thick tapered bar drawn behind everything, as in the reference
            // "chips on the spine" design
            const double half = std::max(6.0, spineThickness * 4.0);
            const double x0 = layout.spineStart.x;
            const double x1 = layout.spineEnd.x;
            const Color body = darkTheme ? Color(46, 51, 66, 255)
                                         : MixColors(spine, Color(255, 255, 255, 255), 0.72f);
            ctx->SetFillPaint(body);
            if (layout.vertical) {
                ctx->FillRectangle(Rect2Dd(layout.spineStart.x - half, layout.spineStart.y,
                                           half * 2.0, layout.spineEnd.y - layout.spineStart.y));
            } else {
                ctx->FillRectangle(Rect2Dd(x0, layout.spineStart.y - half, x1 - x0, half * 2.0));
            }
            return;
        }

        ctx->SetStrokePaint(spine);
        ctx->SetStrokeWidth(static_cast<float>(spineThickness));
        ctx->DrawLine(layout.spineStart, layout.spineEnd);
    }

    void UltraCanvasFishboneDiagram::DrawHead(IRenderContext* ctx) {
        if (headShape == FishboneHeadShape::Hidden) return;

        const Color spine = SpineColor();
        const Point2Dd tip = layout.spineEnd;

        if (layout.vertical) {
            const double size = 22.0;
            std::vector<Point2Dd> triangle = {
                    Point2Dd(tip.x, tip.y + size),
                    Point2Dd(tip.x - size * 0.72, tip.y),
                    Point2Dd(tip.x + size * 0.72, tip.y)};
            ctx->SetFillPaint(spine);
            ctx->FillLinePath(triangle);
            return;
        }

        switch (headShape) {
            case FishboneHeadShape::Arrow: {
                const double size = 14.0;
                ctx->SetStrokePaint(spine);
                ctx->SetStrokeWidth(static_cast<float>(std::max(2.0, spineThickness)));
                ctx->DrawLine(Point2Dd(tip.x - size, tip.y - size * 0.6), tip);
                ctx->DrawLine(Point2Dd(tip.x - size, tip.y + size * 0.6), tip);
                break;
            }
            case FishboneHeadShape::Triangle: {
                const double h = 26.0;
                const double w = 40.0;
                std::vector<Point2Dd> triangle = {
                        Point2Dd(tip.x + w, tip.y),
                        Point2Dd(tip.x, tip.y - h),
                        Point2Dd(tip.x, tip.y + h)};
                ctx->SetFillPaint(spine);
                ctx->FillLinePath(triangle);
                break;
            }
            case FishboneHeadShape::FishHead: {
                const double h = 26.0;
                const double w = 44.0;
                std::vector<Point2Dd> head = {
                        Point2Dd(tip.x, tip.y - h),
                        Point2Dd(tip.x + w * 0.75, tip.y - h * 0.55),
                        Point2Dd(tip.x + w, tip.y),
                        Point2Dd(tip.x + w * 0.75, tip.y + h * 0.55),
                        Point2Dd(tip.x, tip.y + h)};
                ctx->SetFillPaint(spine);
                ctx->FillLinePath(head);
                ctx->SetFillPaint(SurfaceColor());
                ctx->FillCircle(Point2Dd(tip.x + w * 0.55, tip.y - h * 0.22), 4.0);
                break;
            }
            case FishboneHeadShape::Box: {
                Rect2Dd box(tip.x + 6.0, tip.y - layout.headRect.height * 0.5,
                            std::max(60.0, layout.headRect.width - 12.0),
                            layout.headRect.height);
                ctx->SetFillPaint(darkTheme ? Color(58, 64, 82, 255) : Color(52, 58, 64, 255));
                ctx->FillRoundedRectangle(box, 8.0);
                break;
            }
            case FishboneHeadShape::Hidden:
                break;
        }

        // The effect text goes inside the head when asked for
        if (effectPlacement == FishboneEffectPlacement::HeadBox && !document.effect.empty()) {
            const bool onDark = (headShape == FishboneHeadShape::Box ||
                                 headShape == FishboneHeadShape::Triangle ||
                                 headShape == FishboneHeadShape::FishHead);
            Rect2Dd box(tip.x + 8.0, tip.y - layout.headRect.height * 0.5,
                        std::max(48.0, layout.headRect.width - 16.0),
                        layout.headRect.height);

            ctx->SetFontSize(effectFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(onDark ? Color(255, 255, 255, 255) : DiagramTextColor());

            std::vector<std::string> lines = WrapText(ctx, document.effect, box.width - 10.0);
            const double lineHeight = effectFontSize * 1.28;
            double y = box.y + (box.height - lines.size() * lineHeight) * 0.5;
            for (const auto& line : lines) {
                Size2Di size = ctx->GetTextLineDimensions(line);
                ctx->DrawText(line, Point2Dd(box.x + (box.width - size.width) * 0.5, y));
                y += lineHeight;
            }
            ctx->SetFontWeight(FontWeight::Normal);
        }
    }

    void UltraCanvasFishboneDiagram::DrawTail(IRenderContext* ctx) {
        if (tailShape == FishboneTailShape::Hidden) return;

        const Color spine = SpineColor();
        const Point2Dd tail = layout.spineStart;
        ctx->SetFillPaint(spine);

        if (layout.vertical) {
            // A filled triangle here would read as a second arrowhead, so the
            // vertical tail is always an open chevron notch.
            ctx->SetStrokePaint(spine);
            ctx->SetStrokeWidth(static_cast<float>(std::max(2.0, spineThickness)));
            ctx->DrawLine(Point2Dd(tail.x - 11.0, tail.y + 13.0), tail);
            ctx->DrawLine(Point2Dd(tail.x + 11.0, tail.y + 13.0), tail);
            return;
        }

        switch (tailShape) {
            case FishboneTailShape::Chevron: {
                std::vector<Point2Dd> chevron = {
                        Point2Dd(tail.x, tail.y - 16.0),
                        Point2Dd(tail.x + 16.0, tail.y),
                        Point2Dd(tail.x, tail.y + 16.0)};
                ctx->FillLinePath(chevron);
                break;
            }
            case FishboneTailShape::Triangle: {
                std::vector<Point2Dd> triangle = {
                        Point2Dd(tail.x - 20.0, tail.y - 18.0),
                        Point2Dd(tail.x, tail.y),
                        Point2Dd(tail.x - 20.0, tail.y + 18.0)};
                ctx->FillLinePath(triangle);
                break;
            }
            case FishboneTailShape::FishTail: {
                std::vector<Point2Dd> upper = {
                        Point2Dd(tail.x, tail.y),
                        Point2Dd(tail.x - 30.0, tail.y - 22.0),
                        Point2Dd(tail.x - 12.0, tail.y - 2.0)};
                std::vector<Point2Dd> lower = {
                        Point2Dd(tail.x, tail.y),
                        Point2Dd(tail.x - 30.0, tail.y + 22.0),
                        Point2Dd(tail.x - 12.0, tail.y + 2.0)};
                ctx->FillLinePath(upper);
                ctx->FillLinePath(lower);
                break;
            }
            case FishboneTailShape::Hidden:
                break;
        }
    }

    void UltraCanvasFishboneDiagram::DrawSpineCaption(IRenderContext* ctx) {
        if (!showSpineCaption || spineCaption.empty() || layout.vertical) return;

        ctx->SetFontSize(categoryFontSize * 0.92f);
        ctx->SetFontWeight(FontWeight::Bold);

        // The caption goes near the tail, offset to the side the first rib does
        // NOT use - that side is free until its own first root, which is a
        // whole pitch further along, so the caption rarely has to be shortened.
        int captionSide = -1;
        double firstRootX = layout.spineEnd.x;
        if (!layout.categories.empty()) {
            captionSide = -layout.categories.front().side;
            firstRootX = layout.spineEnd.x;
            for (const auto& slot : layout.categories) {
                if (slot.side == captionSide) {
                    firstRootX = slot.root.x;
                    break;
                }
            }
        }
        const double room = std::max(40.0, firstRootX - layout.spineStart.x - 20.0);

        const std::string caption = ElideText(ctx, spineCaption, room);
        Size2Di size = ctx->GetTextLineDimensions(caption);

        const double x = layout.spineStart.x + 12.0;
        const double y = layout.spineStart.y + captionSide * (size.height * 0.6 + 3.0)
                         - size.height * 0.5;

        ctx->SetFillPaint(backgroundColor);
        ctx->FillRectangle(Rect2Dd(x - 5.0, y - 2.0, size.width + 10.0, size.height + 4.0));
        ctx->SetTextPaint(DiagramTextColor());
        ctx->DrawText(caption, Point2Dd(x, y));
        ctx->SetFontWeight(FontWeight::Normal);
    }

    void UltraCanvasFishboneDiagram::DrawWaypoints(IRenderContext* ctx) {
        if (!showWaypoints || layout.categories.empty()) return;

        ctx->SetStrokePaint(WithAlpha(SpineColor(), 190));
        ctx->SetStrokeWidth(2.0f);

        for (const auto& slot : layout.categories) {
            const double size = 6.0;
            if (layout.vertical) {
                ctx->DrawLine(Point2Dd(slot.root.x - size, slot.root.y - size),
                              Point2Dd(slot.root.x, slot.root.y));
                ctx->DrawLine(Point2Dd(slot.root.x + size, slot.root.y - size),
                              Point2Dd(slot.root.x, slot.root.y));
            } else {
                ctx->DrawLine(Point2Dd(slot.root.x - size, slot.root.y - size),
                              Point2Dd(slot.root.x, slot.root.y));
                ctx->DrawLine(Point2Dd(slot.root.x - size, slot.root.y + size),
                              Point2Dd(slot.root.x, slot.root.y));
            }
        }
    }

    void UltraCanvasFishboneDiagram::DrawCategoryChip(
            IRenderContext* ctx, const CategorySlot& slot, bool filled) {
        if (slot.index < 0 || slot.index >= static_cast<int>(document.categories.size())) return;
        const FishboneCategory& category = document.categories[slot.index];
        const Rect2Dd& rect = slot.labelRect;
        if (rect.width < 6.0 || rect.height < 6.0) return;

        const double radius = rect.height * 0.5;
        FishboneRef ref;
        ref.category = slot.index;
        const bool selected = (ref == selectedItem);
        const bool hovered = (ref == hoveredItem);

        if (filled) {
            ctx->SetFillPaint(slot.accent);
            ctx->FillRoundedRectangle(rect, radius);
        } else {
            ctx->SetFillPaint(SurfaceColor());
            ctx->FillRoundedRectangle(rect, radius);
            ctx->SetStrokePaint(slot.accent);
            ctx->SetStrokeWidth(1.6f);
            ctx->DrawRoundedRectangle(rect, radius);
        }

        if (selected || hovered) {
            ctx->SetStrokePaint(selected ? DarkenColor(slot.accent, 0.35f)
                                         : WithAlpha(slot.accent, 150));
            ctx->SetStrokeWidth(selected ? 2.5f : 1.5f);
            ctx->DrawRoundedRectangle(rect, radius);
        }

        // Text runs between the chip edges, keeping clear of the badge on
        // whichever end it sits.
        double textLeft = rect.x + 10.0;
        double textRight = rect.x + rect.width - 10.0;
        if (showCategoryIcons && slot.badgeRadius > 0.0) {
            const bool badgeOnLeft = (slot.badgeCenter.x < rect.x + rect.width * 0.5);
            if (badgeOnLeft) {
                textLeft = std::max(textLeft, slot.badgeCenter.x + slot.badgeRadius + 5.0);
            } else {
                textRight = std::min(textRight, slot.badgeCenter.x - slot.badgeRadius - 5.0);
            }
        }

        ctx->SetFontSize(categoryFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(filled ? Color(255, 255, 255, 255)
                                 : (darkTheme ? Color(236, 239, 244, 255)
                                              : DarkenColor(slot.accent, 0.25f)));

        const double available = std::max(12.0, textRight - textLeft);
        const std::string title = ElideText(ctx, category.title, available);
        Size2Di size = ctx->GetTextLineDimensions(title);
        const double textX = textLeft + std::max(0.0, (available - size.width) * 0.5);
        ctx->DrawText(title, Point2Dd(textX, rect.y + (rect.height - size.height) * 0.5));
        ctx->SetFontWeight(FontWeight::Normal);

        if (slot.hiddenCauses > 0) {
            ctx->SetFontSize(layout.causeFont * 0.9f);
            ctx->SetTextPaint(MutedTextColor());
            const std::string more = "+" + std::to_string(slot.hiddenCauses);
            Size2Di moreSize = ctx->GetTextLineDimensions(more);
            ctx->DrawText(more, Point2Dd(rect.x + rect.width - moreSize.width - 4.0,
                                         rect.y + rect.height + 2.0));
        }
    }

    void UltraCanvasFishboneDiagram::DrawCategoryBadge(
            IRenderContext* ctx, const CategorySlot& slot) {
        if (!showCategoryIcons || slot.badgeRadius <= 0.0) return;
        if (slot.index < 0 || slot.index >= static_cast<int>(document.categories.size())) return;
        const FishboneCategory& category = document.categories[slot.index];

        ctx->SetFillPaint(SurfaceColor());
        ctx->FillCircle(slot.badgeCenter, slot.badgeRadius);
        ctx->SetStrokePaint(slot.accent);
        ctx->SetStrokeWidth(2.4f);
        ctx->DrawCircle(slot.badgeCenter, slot.badgeRadius);

        if (!category.iconPath.empty()) {
            const double inner = slot.badgeRadius * 1.25;
            ctx->DrawImage(category.iconPath,
                           Rect2Dd(slot.badgeCenter.x - inner * 0.5,
                                   slot.badgeCenter.y - inner * 0.5, inner, inner),
                           ImageFitMode::Contain);
            return;
        }

        const std::string glyph = category.iconGlyph.empty()
                ? category.title.substr(0, 1) : category.iconGlyph;
        ctx->SetFontSize(static_cast<float>(std::max(7.0, slot.badgeRadius * 0.95)));
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(DarkenColor(slot.accent, 0.1f));
        Size2Di size = ctx->GetTextLineDimensions(glyph);
        ctx->DrawText(glyph, Point2Dd(slot.badgeCenter.x - size.width * 0.5,
                                      slot.badgeCenter.y - size.height * 0.5));
        ctx->SetFontWeight(FontWeight::Normal);
    }

    void UltraCanvasFishboneDiagram::DrawSelectionHighlight(
            IRenderContext* ctx, const Rect2Dd& rect, const Color& accent, bool selected) {
        ctx->SetFillPaint(WithAlpha(accent, selected ? 62 : 30));
        ctx->FillRoundedRectangle(rect, 4.0);
        if (selected) {
            ctx->SetStrokePaint(accent);
            ctx->SetStrokeWidth(1.2f);
            ctx->DrawRoundedRectangle(rect, 4.0);
        }
    }

    void UltraCanvasFishboneDiagram::DrawCauses(
            IRenderContext* ctx, const CategorySlot& slot, bool drawLeader,
            bool markerAtSpineSide) {
        if (slot.causes.empty()) return;

        const double lineHeight = layout.causeFont * 1.32;
        const Color textColor = DiagramTextColor();

        for (const auto& cause : slot.causes) {
            const FishboneCause* model = FindCause(cause.ref);
            const bool selected = (cause.ref == selectedItem);
            const bool hovered = (cause.ref == hoveredItem);

            if (selected || hovered) {
                DrawSelectionHighlight(ctx, cause.hitRect, slot.accent, selected);
            }

            const Color markerColor = (model && model->markerColor.a > 0)
                    ? model->markerColor : slot.accent;

            // Leader from the twig marker to the text block
            if (drawLeader) {
                const double edge = cause.rightAligned
                        ? cause.textRect.x + cause.textRect.width
                        : cause.textRect.x;
                ctx->SetStrokePaint(WithAlpha(markerColor, 150));
                ctx->SetStrokeWidth(1.2f);
                ctx->DrawLine(cause.marker, Point2Dd(edge, cause.marker.y));
            }

            switch (causeMarker) {
                case FishboneCauseMarker::Dot: {
                    const double radius = (cause.ref.subCause >= 0) ? 3.0 : 4.6;
                    ctx->SetFillPaint(markerColor);
                    ctx->FillCircle(cause.marker, radius);
                    if (model && model->isRootCause) {
                        ctx->SetStrokePaint(DarkenColor(markerColor, 0.3f));
                        ctx->SetStrokeWidth(2.0f);
                        ctx->DrawCircle(cause.marker, radius + 3.0);
                    }
                    break;
                }
                case FishboneCauseMarker::Bullet: {
                    ctx->SetFillPaint(markerColor);
                    ctx->FillCircle(Point2Dd(cause.textRect.x - 7.0,
                                             cause.textRect.y + lineHeight * 0.5),
                                    (cause.ref.subCause >= 0) ? 1.8 : 2.6);
                    break;
                }
                case FishboneCauseMarker::Numbered:
                case FishboneCauseMarker::Hidden:
                default:
                    break;
            }

            const bool emphasised = (model && model->highlighted) || selected;
            ctx->SetFontSize(cause.ref.subCause >= 0 ? layout.causeFont * 0.92f
                                                     : layout.causeFont);
            ctx->SetFontWeight(emphasised ? FontWeight::Bold : FontWeight::Normal);
            ctx->SetTextPaint(emphasised ? DarkenColor(slot.accent, darkTheme ? 0.0f : 0.2f)
                                         : (cause.ref.subCause >= 0 ? MutedTextColor()
                                                                    : textColor));
            DrawTextLines(ctx, cause.lines, cause.textRect, cause.rightAligned, lineHeight);
            ctx->SetFontWeight(FontWeight::Normal);

            if (showWeights && model && model->weight > 0.0) {
                std::ostringstream weight;
                weight << model->weight;
                ctx->SetFontSize(layout.causeFont * 0.82f);
                Size2Di size = ctx->GetTextLineDimensions(weight.str());
                const double badgeW = size.width + 10.0;
                const double badgeH = size.height + 4.0;
                const double x = cause.rightAligned
                        ? cause.textRect.x - badgeW - 4.0
                        : cause.textRect.x + cause.textRect.width + 4.0;
                Rect2Dd badge(x, cause.textRect.y, badgeW, badgeH);
                ctx->SetFillPaint(WithAlpha(slot.accent, 60));
                ctx->FillRoundedRectangle(badge, badgeH * 0.5);
                ctx->SetTextPaint(DarkenColor(slot.accent, 0.2f));
                ctx->DrawText(weight.str(), Point2Dd(badge.x + 5.0, badge.y + 2.0));
            }
        }

        (void)markerAtSpineSide;
    }

// =============================================================================
// DRAWING - DESIGNS
// =============================================================================

    void UltraCanvasFishboneDiagram::RenderClassic(IRenderContext* ctx) {
        DrawSpine(ctx, false);
        DrawTail(ctx);
        DrawHead(ctx);
        DrawSpineCaption(ctx);
        DrawWaypoints(ctx);

        for (const auto& slot : layout.categories) {
            ctx->SetStrokePaint(slot.accent);
            ctx->SetStrokeWidth(2.4f);
            ctx->DrawLine(slot.root, slot.tip);

            DrawCauses(ctx, slot, false, false);
            DrawCategoryChip(ctx, slot, true);
            DrawCategoryBadge(ctx, slot);
        }
    }

    void UltraCanvasFishboneDiagram::RenderSpineChips(IRenderContext* ctx) {
        DrawSpine(ctx, true);
        DrawTail(ctx);
        DrawHead(ctx);
        DrawSpineCaption(ctx);
        DrawWaypoints(ctx);

        for (const auto& slot : layout.categories) {
            ctx->SetStrokePaint(WithAlpha(slot.accent, 210));
            ctx->SetStrokeWidth(2.2f);
            ctx->DrawLine(slot.root, slot.tip);

            DrawCauses(ctx, slot, false, false);
            DrawCategoryChip(ctx, slot, true);
            DrawCategoryBadge(ctx, slot);
        }
    }

    void UltraCanvasFishboneDiagram::RenderCrossedRibs(IRenderContext* ctx) {
        DrawSpine(ctx, false);
        DrawTail(ctx);
        DrawHead(ctx);
        DrawSpineCaption(ctx);

        // One line per pair: the rib of category 2p and the rib of 2p+1 are the
        // same stroke crossing the spine.
        for (size_t i = 0; i < layout.categories.size(); ++i) {
            const auto& slot = layout.categories[i];
            const Color line = darkTheme ? Color(120, 128, 145, 255) : Color(150, 157, 170, 255);
            ctx->SetStrokePaint(line);
            ctx->SetStrokeWidth(1.6f);

            // Extend the stroke a little past the spine so pairs read as one line
            const Point2Dd through(slot.root.x + (slot.root.x - slot.tip.x) * 0.16,
                                   slot.root.y + (slot.root.y - slot.tip.y) * 0.16);
            ctx->DrawLine(slot.tip, through);

            DrawCauses(ctx, slot, false, true);
            DrawCategoryChip(ctx, slot, true);
        }

        // Icon badges sit on the spine at each crossing
        for (const auto& slot : layout.categories) {
            if (!showCategoryIcons) break;
            if (slot.side > 0) continue;   // One badge per pair
            CategorySlot badge = slot;
            badge.badgeCenter = slot.root;
            badge.badgeRadius = 15.0;
            DrawCategoryBadge(ctx, badge);
        }
    }

    void UltraCanvasFishboneDiagram::RenderBracket(IRenderContext* ctx) {
        DrawSpine(ctx, false);
        DrawTail(ctx);
        DrawHead(ctx);
        DrawSpineCaption(ctx);

        const Color bone = darkTheme ? Color(118, 126, 143, 255) : Color(73, 80, 87, 255);

        for (const auto& slot : layout.categories) {
            // Slanted edge plus a shelf running back toward the head
            const double shelf = std::abs(slot.root.x - slot.tip.x) * 0.55 + 20.0;
            std::vector<Point2Dd> bracket = {
                    slot.root, slot.tip, Point2Dd(slot.tip.x + shelf, slot.tip.y)};
            ctx->SetStrokePaint(bone);
            ctx->SetStrokeWidth(1.5f);
            ctx->DrawLinePath(bracket, false);

            DrawCauses(ctx, slot, true, false);
            DrawCategoryChip(ctx, slot, true);
            DrawCategoryBadge(ctx, slot);
        }
    }

    void UltraCanvasFishboneDiagram::RenderChevronSpine(IRenderContext* ctx) {
        const size_t count = layout.categories.size();
        if (count == 0) {
            DrawSpine(ctx, false);
            DrawHead(ctx);
            return;
        }

        const double blockWidth = (layout.spineEnd.x - layout.spineStart.x) /
                                  static_cast<double>(count);
        const double blockHeight = std::min(48.0, layout.diagramArea.height * 0.16);
        const double notch = std::min(18.0, blockWidth * 0.28);
        const double spineY = layout.spineStart.y;

        // Chevron chain: each block points at the next
        for (size_t i = 0; i < count; ++i) {
            const auto& slot = layout.categories[i];
            const double x0 = layout.spineStart.x + blockWidth * static_cast<double>(i);
            const double x1 = x0 + blockWidth;
            const double top = spineY - blockHeight * 0.5;
            const double bottom = spineY + blockHeight * 0.5;

            std::vector<Point2Dd> block = {
                    Point2Dd(x0, top),
                    Point2Dd(x1 - notch, top),
                    Point2Dd(x1, spineY),
                    Point2Dd(x1 - notch, bottom),
                    Point2Dd(x0, bottom),
                    Point2Dd(x0 + notch, spineY)};

            ctx->SetFillPaint(slot.accent);
            ctx->FillLinePath(block);

            if (showCategoryIcons) {
                const FishboneCategory& category = document.categories[slot.index];
                const std::string glyph = category.iconGlyph.empty()
                        ? category.title.substr(0, 1) : category.iconGlyph;
                if (!category.iconPath.empty()) {
                    const double size = blockHeight * 0.55;
                    ctx->DrawImage(category.iconPath,
                                   Rect2Dd(slot.root.x - size * 0.5, spineY - size * 0.5,
                                           size, size),
                                   ImageFitMode::Contain);
                } else {
                    ctx->SetFontSize(static_cast<float>(blockHeight * 0.36));
                    ctx->SetFontWeight(FontWeight::Bold);
                    ctx->SetTextPaint(Color(255, 255, 255, 235));
                    Size2Di size = ctx->GetTextLineDimensions(glyph);
                    ctx->DrawText(glyph, Point2Dd(slot.root.x - size.width * 0.5,
                                                  spineY - size.height * 0.5));
                    ctx->SetFontWeight(FontWeight::Normal);
                }
            }
        }

        DrawHead(ctx);
        DrawTail(ctx);

        for (const auto& slot : layout.categories) {
            // Hairline leader from the spine block to the floating pill
            ctx->SetStrokePaint(WithAlpha(darkTheme ? Color(190, 198, 214, 255)
                                                    : Color(120, 128, 145, 255), 190));
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(Point2Dd(slot.root.x, slot.root.y +
                                                slot.side * blockHeight * 0.5), slot.tip);

            DrawCauses(ctx, slot, false, false);
            DrawCategoryChip(ctx, slot, false);
        }
    }

    void UltraCanvasFishboneDiagram::RenderColumns(IRenderContext* ctx) {
        // Panels first, then the spine on top so it reads as passing through
        for (const auto& slot : layout.categories) {
            ctx->SetFillPaint(darkTheme ? MixColors(slot.accent, Color(29, 32, 41, 255), 0.76f)
                                        : MixColors(slot.accent, Color(255, 255, 255, 255), 0.84f));
            ctx->FillRoundedRectangle(slot.panelRect, 8.0);

            FishboneRef ref;
            ref.category = slot.index;
            if (ref == selectedItem || ref == hoveredItem) {
                ctx->SetStrokePaint(slot.accent);
                ctx->SetStrokeWidth((ref == selectedItem) ? 2.4f : 1.4f);
                ctx->DrawRoundedRectangle(slot.panelRect, 8.0);
            }
        }

        DrawSpine(ctx, false);
        DrawTail(ctx);
        DrawHead(ctx);
        DrawWaypoints(ctx);

        for (const auto& slot : layout.categories) {
            // Filled badge at the far end of the panel
            if (showCategoryIcons && slot.badgeRadius > 0.0) {
                ctx->SetFillPaint(slot.accent);
                ctx->FillCircle(slot.badgeCenter, slot.badgeRadius);

                const FishboneCategory& category = document.categories[slot.index];
                if (!category.iconPath.empty()) {
                    const double inner = slot.badgeRadius * 1.2;
                    ctx->DrawImage(category.iconPath,
                                   Rect2Dd(slot.badgeCenter.x - inner * 0.5,
                                           slot.badgeCenter.y - inner * 0.5, inner, inner),
                                   ImageFitMode::Contain);
                } else {
                    const std::string glyph = category.iconGlyph.empty()
                            ? category.title.substr(0, 1) : category.iconGlyph;
                    ctx->SetFontSize(static_cast<float>(std::max(8.0, slot.badgeRadius * 0.85)));
                    ctx->SetFontWeight(FontWeight::Bold);
                    ctx->SetTextPaint(Color(255, 255, 255, 240));
                    Size2Di size = ctx->GetTextLineDimensions(glyph);
                    ctx->DrawText(glyph, Point2Dd(slot.badgeCenter.x - size.width * 0.5,
                                                  slot.badgeCenter.y - size.height * 0.5));
                    ctx->SetFontWeight(FontWeight::Normal);
                }
            }

            // Category name beside the spine
            const FishboneCategory& category = document.categories[slot.index];
            ctx->SetFontSize(categoryFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(DarkenColor(slot.accent, darkTheme ? 0.0f : 0.18f));
            Size2Di size = ctx->GetTextLineDimensions(category.title);
            ctx->DrawText(category.title,
                          Point2Dd(slot.labelRect.x + (slot.labelRect.width - size.width) * 0.5,
                                   slot.labelRect.y + (slot.labelRect.height - size.height) * 0.5));
            ctx->SetFontWeight(FontWeight::Normal);

            DrawCauses(ctx, slot, false, false);
        }
    }

    void UltraCanvasFishboneDiagram::RenderVertical(IRenderContext* ctx) {
        DrawSpine(ctx, false);
        DrawTail(ctx);
        DrawHead(ctx);
        DrawWaypoints(ctx);

        for (const auto& slot : layout.categories) {
            ctx->SetStrokePaint(slot.accent);
            ctx->SetStrokeWidth(2.2f);
            ctx->DrawLine(slot.root, slot.tip);

            DrawCauses(ctx, slot, false, false);
            DrawCategoryChip(ctx, slot, true);
            DrawCategoryBadge(ctx, slot);
        }
    }

// =============================================================================
// RENDERING - TOP LEVEL
// =============================================================================

    void UltraCanvasFishboneDiagram::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!ctx) return;

        UpdateLayout(ctx);

        ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);

        // The effect doubles as the element title in the infographic designs -
        // which is where every surveyed reference puts it.
        const std::string heading =
                (effectPlacement == FishboneEffectPlacement::Title && !document.effect.empty())
                        ? document.effect : chartTitle;
        if (!heading.empty() &&
            (effectPlacement == FishboneEffectPlacement::Title || !chartTitle.empty())) {
            ctx->SetTextPaint(DiagramTextColor());
            ctx->SetFontSize(titleFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            Size2Di size = ctx->GetTextLineDimensions(heading);
            ctx->DrawText(heading, Point2Dd(GetWidth() * 0.5 - size.width * 0.5,
                                            std::max(6.0, (48.0 - size.height) * 0.5)));
            ctx->SetFontWeight(FontWeight::Normal);
        }

        RenderChart(ctx);
    }

    void UltraCanvasFishboneDiagram::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        if (!layout.valid) UpdateLayout(ctx);

        switch (design) {
            case FishboneDesign::Classic:      RenderClassic(ctx); break;
            case FishboneDesign::SpineChips:   RenderSpineChips(ctx); break;
            case FishboneDesign::CrossedRibs:  RenderCrossedRibs(ctx); break;
            case FishboneDesign::Bracket:      RenderBracket(ctx); break;
            case FishboneDesign::ChevronSpine: RenderChevronSpine(ctx); break;
            case FishboneDesign::Columns:      RenderColumns(ctx); break;
            case FishboneDesign::Vertical:     RenderVertical(ctx); break;
            case FishboneDesign::Compact:      RenderClassic(ctx); break;
        }
    }

// =============================================================================
// EVENTS
// =============================================================================

    bool UltraCanvasFishboneDiagram::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!layout.valid) return false;

        FishboneRef found = FindItemAt(mousePos);
        if (found != hoveredItem) {
            hoveredItem = found;
            if (hoveredItem.IsCause() || hoveredItem.IsSubCause()) {
                const FishboneCause* cause = FindCause(hoveredItem);
                if (cause && onCauseHover) onCauseHover(hoveredItem, *cause);
            }
            RequestRedraw();
        }

        // GetWindow() is null when the element is driven offscreen (tests, an
        // export pass); the tooltip manager needs a real window.
        if (enableTooltips && GetWindow()) {
            if (hoveredItem.IsValid()) {
                Point2Di tipPos(mousePos.x + 15, mousePos.y - 20);
                auto windowPos = MapFromLocal(tipPos, nullptr);
                UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        GetWindow(), BuildTooltip(hoveredItem), windowPos);
            } else {
                UltraCanvasTooltipManager::HideTooltip();
            }
        }

        return hoveredItem.IsValid();
    }

    bool UltraCanvasFishboneDiagram::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown: {
                if (event.button != UCMouseButton::Left || !enableSelection || !layout.valid) {
                    return UltraCanvasChartElementBase::OnEvent(event);
                }
                FishboneRef clicked = FindItemAt(Point2Di(event.pointer.x, event.pointer.y));
                if (clicked.IsValid()) {
                    selectedItem = (clicked == selectedItem) ? FishboneRef() : clicked;
                    if (selectedItem.IsValid()) {
                        if (selectedItem.IsCategory() && onCategorySelect) {
                            onCategorySelect(static_cast<size_t>(selectedItem.category),
                                             document.categories[selectedItem.category]);
                        } else if (onCauseSelect) {
                            const FishboneCause* cause = FindCause(selectedItem);
                            if (cause) onCauseSelect(selectedItem, *cause);
                        }
                    }
                    if (onSelectionChange) onSelectionChange();
                    RequestRedraw();
                    return true;
                }
                if (selectedItem.IsValid()) {
                    ClearSelection();
                    if (onSelectionChange) onSelectionChange();
                    return true;
                }
                return UltraCanvasChartElementBase::OnEvent(event);
            }

            case UCEventType::MouseDoubleClick: {
                if (!layout.valid) return false;
                FishboneRef clicked = FindItemAt(Point2Di(event.pointer.x, event.pointer.y));
                if (clicked.IsCategory()) {
                    SetCategoryCollapsed(static_cast<size_t>(clicked.category),
                                         !GetCategoryCollapsed(static_cast<size_t>(clicked.category)));
                    return true;
                }
                if (clicked.IsValid() && onCauseDoubleClick) {
                    const FishboneCause* cause = FindCause(clicked);
                    if (cause) {
                        onCauseDoubleClick(clicked, *cause);
                        return true;
                    }
                }
                return false;
            }

            case UCEventType::MouseLeave:
                if (hoveredItem.IsValid()) {
                    hoveredItem = FishboneRef();
                    RequestRedraw();
                }
                UltraCanvasTooltipManager::HideTooltip();
                return false;

            default:
                return UltraCanvasChartElementBase::OnEvent(event);
        }
    }

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasFishboneDiagram> CreateFishboneDiagramElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasFishboneDiagram>(id, x, y, width, height);
    }

    std::shared_ptr<UltraCanvasFishboneDiagram> CreateFishboneDiagramElement(
            const std::string& id, int x, int y, int width, int height,
            FishboneDesign design) {
        auto diagram = std::make_shared<UltraCanvasFishboneDiagram>(id, x, y, width, height);
        diagram->SetDesign(design);
        return diagram;
    }

} // namespace UltraCanvas
