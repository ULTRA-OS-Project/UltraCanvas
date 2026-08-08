// Plugins/Diagrams/UltraCanvasFishboneModel.cpp
// Fishbone (Ishikawa) model: category presets, queries, validation, samples
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasFishboneModel.h"
#include <algorithm>

namespace UltraCanvas {

// =============================================================================
// CATEGORY PRESETS
// =============================================================================

    const char* FishboneCategoryPresetName(FishboneCategoryPreset preset) {
        switch (preset) {
            case FishboneCategoryPreset::SixM:     return "6M (manufacturing)";
            case FishboneCategoryPreset::FiveME:   return "5M+E (manufacturing)";
            case FishboneCategoryPreset::EightP:   return "8P (marketing / service)";
            case FishboneCategoryPreset::FourS:    return "4S (service)";
            case FishboneCategoryPreset::FiveS:    return "5S (service + safety)";
            case FishboneCategoryPreset::PEMPEM:   return "PEMPEM (healthcare / general)";
            case FishboneCategoryPreset::Software: return "Software delivery";
        }
        return "Custom";
    }

    std::vector<FishboneCategory> FishboneCategoriesFromPreset(FishboneCategoryPreset preset) {
        // {title, glyph} pairs; colors are left transparent so the element's
        // palette assigns them in order.
        std::vector<std::pair<const char*, const char*>> entries;

        switch (preset) {
            case FishboneCategoryPreset::SixM:
                entries = {{"Man", "M"}, {"Machine", "Mc"}, {"Material", "Mt"},
                           {"Method", "Me"}, {"Measurement", "Ms"}, {"Mother Nature", "Mn"}};
                break;
            case FishboneCategoryPreset::FiveME:
                entries = {{"Man", "M"}, {"Machine", "Mc"}, {"Material", "Mt"},
                           {"Method", "Me"}, {"Environment", "En"}};
                break;
            case FishboneCategoryPreset::EightP:
                entries = {{"Product", "Pr"}, {"Price", "Pc"}, {"Place", "Pl"},
                           {"Promotion", "Pm"}, {"People", "Pe"}, {"Process", "Ps"},
                           {"Physical Evidence", "Ph"}, {"Productivity", "Pd"}};
                break;
            case FishboneCategoryPreset::FourS:
                entries = {{"Surroundings", "Su"}, {"Suppliers", "Sp"},
                           {"Systems", "Sy"}, {"Skills", "Sk"}};
                break;
            case FishboneCategoryPreset::FiveS:
                entries = {{"Surroundings", "Su"}, {"Suppliers", "Sp"},
                           {"Systems", "Sy"}, {"Skills", "Sk"}, {"Safety", "Sf"}};
                break;
            case FishboneCategoryPreset::PEMPEM:
                entries = {{"People", "Pe"}, {"Equipment", "Eq"}, {"Materials", "Ma"},
                           {"Process", "Pr"}, {"Environment", "En"}, {"Management", "Mg"}};
                break;
            case FishboneCategoryPreset::Software:
                entries = {{"Requirements", "Rq"}, {"Design", "Ds"}, {"Code", "Cd"},
                           {"Test", "Ts"}, {"Process", "Pr"}, {"Tooling", "Tl"},
                           {"People", "Pe"}, {"Environment", "En"}};
                break;
        }

        std::vector<FishboneCategory> categories;
        categories.reserve(entries.size());
        for (const auto& entry : entries) {
            FishboneCategory category(entry.first);
            category.iconGlyph = entry.second;
            categories.push_back(std::move(category));
        }
        return categories;
    }

// =============================================================================
// QUERIES
// =============================================================================

    static size_t CountCauseTree(const std::vector<FishboneCause>& causes,
                                 bool includeSubCauses) {
        size_t total = causes.size();
        if (includeSubCauses) {
            for (const auto& cause : causes) {
                total += CountCauseTree(cause.subCauses, true);
            }
        }
        return total;
    }

    size_t CountFishboneCauses(const std::vector<FishboneCategory>& categories,
                               bool includeSubCauses) {
        size_t total = 0;
        for (const auto& category : categories) {
            total += CountCauseTree(category.causes, includeSubCauses);
        }
        return total;
    }

    static int CauseTreeDepth(const std::vector<FishboneCause>& causes) {
        int deepest = 0;
        for (const auto& cause : causes) {
            deepest = std::max(deepest, 1 + CauseTreeDepth(cause.subCauses));
        }
        return deepest;
    }

    int FishboneDepth(const FishboneDocument& document) {
        if (document.categories.empty()) return 0;
        int deepest = 1;
        for (const auto& category : document.categories) {
            deepest = std::max(deepest, 1 + CauseTreeDepth(category.causes));
        }
        return deepest;
    }

    static bool IsBlank(const std::string& text) {
        return text.find_first_not_of(" \t\r\n") == std::string::npos;
    }

    static void ValidateCauses(const std::vector<FishboneCause>& causes,
                               const std::string& path, int depth,
                               std::vector<std::string>& problems) {
        for (size_t i = 0; i < causes.size(); ++i) {
            const std::string here = path + " > cause " + std::to_string(i + 1);
            if (IsBlank(causes[i].text)) {
                problems.push_back(here + " has no text");
            }
            if (depth + 1 > kFishboneMaxRenderDepth && !causes[i].subCauses.empty()) {
                problems.push_back(here + " nests deeper than the renderer draws (depth " +
                                   std::to_string(kFishboneMaxRenderDepth) + ")");
            }
            ValidateCauses(causes[i].subCauses, here, depth + 1, problems);
        }
    }

