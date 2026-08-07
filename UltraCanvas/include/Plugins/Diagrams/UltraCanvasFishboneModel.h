// include/Plugins/Diagrams/UltraCanvasFishboneModel.h
// Fishbone (Ishikawa) cause-and-effect model: effect, cause categories, causes
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework
//
// This is the semantic model behind the fishbone feature. Like
// UltraCanvasSequenceModel it is deliberately UI-free - it includes nothing
// from the widget or render stack - so that:
//   * the text interchange (outline and Mermaid `ishikawa-beta`) is a pure
//     function of the model,
//   * it can be unit-tested without a window (Tests/FishboneModelTest.cpp),
//   * the renderer element (UltraCanvasFishboneDiagram) owns only geometry.
//
// The structure is fixed by the diagram, not by the data: depth 0 is the
// EFFECT, depth 1 is a CAUSE CATEGORY (a rib), depth 2 is a CAUSE (a twig) and
// depth 3+ are SUB-CAUSES (the 5-Whys tail). That is what separates a fishbone
// from a general topic tree, and why causes nest but categories never do.
//
// See Docs/UltraCanvas/UltraCanvasFishboneDiagramProposal.md for the research
// behind the design.
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// MODEL
// =============================================================================

    // One cause on a rib. Sub-causes nest to any depth; renderers stop at
    // kFishboneMaxRenderDepth.
    struct FishboneCause {
        std::string text;
        std::vector<FishboneCause> subCauses;

        // Fully transparent = use the category accent color
        Color markerColor = Color(0, 0, 0, 0);

        std::string tooltip;        // Overrides the generated tooltip
        bool   highlighted = false; // Extra emphasis (a suspected cause)
        bool   isRootCause = false; // Verified root cause; drawn with a ring
        double weight      = 0.0;   // Optional score; 0 = unweighted

        FishboneCause() = default;
        explicit FishboneCause(const std::string& t) : text(t) {}
        FishboneCause(const std::string& t, std::vector<FishboneCause> subs)
                : text(t), subCauses(std::move(subs)) {}
    };

    // One rib: a named group of causes.
    struct FishboneCategory {
        std::string title;
        std::string iconPath;   // Image file drawn inside the category badge
        std::string iconGlyph;  // Short text drawn when no image is set

        // Fully transparent = take the next palette entry
        Color accentColor = Color(0, 0, 0, 0);

        // -1 above/left of the spine, +1 below/right, 0 = let the side policy decide
        int  side = 0;
        bool collapsed = false;  // Draw the rib and label but not the causes

        std::vector<FishboneCause> causes;

        FishboneCategory() = default;
        explicit FishboneCategory(const std::string& t) : title(t) {}
        FishboneCategory(const std::string& t, const Color& accent)
                : title(t), accentColor(accent) {}

        // The returned reference follows the std::vector::emplace_back rule: a
        // later AddCause() on the same category may invalidate it. Finish with
        // it, or index into `causes`, before adding the next one.
        FishboneCause& AddCause(const std::string& text) {
            causes.emplace_back(text);
            return causes.back();
        }
    };

    // A whole diagram: the effect plus its cause categories. The element stores
    // one of these, so import/export is a single assignment.
    struct FishboneDocument {
        std::string effect;
        std::vector<FishboneCategory> categories;

        void Clear() { effect.clear(); categories.clear(); }
        bool IsEmpty() const { return effect.empty() && categories.empty(); }

        // Same invalidation rule as FishboneCategory::AddCause: the reference
        // is only good until the next AddCategory().
        FishboneCategory& AddCategory(const std::string& title) {
            categories.emplace_back(title);
            return categories.back();
        }
    };

    // Renderers draw the effect, the category, the cause and one sub-cause tier.
    constexpr int kFishboneMaxRenderDepth = 3;

// =============================================================================
// CATEGORY PRESETS
// =============================================================================

    // The named category checklists used in practice. Each preset fills in
    // titles and icon glyphs and leaves the causes empty.
    enum class FishboneCategoryPreset {
        SixM,      // Man, Machine, Material, Method, Measurement, Mother Nature
        FiveME,    // 5M+E: as SixM without Measurement
        EightP,    // Product, Price, Place, Promotion, People, Process,
                   //   Physical Evidence, Productivity (marketing / service)
        FourS,     // Surroundings, Suppliers, Systems, Skills (service)
        FiveS,     // FourS + Safety
        PEMPEM,    // People, Equipment, Materials, Process, Environment, Management
        Software   // Requirements, Design, Code, Test, Process, Tooling, People, Environment
    };

    const char* FishboneCategoryPresetName(FishboneCategoryPreset preset);

    // Titles (and glyphs) only - the caller fills in the causes.
    std::vector<FishboneCategory> FishboneCategoriesFromPreset(FishboneCategoryPreset preset);

// =============================================================================
// QUERIES
// =============================================================================

    // Number of causes across every category. Sub-causes are counted only when
    // includeSubCauses is set.
    size_t CountFishboneCauses(const std::vector<FishboneCategory>& categories,
                               bool includeSubCauses = false);

    // 0 = no categories, 1 = categories only, 2 = categories with causes,
    // 3+ = sub-causes present (the deepest sub-cause tier reached).
    int FishboneDepth(const FishboneDocument& document);

    // Human-readable problems: no effect, no categories, an empty category
    // title, a category with no causes, blank cause text. Never fatal - the
    // renderer draws whatever it is given.
    std::vector<std::string> ValidateFishbone(const FishboneDocument& document);

// =============================================================================
// TEXT INTERCHANGE
// =============================================================================

    // Indented outline: the first non-blank line is the effect, lines indented
    // one step are categories, deeper lines are causes and sub-causes. Tabs
    // count as one step; spaces are grouped by the smallest indent seen, so
    // 2-space and 4-space files both round-trip. Leading "-", "*" and "+"
    // bullets are stripped. Blank lines and "//" / "#" comments are ignored.
    FishboneDocument ParseFishboneOutline(const std::string& text,
                                          std::string* error = nullptr);

    std::string ExportFishboneOutline(const FishboneDocument& document,
                                      int indentWidth = 2);

    // Mermaid `ishikawa-beta` (Mermaid 11.13+). The body is the same indented
    // outline, so both directions delegate to the outline codec; the parser
    // additionally accepts and skips the `ishikawa-beta` / `ishikawa` header
    // and a `---` front-matter block.
    FishboneDocument ParseMermaidIshikawa(const std::string& text,
                                          std::string* error = nullptr);

    std::string ExportMermaidIshikawa(const FishboneDocument& document,
                                      int indentWidth = 4);

// =============================================================================
// SAMPLE DATA
// =============================================================================

    namespace FishboneSamples {
        // "Quality Control Issues" - the 6M manufacturing example
        FishboneDocument QualityControl();
        // "Customer Churn" - the 4S service example, with one sub-cause tier
        FishboneDocument CustomerChurn();
        // "Website Launch Delayed" - the software example
        FishboneDocument SoftwareDelivery();
    }

} // namespace UltraCanvas
