// Tests/FishboneModelTest.cpp
// Unit tests for the fishbone (Ishikawa) model and its text interchange.
//
// Covers the parts that are pure data and therefore testable without a window:
// category presets, cause/sub-cause counting, depth, validation, the indented
// outline codec (tabs, mixed indents, bullets, comments) and the Mermaid
// `ishikawa-beta` codec in both directions - including the two indentation
// shapes found in the wild, where the effect and the categories either share an
// indent level or do not.
//
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasFishboneModel.h"

#include <cstdio>
#include <string>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

// =============================================================================

static void TestPresets() {
    std::printf("\n[category presets]\n");

    auto sixM = FishboneCategoriesFromPreset(FishboneCategoryPreset::SixM);
    CHECK(sixM.size() == 6, "6M has six categories");
    CHECK(sixM[0].title == "Man", "6M starts with Man");
    CHECK(sixM[5].title == "Mother Nature", "6M ends with Mother Nature");
    CHECK(sixM[0].causes.empty(), "a preset fills in titles only, not causes");
    CHECK(sixM[0].accentColor.a == 0,
          "preset colors stay transparent so the palette assigns them");

    auto fourS = FishboneCategoriesFromPreset(FishboneCategoryPreset::FourS);
    CHECK(fourS.size() == 4, "4S has four categories");
    CHECK(fourS[0].title == "Surroundings" && fourS[3].title == "Skills",
          "4S is Surroundings, Suppliers, Systems, Skills");

    CHECK(FishboneCategoriesFromPreset(FishboneCategoryPreset::EightP).size() == 8,
          "8P has eight categories");
    CHECK(FishboneCategoriesFromPreset(FishboneCategoryPreset::FiveME).size() == 5,
          "5M+E has five categories");
    CHECK(FishboneCategoriesFromPreset(FishboneCategoryPreset::FiveS).size() == 5,
          "5S has five categories");
    CHECK(FishboneCategoriesFromPreset(FishboneCategoryPreset::PEMPEM).size() == 6,
          "PEMPEM has six categories");
    CHECK(FishboneCategoriesFromPreset(FishboneCategoryPreset::Software).size() == 8,
          "the software preset has eight categories");

    CHECK(std::string(FishboneCategoryPresetName(FishboneCategoryPreset::SixM))
                  .find("6M") != std::string::npos,
          "preset names are human readable");
}

static void TestCountingAndDepth() {
    std::printf("\n[counting and depth]\n");

    FishboneDocument doc;
    doc.effect = "Effect";
    CHECK(FishboneDepth(doc) == 0, "an empty document has depth 0");

    doc.AddCategory("First");
    CHECK(FishboneDepth(doc) == 1, "categories alone give depth 1");

    // Note: AddCategory returns a reference that the next AddCategory may
    // invalidate (the std::vector::emplace_back rule), so index afterwards.
    doc.categories[0].AddCause("Cause A");
    doc.categories[0].AddCause("Cause B");
    doc.AddCategory("Second");
    doc.categories[1].AddCause("Cause C");

    CHECK(CountFishboneCauses(doc.categories, false) == 3, "three top-level causes");
    CHECK(CountFishboneCauses(doc.categories, true) == 3,
          "sub-cause counting matches when there are none");
    CHECK(FishboneDepth(doc) == 2, "causes give depth 2");

    doc.categories[0].causes[0].subCauses.emplace_back("Why 1");
    doc.categories[0].causes[0].subCauses.emplace_back("Why 2");
    CHECK(CountFishboneCauses(doc.categories, false) == 3,
          "sub-causes do not inflate the shallow count");
    CHECK(CountFishboneCauses(doc.categories, true) == 5,
          "sub-causes are counted when asked for");
    CHECK(FishboneDepth(doc) == 3, "one sub-cause tier gives depth 3");

    doc.categories[0].causes[0].subCauses[0].subCauses.emplace_back("Why 3");
    CHECK(FishboneDepth(doc) == 4, "depth keeps counting past what is rendered");
}

static void TestValidation() {
    std::printf("\n[validation]\n");

    FishboneDocument empty;
    auto problems = ValidateFishbone(empty);
    CHECK(problems.size() >= 2, "an empty document reports the missing effect and categories");

    FishboneDocument doc = FishboneSamples::QualityControl();
    CHECK(ValidateFishbone(doc).empty(), "the quality-control sample is clean");

    doc.AddCategory("Lonely");
    problems = ValidateFishbone(doc);
    CHECK(problems.size() == 1, "a category with no causes is the only complaint");

    doc.categories.back().AddCause("");
    problems = ValidateFishbone(doc);
    CHECK(problems.size() == 1, "a blank cause replaces the empty-category complaint");
}

