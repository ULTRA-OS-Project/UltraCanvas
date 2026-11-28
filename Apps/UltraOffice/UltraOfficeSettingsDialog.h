// Apps/UltraOffice/UltraOfficeSettingsDialog.h
// UltraOffice's Settings window: the framework's settings window
// (UltraCanvasSettingsPageKit.h - the tree of pages, the page, Restore
// defaults and Close, as UltraFiler's and UltraSnip's are) over the values
// UltraOfficeSettings.h names, on five pages: General, Saving, Documents,
// Spreadsheets and Presentations. Each change is saved at once and applied
// at once where it can be (the theme, the author name, the gridlines, the
// navigator, the zoom of the open documents, the autosave interval); the
// language at the next start, and a new document's font, page and slide size
// when one is made - each page says which.
//
// Version: 0.1.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace UltraCanvas::SettingsPageKit { class UltraCanvasSettingsWindow; }

namespace UltraOffice {

class UltraOfficeWindow;

// The pages, by the id of their tree row.
inline constexpr const char* kSettingsGeneral = "general";
inline constexpr const char* kSettingsSaving = "saving";
inline constexpr const char* kSettingsDocuments = "documents";
inline constexpr const char* kSettingsSpreadsheets = "spreadsheets";
inline constexpr const char* kSettingsPresentations = "presentations";

// Opens the window over `window` on `page` (General when empty), or brings
// the open one forward on it.
void ShowSettingsDialog(UltraOfficeWindow& window, const std::string& page = std::string());
// The open window; null when none is open. Its controls are found by id
// ("office-set-...") in its window.
UltraCanvas::SettingsPageKit::UltraCanvasSettingsWindow* GetSettingsDialog();
// Closes it, without waiting for an event loop (the window it belongs to is
// going).
void CloseSettingsDialog();

} // namespace UltraOffice
