// Apps/DemoApp/UltraCanvasLocalisationEditorExamples.cpp
// The localisation editor (UltraCanvasLocalisationEditor) on a copy of the
// shipped catalogues: the real standard set and base sets in every language
// they ship in, and a sample application "UltraCanvasDemo" that takes the
// Video and Audio base sets, with a few words of its own and one override.
// The copy lives under the temp folder, so a save on this page changes
// nothing in the resources tree; its settings folder is a sibling, so the
// settings-folder path can be tried too (Save goes to the copy's source
// tree, which is writable; the status line says where).
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework
#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasLocalisationEditor.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace UltraCanvas {

namespace {

// The shipped locale tree copied under the temp folder, with the sample
// application's sets added; the path of the copy, empty when it failed.
std::filesystem::path PrepareDemoLocale() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / "ultracanvas-demo-locale";
    if (ec) return {};
    const fs::path shipped = root / "shipped";
    const fs::path source = PathFromUtf8(NormalizePath(GetResourcesDir() + "media/locale"));
    fs::remove_all(shipped, ec);
    fs::create_directories(shipped, ec);
    if (fs::is_directory(source, ec)) fs::copy(source, shipped, fs::copy_options::recursive, ec);
    fs::create_directories(root / "settings", ec);
    auto put = [&](const fs::path& path, const char* text) {
        fs::create_directories(path.parent_path(), ec);
        std::ofstream(path, std::ios::binary) << text;
    };
    put(shipped / "en" / "UltraCanvasDemo.json",
        "{\n  \"@language\": \"en\",\n  \"@bases\": [\n    \"Video\",\n    \"Audio\"\n  ],\n"
        "  \"@inherited\": [\n    \"Cancel\",\n    \"Clip\",\n    \"Delete\",\n    \"Fade out\",\n    \"Open\",\n    \"Save\",\n    \"Trim\",\n    \"Volume\"\n  ],\n"
        "  \"Fade out the audio\": \"Fade out the audio\",\n  \"Ripple delete\": \"Ripple delete\",\n  \"Show waveform\": \"Show waveform\"\n}\n");
    put(shipped / "de" / "UltraCanvasDemo.json",
        "{\n  \"@language\": \"de\",\n  \"@bases\": [\n    \"Video\",\n    \"Audio\"\n  ],\n"
        "  \"Delete\": \"Aus dem Album entfernen\",\n  \"Ripple delete\": \"Ripple-Löschen\"\n}\n");
    put(shipped / "fr" / "UltraCanvasDemo.json",
        "{\n  \"@language\": \"fr\",\n  \"@bases\": [\n    \"Video\",\n    \"Audio\"\n  ],\n  \"Ripple delete\": \"Suppression avec décalage\"\n}\n");
    return root;
}

}  // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateLocalisationEditorExamples() {
    auto container = DemoKit::CreatePage("LocalisationEditorPage", "Localisation Editor",
        "Every set an application's texts come from in one grid - its own set, the base sets it "
        "takes, the standard set - with where each text comes from in the row. Pick a language, "
        "type in the edit row, take a base set from Sets..., promote or demote a row from its "
        "right-click menu, Undo, Save. This page works on a copy of the shipped catalogues under "
        "the temp folder with a sample application, so nothing here changes the resources tree.");
    container->SetFillsArea(true);

    auto card = DemoKit::CreateCard("LocalisationEditorCard");
    DemoKit::Add(container, card, 1);
    card->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexShrink(1);

    auto status = DemoKit::AddText(card, "LocalisationEditorStatus", "Ready.");
    status->SetTextColor(Color(90, 90, 90, 255));

    auto editor = std::make_shared<UltraCanvasLocalisationEditor>("LocalisationEditorDemo");
    editor->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0))
          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    const std::filesystem::path root = PrepareDemoLocale();
    if (root.empty()) {
        status->SetText("The temp folder is not writable: the editor shows the shipped catalogues read-only.");
        editor->SetFolders(NormalizePath(GetResourcesDir() + "media/locale"), "");
    } else {
        editor->SetFolders(PathToUtf8(root / "shipped"), PathToUtf8(root / "settings"));
    }
    editor->SetTranslatorName("Demo");
    editor->onStatus = [status](const std::string& text) { status->SetText(text); };
    editor->Open("UltraCanvasDemo", "de");
    DemoKit::Add(card, editor, 1);
    return container;
}

} // namespace UltraCanvas