static void TestSamples() {
    std::printf("\n[samples]\n");

    FishboneDocument quality = FishboneSamples::QualityControl();
    CHECK(quality.effect == "Quality Control Issues", "quality sample names its effect");
    CHECK(quality.categories.size() == 4, "quality sample has four categories");
    CHECK(CountFishboneCauses(quality.categories) == 12, "quality sample has 12 causes");

    FishboneDocument churn = FishboneSamples::CustomerChurn();
    CHECK(churn.categories.size() == 4, "churn sample uses the 4S set");
    CHECK(FishboneDepth(churn) == 3, "churn sample exercises the sub-cause tier");

    bool foundRootCause = false;
    for (const auto& category : churn.categories) {
        for (const auto& cause : category.causes) {
            if (cause.isRootCause) foundRootCause = true;
        }
    }
    CHECK(foundRootCause, "churn sample marks a verified root cause");

    FishboneDocument software = FishboneSamples::SoftwareDelivery();
    CHECK(software.categories.size() == 5, "software sample has five categories");
    CHECK(ValidateFishbone(software).empty(), "software sample is clean");
}

static void TestOutlineParsing() {
    std::printf("\n[outline parsing]\n");

    const std::string text =
            "Machine Downtime\n"
            "  Man\n"
            "    Untrained operators\n"
            "    Shift handover gaps\n"
            "  Machine\n"
            "    Worn bearings\n"
            "      No vibration monitoring\n"
            "  Method\n"
            "    No preventive schedule\n";

    std::string error;
    FishboneDocument doc = ParseFishboneOutline(text, &error);

    CHECK(error.empty(), "a well-formed outline parses without error");
    CHECK(doc.effect == "Machine Downtime", "the first line is the effect");
    CHECK(doc.categories.size() == 3, "three categories");
    CHECK(doc.categories[0].title == "Man", "first category is Man");
    CHECK(doc.categories[0].causes.size() == 2, "Man has two causes");
    CHECK(doc.categories[1].causes.size() == 1, "Machine has one cause");
    CHECK(doc.categories[1].causes[0].subCauses.size() == 1,
          "the deeper line becomes a sub-cause, not a fourth category");
    CHECK(doc.categories[1].causes[0].subCauses[0].text == "No vibration monitoring",
          "the sub-cause keeps its text");
}

static void TestOutlineTolerance() {
    std::printf("\n[outline tolerance]\n");

    // Tabs, four-space indents, bullets, comments and blank lines all at once.
    const std::string messy =
            "// a comment line\n"
            "\n"
            "Late Deliveries\n"
            "\tSuppliers\n"
            "\t\t- Single source for parts\n"
            "\t\t* Long lead times\n"
            "\tSystems\n"
            "\t\t+ Manual order entry\n";

    FishboneDocument doc = ParseFishboneOutline(messy);
    CHECK(doc.effect == "Late Deliveries", "comments and blank lines are skipped");
    CHECK(doc.categories.size() == 2, "tab indents give two categories");
    CHECK(doc.categories[0].causes.size() == 2, "bullets are stripped, causes kept");
    CHECK(doc.categories[0].causes[0].text == "Single source for parts",
          "the '-' bullet is not part of the text");
    CHECK(doc.categories[1].causes[0].text == "Manual order entry",
          "the '+' bullet is not part of the text");

    // A two-space file and a four-space file must read identically.
    FishboneDocument two = ParseFishboneOutline("E\n  C\n    x\n");
    FishboneDocument four = ParseFishboneOutline("E\n    C\n        x\n");
    CHECK(two.categories.size() == 1 && four.categories.size() == 1,
          "indent width does not change the shape");
    CHECK(two.categories[0].causes.size() == 1 && four.categories[0].causes.size() == 1,
          "causes survive either indent width");

    std::string error;
    FishboneDocument onlyEffect = ParseFishboneOutline("Just an effect\n", &error);
    CHECK(onlyEffect.categories.empty() && !error.empty(),
          "an effect with no categories is reported");

    error.clear();
    FishboneDocument nothing = ParseFishboneOutline("   \n\n", &error);
    CHECK(nothing.IsEmpty() && !error.empty(), "empty input is reported");
}

