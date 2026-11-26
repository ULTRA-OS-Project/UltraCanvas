// Apps/UltraOffice/UltraOfficeSettings.h
// What UltraOffice remembers and what its Settings window sets, by name:
// a typed view over the window's UltraCanvasAppSettings
// (<settings folder>/UltraOffice/settings.json). Every value has its default
// here, is kept within its range when it is read and written, and is saved
// with Save(). No window or display is needed, so the model is tested on its
// own (Tests/UltraOfficeSettingsTest.cpp).
//
// Version: 0.2.0 - the font lists: every installed font (FontChoices gone), the recent fonts kept
//                  (GetRecentFonts / RememberRecentFonts)
// Version: 0.1.0 - the Settings window's model: General (name, language, theme, recent files, reopening),
//                  Saving (format, autosave, recovery copies, interval), Documents (font, size, page,
//                  navigator, fit and width share), Spreadsheets (gridlines), Presentations (slide size,
//                  which monitor the show goes to, presenter view)
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraOfficeHost.h"   // SaveFormat

#include "UltraCanvasAppSettings.h"

#include <string>
#include <vector>

namespace UltraOffice {

// The look: the desktop's, or always light or dark (ThemePolicy).
enum class ThemeChoice { System, Light, Dark };
// The page a new document starts on.
enum class PageSizeChoice { A4, Letter };
// How a document tab zooms: 100 %, or one of the editor's fitting zooms.
enum class DocumentFit { Actual, PageWidth, WholePage, WidthShare };
// The slides of a new presentation: 16:9 or 4:3.
enum class SlideSizeChoice { Widescreen, Standard };

class OfficeSettings {
public:
    explicit OfficeSettings(UltraCanvas::UltraCanvasAppSettings& store) : store_(store) {}

    UltraCanvas::UltraCanvasAppSettings& Store() { return store_; }
    const UltraCanvas::UltraCanvasAppSettings& Store() const { return store_; }
    bool Save() const { return store_.Save(); }
    // Every setting below back to its default (the recent files and the
    // files to reopen are kept: they are not settings).
    void RestoreDefaults();

    // ===== GENERAL =====
    // The name comments and tracked changes are signed with; empty: the
    // login name (LoginName).
    std::string GetAuthorName() const;
    void SetAuthorName(const std::string& name);
    std::string EffectiveAuthorName() const;
    static std::string LoginName();
    // "system" (the language every UltraCanvas application shares), or a
    // language tag ("de", "pt-BR") for UltraOffice alone. Read at the start.
    std::string GetLanguage() const;
    void SetLanguage(const std::string& language);
    ThemeChoice GetTheme() const;
    void SetTheme(ThemeChoice theme);
    // How many recent files are kept and offered: 0 to 30, 8 by default.
    static constexpr int kMaxRecentFiles = 30;
    int GetRecentCount() const;
    void SetRecentCount(int count);   // keeps that many of the list
    std::vector<std::string> GetRecentFiles() const;
    void RememberRecentFile(const std::string& path);
    void ForgetRecentFile(const std::string& path);
    void ClearRecentFiles();
    // Whether the files open when UltraOffice closes open again at the next
    // start (one started with files of its own opens those instead).
    bool GetReopenFiles() const;
    void SetReopenFiles(bool reopen);
    std::vector<std::string> GetOpenFiles() const;
    void SetOpenFiles(const std::vector<std::string>& paths);

    // ===== SAVING =====
    SaveFormat GetSaveFormat() const;
    void SetSaveFormat(SaveFormat format);
    // Autosave: each document that has a file of a type it can be written to
    // is saved there by itself, every GetAutosaveMinutes. Off by default.
    bool GetAutosave() const;
    void SetAutosave(bool on);
    // Recovery copies: a copy of each document with unsaved changes in the
    // recovery folder, offered back after a run that did not close. On by
    // default.
    bool GetRecoveryCopies() const;
    void SetRecoveryCopies(bool on);
    // How often both happen: 1 to 60 minutes, 1 by default.
    int GetAutosaveMinutes() const;
    void SetAutosaveMinutes(int minutes);

    // ===== DOCUMENTS =====
    // The font and size a new document's text has: "" is the editor's
    // default font, any installed family otherwise (the font lists are
    // UltraCanvasFontPicker, every installed font); 6 to 96 points, 12 by
    // default. The sizes the lists offer:
    static std::vector<float> FontSizeChoices() { return {8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72}; }
    std::string GetDocumentFont() const;
    void SetDocumentFont(const std::string& family);
    float GetDocumentFontSize() const;
    void SetDocumentFontSize(float points);
    // The fonts chosen last in any font list, newest first: the lists'
    // "Recent" section, shared by every tab. Kept: kMaxRecentFonts.
    static constexpr int kMaxRecentFonts = 10;
    std::vector<std::string> GetRecentFonts() const;
    void SetRecentFonts(const std::vector<std::string>& families);
    PageSizeChoice GetPageSize() const;
    void SetPageSize(PageSizeChoice size);
    // The navigator beside a document (F5); every tab follows it.
    bool GetShowNavigator() const;
    void SetShowNavigator(bool show);
    // The fit a document tab opens with - the last one chosen in the view
    // group - and the width share's share of the view (0.1 to 1, 0.6).
    DocumentFit GetDocumentFit() const;
    void SetDocumentFit(DocumentFit fit);
    float GetWidthShare() const;
    void SetWidthShare(float share);
    // The shares the view group's list offers.
    static std::vector<float> WidthShareChoices() { return {0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f}; }

    // ===== SPREADSHEETS =====
    bool GetShowGridlines() const;
    void SetShowGridlines(bool show);

    // ===== PRESENTATIONS =====
    SlideSizeChoice GetSlideSize() const;
    void SetSlideSize(SlideSizeChoice size);
    // With two monitors: the show on the second (and the presenter view on
    // this one), or the other way round.
    bool GetShowOnSecondScreen() const;
    void SetShowOnSecondScreen(bool second);
    // With two monitors, the presenter view - the next slide, the notes, the
    // time - on the monitor the show is not on.
    bool GetPresenterView() const;
    void SetPresenterView(bool on);

private:
    UltraCanvas::UltraCanvasAppSettings& store_;
};

} // namespace UltraOffice