    std::vector<std::string> ValidateFishbone(const FishboneDocument& document) {
        std::vector<std::string> problems;

        if (IsBlank(document.effect)) {
            problems.push_back("The diagram has no effect (the problem being analysed)");
        }
        if (document.categories.empty()) {
            problems.push_back("The diagram has no cause categories");
        }

        for (size_t c = 0; c < document.categories.size(); ++c) {
            const auto& category = document.categories[c];
            const std::string path = "Category " + std::to_string(c + 1) +
                                     (category.title.empty() ? "" : " (" + category.title + ")");
            if (IsBlank(category.title)) {
                problems.push_back(path + " has no title");
            }
            if (category.causes.empty()) {
                problems.push_back(path + " has no causes");
            }
            ValidateCauses(category.causes, path, 2, problems);
        }
        return problems;
    }

// =============================================================================
// SAMPLE DATA
// =============================================================================

    namespace FishboneSamples {

        FishboneDocument QualityControl() {
            FishboneDocument doc;
            doc.effect = "Quality Control Issues";

            auto& man = doc.AddCategory("Man (Human Factors)");
            man.iconGlyph = "M";
            man.AddCause("Inadequate training");
            man.AddCause("Operator error");
            man.AddCause("Non-compliance with standards");

            auto& machines = doc.AddCategory("Machines (Equipment)");
            machines.iconGlyph = "Mc";
            machines.AddCause("Malfunctions or breakdowns");
            machines.AddCause("Calibration issues");
            machines.AddCause("Outdated technology");

            auto& materials = doc.AddCategory("Materials (Raw)");
            materials.iconGlyph = "Mt";
            materials.AddCause("Defective supplies");
            materials.AddCause("Inconsistent quality");
            materials.AddCause("Poor handling/storage");

            auto& method = doc.AddCategory("Method");
            method.iconGlyph = "Me";
            method.AddCause("Unclear work instructions");
            method.AddCause("No in-process inspection");
            method.AddCause("Rework not tracked");

            return doc;
        }

        FishboneDocument CustomerChurn() {
            FishboneDocument doc;
            doc.effect = "Customer Churn";

            auto& systems = doc.AddCategory("Systems");
            systems.iconGlyph = "Sy";
            {
                FishboneCause onboarding("Poor onboarding processes");
                onboarding.subCauses.emplace_back("No guided first run");
                onboarding.subCauses.emplace_back("Setup needs support");
                systems.causes.push_back(std::move(onboarding));
            }
            systems.AddCause("Ineffective subscription management");
            systems.AddCause("Lack of personalization");

            auto& surroundings = doc.AddCategory("Surroundings");
            surroundings.iconGlyph = "Su";
            surroundings.AddCause("Pricing changes");
            surroundings.AddCause("Market competition");
            surroundings.AddCause("Economic factors");

            auto& skills = doc.AddCategory("Skills");
            skills.iconGlyph = "Sk";
            skills.AddCause("Inadequate customer service");
            {
                FishboneCause retention("Weak retention strategies");
                retention.isRootCause = true;
                retention.subCauses.emplace_back("No win-back campaign");
                skills.causes.push_back(std::move(retention));
            }
            skills.AddCause("Passive customer engagement");

            auto& suppliers = doc.AddCategory("Suppliers");
            suppliers.iconGlyph = "Sp";
            suppliers.AddCause("Perceived lack of value");
            suppliers.AddCause("Unmet expectations");
            suppliers.AddCause("Limited differentiation");

            return doc;
        }

        FishboneDocument SoftwareDelivery() {
            FishboneDocument doc;
            doc.effect = "Release Slipped Two Sprints";

            auto& requirements = doc.AddCategory("Requirements");
            requirements.iconGlyph = "Rq";
            requirements.AddCause("Scope added mid-sprint");
            requirements.AddCause("Acceptance criteria unclear");
            requirements.AddCause("Late stakeholder feedback");

            auto& code = doc.AddCategory("Code");
            code.iconGlyph = "Cd";
            {
                FishboneCause debt("Accumulated technical debt");
                debt.highlighted = true;
                debt.weight = 8.0;
                code.causes.push_back(std::move(debt));
            }
            code.AddCause("Merge conflicts on long branches");
            code.AddCause("Reviews queued for days");

            auto& test = doc.AddCategory("Test");
            test.iconGlyph = "Ts";
            test.AddCause("Flaky integration suite");
            test.AddCause("Manual regression pass");
            test.AddCause("No environment parity");

            auto& tooling = doc.AddCategory("Tooling");
            tooling.iconGlyph = "Tl";
            tooling.AddCause("Slow CI pipeline");
            tooling.AddCause("Fragile release scripts");
            tooling.AddCause("Missing observability");

            auto& people = doc.AddCategory("People");
            people.iconGlyph = "Pe";
            people.AddCause("Two engineers on leave");
            people.AddCause("Context switching across projects");
            people.AddCause("Onboarding a new team member");

            return doc;
        }

    } // namespace FishboneSamples

} // namespace UltraCanvas