static void TestOutlineRoundTrip() {
    std::printf("\n[outline round trip]\n");

    FishboneDocument original = FishboneSamples::CustomerChurn();
    FishboneDocument reparsed = ParseFishboneOutline(ExportFishboneOutline(original));

    CHECK(reparsed.effect == original.effect, "the effect survives a round trip");
    CHECK(reparsed.categories.size() == original.categories.size(),
          "the category count survives a round trip");
    CHECK(CountFishboneCauses(reparsed.categories, true) ==
                  CountFishboneCauses(original.categories, true),
          "every cause and sub-cause survives a round trip");
    CHECK(FishboneDepth(reparsed) == FishboneDepth(original),
          "the depth survives a round trip");
    CHECK(reparsed.categories[0].causes[0].text == original.categories[0].causes[0].text,
          "cause text survives a round trip");
}

static void TestMermaidParsing() {
    std::printf("\n[mermaid ishikawa-beta]\n");

    // Mermaid's own documented shape: the effect and the categories share one
    // indent step under the keyword.
    const std::string mermaid =
            "ishikawa-beta\n"
            "    Blurry Photo\n"
            "    Process\n"
            "        Out of focus\n"
            "        Shutter speed too slow\n"
            "    Equipment\n"
            "        LENS\n"
            "            Damaged lens\n"
            "            Dirty lens\n"
            "    Environment\n"
            "        Too dark\n";

    std::string error;
    FishboneDocument doc = ParseMermaidIshikawa(mermaid, &error);

    CHECK(error.empty(), "the documented Mermaid example parses cleanly");
    CHECK(doc.effect == "Blurry Photo", "the line after the keyword is the effect");
    CHECK(doc.categories.size() == 3, "three categories, not four - the effect is not one");
    CHECK(doc.categories[0].title == "Process", "first category is Process");
    CHECK(doc.categories[0].causes.size() == 2, "Process has two causes");
    CHECK(doc.categories[1].causes.size() == 1, "Equipment has one cause (LENS)");
    CHECK(doc.categories[1].causes[0].subCauses.size() == 2,
          "LENS carries two sub-causes");
    CHECK(doc.categories[2].causes.size() == 1, "Environment has one cause");

    // Front matter and comments must not derail the parse.
    const std::string withFrontMatter =
            "---\n"
            "title: Photo quality\n"
            "---\n"
            "ishikawa-beta\n"
            "    Blurry Photo\n"
            "    Process\n"
            "        Out of focus\n";
    FishboneDocument fm = ParseMermaidIshikawa(withFrontMatter);
    CHECK(fm.effect == "Blurry Photo" && fm.categories.size() == 1,
          "front matter is skipped");

    // No keyword at all falls back to reading the text as a plain outline.
    FishboneDocument plain = ParseMermaidIshikawa("Effect\n  Category\n    Cause\n");
    CHECK(plain.effect == "Effect" && plain.categories.size() == 1,
          "text without the keyword is read as an outline");
}

static void TestMermaidRoundTrip() {
    std::printf("\n[mermaid round trip]\n");

    FishboneDocument original = FishboneSamples::CustomerChurn();
    const std::string emitted = ExportMermaidIshikawa(original);

    CHECK(emitted.rfind("ishikawa-beta", 0) == 0, "the export leads with the keyword");
    CHECK(emitted.find("Customer Churn") != std::string::npos,
          "the effect appears in the export");

    FishboneDocument reparsed = ParseMermaidIshikawa(emitted);
    CHECK(reparsed.effect == original.effect, "the effect survives the Mermaid round trip");
    CHECK(reparsed.categories.size() == original.categories.size(),
          "the category count survives the Mermaid round trip");
    CHECK(CountFishboneCauses(reparsed.categories, true) ==
                  CountFishboneCauses(original.categories, true),
          "every cause and sub-cause survives the Mermaid round trip");
    CHECK(reparsed.categories[2].causes[1].subCauses.size() ==
                  original.categories[2].causes[1].subCauses.size(),
          "a nested sub-cause keeps its parent");

    // The emitted text must also be readable by the outline parser, since the
    // two notations differ only in the header.
    FishboneDocument viaOutline = ParseMermaidIshikawa(ExportFishboneOutline(original));
    CHECK(viaOutline.categories.size() == original.categories.size(),
          "the Mermaid reader accepts a plain outline too");
}

// =============================================================================

int main() {
    std::printf("================================\n");
    std::printf("  Fishbone model tests\n");
    std::printf("================================\n");

    TestPresets();
    TestCountingAndDepth();
    TestValidation();
    TestSamples();
    TestOutlineParsing();
    TestOutlineTolerance();
    TestOutlineRoundTrip();
    TestMermaidParsing();
    TestMermaidRoundTrip();

    std::printf("\n================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
