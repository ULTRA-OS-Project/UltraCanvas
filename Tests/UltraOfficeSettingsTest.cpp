// Tests/UltraOfficeSettingsTest.cpp
// UltraOffice's settings model (Apps/UltraOffice/UltraOfficeSettings.h), with
// no window: every value's default, values kept within their range, a save
// and a fresh load giving back what was set, the recent files cut to their
// count, Restore defaults, and the keys the settings file had before the
// Settings window (saveFormat, documentNavigator) still read.
//
// Usage: UltraOfficeSettingsTest
// Exit code is the number of failed checks.

#include "UltraOfficeSettings.h"

#include "UltraCanvasAppSettings.h"
#include "UltraCanvasPathUtf8.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraOffice;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

} // namespace

int main() {
    std::error_code ec;
    const fs::path folder = fs::temp_directory_path() / "uc-ultraoffice-settings-test";
    fs::remove_all(folder, ec);
    fs::create_directories(folder, ec);
    const std::string file = PathToUtf8(folder / "settings.json");

    std::printf("=== Defaults ===\n");
    {
        UltraCanvasAppSettings store = UltraCanvasAppSettings::AtFile(file);
        store.Load();
        const OfficeSettings s(store);
        Check(s.GetAuthorName().empty() && s.EffectiveAuthorName() == OfficeSettings::LoginName(),
              "no author name: the login name signs");
        Check(s.GetLanguage() == "system", "the language every application shares");
        Check(s.GetTheme() == ThemeChoice::System, "the desktop's look");
        Check(s.GetRecentCount() == 8 && s.GetRecentFiles().empty(), "eight recent files, none yet");
        Check(!s.GetReopenFiles() && s.GetOpenFiles().empty(), "nothing reopened");
        Check(s.GetSaveFormat() == SaveFormat::OfficeOpenXml, "new files as Office Open XML");
        Check(!s.GetAutosave() && s.GetRecoveryCopies() && s.GetAutosaveMinutes() == 1,
              "no autosave, recovery copies every minute");
        Check(s.GetDocumentFont().empty() && Near(s.GetDocumentFontSize(), 12.0f), "the default font at 12 points");
        Check(s.GetPageSize() == PageSizeChoice::A4, "A4");
        Check(!s.GetShowNavigator(), "no navigator");
        Check(s.GetDocumentFit() == DocumentFit::PageWidth && Near(s.GetWidthShare(), 0.6f), "page width; a share of 60 %");
        Check(s.GetShowGridlines(), "gridlines shown");
        Check(s.GetSlideSize() == SlideSizeChoice::Widescreen && s.GetShowOnSecondScreen() && s.GetPresenterView(),
              "16:9 slides, the show on the second monitor with the presenter view");
        Check(s.GetToolLayout() == ToolLayout::Panels && !s.GetToolRail(), "the tools in panels at the left, no rail");
        Check(s.GetPanelState(OfficeKind::Document).empty() && s.GetPanelWidth(OfficeKind::Spreadsheet) == 286,
              "no panels kept yet; the column 286 px wide");
    }

    std::printf("\n=== Set, saved, loaded again ===\n");
    {
        UltraCanvasAppSettings store = UltraCanvasAppSettings::AtFile(file);
        store.Load();
        OfficeSettings s(store);
        s.SetAuthorName("Ada Lovelace");
        s.SetLanguage("de");
        s.SetTheme(ThemeChoice::Dark);
        s.SetReopenFiles(true);
        s.SetOpenFiles({"/tmp/a.docx", "/tmp/b.ods"});
        s.SetSaveFormat(SaveFormat::OpenDocument);
        s.SetAutosave(true);
        s.SetRecoveryCopies(false);
        s.SetAutosaveMinutes(5);
        s.SetDocumentFont("Georgia");
        s.SetDocumentFontSize(11.0f);
        s.SetPageSize(PageSizeChoice::Letter);
        s.SetShowNavigator(true);
        s.SetDocumentFit(DocumentFit::WidthShare);
        s.SetWidthShare(0.7f);
        s.SetShowGridlines(false);
        s.SetSlideSize(SlideSizeChoice::Standard);
        s.SetShowOnSecondScreen(false);
        s.SetPresenterView(false);
        s.SetToolLayout(ToolLayout::Toolbar);
        s.SetToolRail(true);
        s.SetPanelState(OfficeKind::Document, "order=font,clipboard;open=font;rail=0");
        s.SetPanelState(OfficeKind::Presentation, "order=slide;open=;rail=0");
        s.SetPanelWidth(OfficeKind::Spreadsheet, 330);
        for (int i = 0; i < 12; ++i) s.RememberRecentFile("/tmp/file" + std::to_string(i) + ".odt");
        Check(s.GetRecentFiles().size() == 8 && s.GetRecentFiles().front() == "/tmp/file11.odt",
              "eight recent files kept, the last opened first");
        s.SetRecentCount(4);
        Check(s.GetRecentFiles().size() == 4, "a smaller count cuts the list");
        Check(s.Save(), "saved");
    }
    {
        UltraCanvasAppSettings store = UltraCanvasAppSettings::AtFile(file);
        Check(store.Load(), "the file reads back");
        const OfficeSettings s(store);
        Check(s.GetAuthorName() == "Ada Lovelace" && s.EffectiveAuthorName() == "Ada Lovelace", "the author name");
        Check(s.GetLanguage() == "de", "the language");
        Check(s.GetTheme() == ThemeChoice::Dark, "the look");
        Check(s.GetReopenFiles() && s.GetOpenFiles() == std::vector<std::string>{"/tmp/a.docx", "/tmp/b.ods"},
              "the files to reopen");
        Check(s.GetSaveFormat() == SaveFormat::OpenDocument, "the format");
        Check(s.GetAutosave() && !s.GetRecoveryCopies() && s.GetAutosaveMinutes() == 5, "autosave, copies, interval");
        Check(s.GetDocumentFont() == "Georgia" && Near(s.GetDocumentFontSize(), 11.0f), "the font and its size");
        Check(s.GetPageSize() == PageSizeChoice::Letter, "the page");
        Check(s.GetShowNavigator(), "the navigator");
        Check(s.GetDocumentFit() == DocumentFit::WidthShare && Near(s.GetWidthShare(), 0.7f), "the fit and the share");
        Check(!s.GetShowGridlines(), "the gridlines");
        Check(s.GetSlideSize() == SlideSizeChoice::Standard && !s.GetShowOnSecondScreen() && !s.GetPresenterView(),
              "the slides and the monitors");
        Check(s.GetRecentCount() == 4 && s.GetRecentFiles().size() == 4, "the recent files' count");
        Check(s.GetToolLayout() == ToolLayout::Toolbar && s.GetToolRail(), "the tools: the toolbar, the rail");
        Check(s.GetPanelState(OfficeKind::Document) == "order=font,clipboard;open=font;rail=0" &&
                  s.GetPanelState(OfficeKind::Presentation) == "order=slide;open=;rail=0" &&
                  s.GetPanelState(OfficeKind::Spreadsheet).empty(),
              "each kind's panels, apart");
        Check(s.GetPanelWidth(OfficeKind::Spreadsheet) == 330 && s.GetPanelWidth(OfficeKind::Document) == 286,
              "each kind's column width");
        Check(store.GetString("toolLayout") == "toolbar" && !store.GetString("documentPanels").empty() &&
                  store.GetInt("spreadsheetPanelWidth", 0) == 330,
              "the keys toolLayout, documentPanels, spreadsheetPanelWidth");
        // The keys the file had before the Settings window, as the window
        // wrote them.
        Check(store.GetString("saveFormat") == "odf" && store.GetBool("documentNavigator", false),
              "saveFormat and documentNavigator are the keys they were");
    }

    std::printf("\n=== Kept within range ===\n");
    {
        UltraCanvasAppSettings store = UltraCanvasAppSettings::AtFile(file);
        store.Load();
        OfficeSettings s(store);
        s.SetWidthShare(3.0f);
        Check(Near(s.GetWidthShare(), 1.0f), "a share is at most the whole width");
        s.SetWidthShare(0.01f);
        Check(Near(s.GetWidthShare(), 0.1f), "...and at least a tenth");
        s.SetWidthShare(0.654f);
        Check(Near(s.GetWidthShare(), 0.65f), "...in whole percents");
        s.SetAutosaveMinutes(0);
        Check(s.GetAutosaveMinutes() == 1, "at least a minute");
        s.SetAutosaveMinutes(500);
        Check(s.GetAutosaveMinutes() == 60, "at most an hour");
        s.SetDocumentFontSize(400.0f);
        Check(Near(s.GetDocumentFontSize(), 96.0f), "a font size of at most 96 points");
        s.SetRecentCount(99);
        Check(s.GetRecentCount() == OfficeSettings::kMaxRecentFiles, "at most 30 recent files");
        s.SetRecentCount(0);
        s.RememberRecentFile("/tmp/kept.odt");
        Check(s.GetRecentFiles().empty(), "none kept when the count is none");
        store.Set("documentFit", std::string("sideways"));
        Check(s.GetDocumentFit() == DocumentFit::PageWidth, "an unknown fit reads as page width");
        s.SetLanguage("");
        Check(s.GetLanguage() == "system", "an empty language is the system's");
        s.SetPanelWidth(OfficeKind::Document, 20);
        Check(s.GetPanelWidth(OfficeKind::Document) == OfficeSettings::kMinPanelWidth, "a column at least 160 px wide");
        s.SetPanelWidth(OfficeKind::Document, 2000);
        Check(s.GetPanelWidth(OfficeKind::Document) == OfficeSettings::kMaxPanelWidth, "...and at most 400");
    }

    std::printf("\n=== Clear recent files, restore defaults ===\n");
    {
        UltraCanvasAppSettings store = UltraCanvasAppSettings::AtFile(file);
        store.Load();
        OfficeSettings s(store);
        s.SetRecentCount(8);
        s.RememberRecentFile("/tmp/one.odt");
        s.ClearRecentFiles();
        Check(s.GetRecentFiles().empty(), "the recent files cleared");
        s.SetOpenFiles({"/tmp/still.odt"});
        s.RestoreDefaults();
        Check(s.GetAuthorName().empty() && s.GetTheme() == ThemeChoice::System && s.GetSaveFormat() == SaveFormat::OfficeOpenXml &&
                  s.GetDocumentFit() == DocumentFit::PageWidth && s.GetShowGridlines() && !s.GetAutosave(),
              "every setting back to its default");
        Check(s.GetOpenFiles() == std::vector<std::string>{"/tmp/still.odt"}, "...the files to reopen are no setting");
        Check(s.GetToolLayout() == ToolLayout::Panels && !s.GetToolRail(), "...the tools back in panels");
        Check(!s.GetPanelState(OfficeKind::Document).empty(), "...the panels' order kept, as the recent files are");
    }

    fs::remove_all(folder, ec);
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
