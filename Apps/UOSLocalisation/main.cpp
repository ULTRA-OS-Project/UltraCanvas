// Apps/UOSLocalisation/main.cpp
// UOS-Localisation, the ULTRA OS translation editor: every set an
// application's texts come from - its own set, the base sets it takes, the
// standard set - in one grid, for any application and any language, in a
// window of its own (UltraCanvasLocalisationEditor, Window arrangement).
// It was the Translations page of UOS-Settings until 0.28. One copy per user:
// a second start brings the first forward and quits, on the application and
// language it names (UltraCanvasSingleInstance.h).
//
//   UOS-Localisation                       the last language set, first application
//   UOS-Localisation --app UltraFiler --lang de
//
// Version: 0.1.0
// Last Modified: 2026-11-22
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasModalDialog.h"     // UltraCanvasDialogManager
#include "UltraCanvasLocalisationEditor.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#ifndef UOSLOCALISATION_VERSION
#error "UOSLOCALISATION_VERSION must come from Docs/UOSLocalisation/CHANGELOG.md via CMake"
#endif

using namespace UltraCanvas;

namespace {

void PrintUsage(const char* program) {
    std::printf("UOS-Localisation - the ULTRA OS translation editor.\n"
                "Usage: %s [options]\n"
                "  --app <name>    open that application's texts (UltraFiler, UOS-Settings, ...)\n"
                "  --lang <tag>    in that language (de, pt-BR, zh-Hans, ...)\n"
                "  -h, --help      show this message\n"
                "  -v, --version   show the version\n", program);
}

// The language the editor opens in when none is asked for: the one chosen
// in UOS-Settings (Language.conf), else the one this program runs in.
std::string StartLanguage(const std::string& settingsFolder) {
    const LanguageSettings settings = LanguageSettings::Load(settingsFolder);
    std::string language = settings.language == "system" || settings.language.empty()
            ? UltraLocalisation::CurrentLanguage() : settings.language;
    return language.empty() ? "en" : language;
}

} // namespace

int main(int argc, char** argv) {
    std::string application, language;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") { PrintUsage(argv[0]); return EXIT_SUCCESS; }
        if (a == "-v" || a == "--version") { std::printf("UOS-Localisation %s\n", UOSLOCALISATION_VERSION); return EXIT_SUCCESS; }
        if (a == "--app" && i + 1 < argc) { application = argv[++i]; continue; }
        if (a == "--lang" && i + 1 < argc) { language = argv[++i]; continue; }
        std::fprintf(stderr, "Unknown option: %s\n", a.c_str());
        PrintUsage(argv[0]);
        return EXIT_FAILURE;
    }

    // --version above runs alongside an open window; only the window claims.
    UltraCanvasSingleInstance instance("org.ultraos.uoslocalisation", "UOS-Localisation");
    JSONValue request = JSONValue::MakeObject();
    if (!application.empty()) request.Set("app", JSONValue(application));
    if (!language.empty()) request.Set("lang", JSONValue(language));
    const UltraCanvasSingleInstance::Start start = instance.Claim(request);
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;   // the running copy came forward

    UltraCanvasApplication app;
    if (!app.Initialize("UOS-Localisation")) {
        std::fprintf(stderr, "UOS-Localisation: the UltraCanvas application could not "
                             "be initialised (no display?).\n");
        return EXIT_FAILURE;
    }
    if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
        return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UOS-Localisation");
    app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UOS-Localisation.png"));

    WindowConfig config;
    config.title = std::string("UOS-Localisation ") + UOSLOCALISATION_VERSION;
    config.width = 1280;
    config.height = 800;
    config.minWidth = 900;
    config.minHeight = 520;
    config.resizable = true;
    auto window = CreateWindow(config);
    if (!window || !window->IsCreated()) {
        std::fprintf(stderr, "UOS-Localisation: the window could not be created.\n");
        return EXIT_FAILURE;
    }
    window->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto editor = std::make_shared<UltraCanvasLocalisationEditor>("uos-localisation", LocalisationEditorLayout::Window);
    editor->layoutItem.SetFlexGrow(1).SetFlexShrink(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    const std::string settingsFolder = PathToUtf8(UltraCanvasSettingsFolder());
    const std::string shipped = UltraLocalisation::ShippedFolder().empty()
            ? NormalizePath(GetResourcesDir() + "media/locale") : UltraLocalisation::ShippedFolder();
    editor->SetFolders(shipped, settingsFolder);
    editor->SetTranslatorName(LanguageSettings::Load(settingsFolder).translator);
    editor->Open(application, language.empty() ? StartLanguage(settingsFolder) : language);
    window->AddChild(editor);

    // Unsaved edits are asked about before the window goes.
    UltraCanvasWindow* win = window.get();
    bool discard = false;
    window->onWindowClosing = [&discard, win, editor]() -> bool {
        if (discard || !editor->GetWorkspace().Dirty()) return true;
        UltraCanvasDialogManager::ShowConfirmation(
                Tr("The translations have unsaved changes. Close and lose them?"), "UOS-Localisation",
                [&discard, win](bool yes) { if (yes) { discard = true; win->Close(); } }, win);
        return false;
    };
    window->onWindowClosed = [&app]() { app.Exit(); };
    window->Show();

    // A later start brings this window forward, on the application and
    // language it names - unless that would drop unsaved edits.
    instance.Listen([editor](const JSONValue& request) {
        const std::string app = request.Get("app").GetString();
        const std::string lang = request.Get("lang").GetString();
        if ((!app.empty() || !lang.empty()) && !editor->GetWorkspace().Dirty()) {
            editor->Open(app.empty() ? editor->GetWorkspace().Application() : app,
                         lang.empty() ? editor->GetWorkspace().Language() : lang);
        }
        UltraCanvasSingleInstance::BringToFront();
    });
    app.Run();
    instance.Close();         // no request reaches the window once it starts to go
    return EXIT_SUCCESS;
}
