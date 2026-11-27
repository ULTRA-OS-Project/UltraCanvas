// Apps/UltraOffice/UltraOfficeSettings.cpp
// See the header.
//
// Version: 0.2.0 - the recent fonts ("recentFonts")
// Version: 0.1.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeSettings.h"

#include "UltraCanvasPathUtf8.h"   // GetEnvUtf8

#include <algorithm>
#include <cmath>

namespace UltraOffice {

using namespace UltraCanvas;

namespace {

// The keys of settings.json. "recent", "saveFormat", "documentNavigator"
// and "documentNavigatorWidth" were there before the Settings window.
const char* kAuthorKey = "authorName";
const char* kLanguageKey = "language";
const char* kThemeKey = "theme";
const char* kRecentKey = "recent";
const char* kRecentCountKey = "recentCount";
const char* kReopenKey = "reopenFiles";
const char* kOpenFilesKey = "openFiles";
const char* kFormatKey = "saveFormat";
const char* kAutosaveKey = "autosave";
const char* kRecoveryKey = "recoveryCopies";
const char* kAutosaveMinutesKey = "autosaveMinutes";
const char* kDocumentFontKey = "documentFont";
const char* kDocumentFontSizeKey = "documentFontSize";
const char* kRecentFontsKey = "recentFonts";   // not a setting: kept by RestoreDefaults, like the recent files
const char* kPageSizeKey = "pageSize";
const char* kNavigatorKey = "documentNavigator";
const char* kDocumentFitKey = "documentFit";
const char* kWidthShareKey = "documentWidthShare";
const char* kGridlinesKey = "sheetGridlines";
const char* kSlideSizeKey = "slideSize";
const char* kSecondScreenKey = "showOnSecondScreen";
const char* kPresenterKey = "presenterView";

constexpr int kDefaultRecent = 8;
constexpr float kDefaultFontSize = 12.0f;
constexpr float kDefaultShare = 0.6f;

} // namespace

void OfficeSettings::RestoreDefaults() {
    for (const char* key : {kAuthorKey, kLanguageKey, kThemeKey, kRecentCountKey, kReopenKey, kFormatKey, kAutosaveKey,
                            kRecoveryKey, kAutosaveMinutesKey, kDocumentFontKey, kDocumentFontSizeKey, kPageSizeKey,
                            kNavigatorKey, kDocumentFitKey, kWidthShareKey, kGridlinesKey, kSlideSizeKey,
                            kSecondScreenKey, kPresenterKey})
        store_.Remove(key);
}

// ===== GENERAL =====

std::string OfficeSettings::GetAuthorName() const { return store_.GetString(kAuthorKey); }
void OfficeSettings::SetAuthorName(const std::string& name) { store_.Set(kAuthorKey, name); }

std::string OfficeSettings::LoginName() {
    // UTF-8, as a document stores it.
    std::string user = GetEnvUtf8("USER");
    if (user.empty()) user = GetEnvUtf8("USERNAME");
    return user;
}

std::string OfficeSettings::EffectiveAuthorName() const {
    const std::string name = GetAuthorName();
    return name.empty() ? LoginName() : name;
}

std::string OfficeSettings::GetLanguage() const {
    const std::string language = store_.GetString(kLanguageKey, "system");
    return language.empty() ? std::string("system") : language;
}
void OfficeSettings::SetLanguage(const std::string& language) {
    store_.Set(kLanguageKey, language.empty() ? std::string("system") : language);
}

ThemeChoice OfficeSettings::GetTheme() const {
    const std::string theme = store_.GetString(kThemeKey, "system");
    return theme == "light" ? ThemeChoice::Light : theme == "dark" ? ThemeChoice::Dark : ThemeChoice::System;
}
void OfficeSettings::SetTheme(ThemeChoice theme) {
    store_.Set(kThemeKey, std::string(theme == ThemeChoice::Light ? "light" : theme == ThemeChoice::Dark ? "dark" : "system"));
}

int OfficeSettings::GetRecentCount() const {
    return std::clamp(store_.GetInt(kRecentCountKey, kDefaultRecent), 0, kMaxRecentFiles);
}
void OfficeSettings::SetRecentCount(int count) {
    count = std::clamp(count, 0, kMaxRecentFiles);
    store_.Set(kRecentCountKey, count);
    std::vector<std::string> recent = store_.GetList(kRecentKey);
    if (static_cast<int>(recent.size()) > count) {
        recent.resize(static_cast<size_t>(count));
        store_.Set(kRecentKey, recent);
    }
}

std::vector<std::string> OfficeSettings::GetRecentFiles() const {
    std::vector<std::string> recent = store_.GetList(kRecentKey);
    const size_t count = static_cast<size_t>(GetRecentCount());
    if (recent.size() > count) recent.resize(count);
    return recent;
}
void OfficeSettings::RememberRecentFile(const std::string& path) {
    const int count = GetRecentCount();
    if (count <= 0) { store_.Remove(kRecentKey); return; }
    store_.Remember(kRecentKey, path, static_cast<size_t>(count));
}
void OfficeSettings::ForgetRecentFile(const std::string& path) { store_.Forget(kRecentKey, path); }
void OfficeSettings::ClearRecentFiles() { store_.Remove(kRecentKey); }

bool OfficeSettings::GetReopenFiles() const { return store_.GetBool(kReopenKey, false); }
void OfficeSettings::SetReopenFiles(bool reopen) { store_.Set(kReopenKey, reopen); }
std::vector<std::string> OfficeSettings::GetOpenFiles() const { return store_.GetList(kOpenFilesKey); }
void OfficeSettings::SetOpenFiles(const std::vector<std::string>& paths) { store_.Set(kOpenFilesKey, paths); }

// ===== SAVING =====

SaveFormat OfficeSettings::GetSaveFormat() const {
    return store_.GetString(kFormatKey, "ooxml") == "odf" ? SaveFormat::OpenDocument : SaveFormat::OfficeOpenXml;
}
void OfficeSettings::SetSaveFormat(SaveFormat format) {
    store_.Set(kFormatKey, std::string(format == SaveFormat::OpenDocument ? "odf" : "ooxml"));
}

bool OfficeSettings::GetAutosave() const { return store_.GetBool(kAutosaveKey, false); }
void OfficeSettings::SetAutosave(bool on) { store_.Set(kAutosaveKey, on); }
bool OfficeSettings::GetRecoveryCopies() const { return store_.GetBool(kRecoveryKey, true); }
void OfficeSettings::SetRecoveryCopies(bool on) { store_.Set(kRecoveryKey, on); }
int OfficeSettings::GetAutosaveMinutes() const { return std::clamp(store_.GetInt(kAutosaveMinutesKey, 1), 1, 60); }
void OfficeSettings::SetAutosaveMinutes(int minutes) { store_.Set(kAutosaveMinutesKey, std::clamp(minutes, 1, 60)); }

// ===== DOCUMENTS =====

std::string OfficeSettings::GetDocumentFont() const { return store_.GetString(kDocumentFontKey); }
void OfficeSettings::SetDocumentFont(const std::string& family) { store_.Set(kDocumentFontKey, family); }

std::vector<std::string> OfficeSettings::GetRecentFonts() const {
    std::vector<std::string> families = store_.GetList(kRecentFontsKey);
    families.erase(std::remove(families.begin(), families.end(), std::string()), families.end());
    if (families.size() > static_cast<size_t>(kMaxRecentFonts)) families.resize(static_cast<size_t>(kMaxRecentFonts));
    return families;
}

void OfficeSettings::SetRecentFonts(const std::vector<std::string>& families) {
    std::vector<std::string> kept;
    for (const std::string& family : families) {
        if (family.empty() || std::find(kept.begin(), kept.end(), family) != kept.end()) continue;
        kept.push_back(family);
        if (kept.size() >= static_cast<size_t>(kMaxRecentFonts)) break;
    }
    store_.Set(kRecentFontsKey, kept);
}

float OfficeSettings::GetDocumentFontSize() const {
    const double size = store_.GetNumber(kDocumentFontSizeKey, kDefaultFontSize);
    return std::isfinite(size) ? std::clamp(static_cast<float>(size), 6.0f, 96.0f) : kDefaultFontSize;
}
void OfficeSettings::SetDocumentFontSize(float points) {
    if (!std::isfinite(points)) return;
    store_.Set(kDocumentFontSizeKey, static_cast<double>(std::clamp(points, 6.0f, 96.0f)));
}

PageSizeChoice OfficeSettings::GetPageSize() const {
    return store_.GetString(kPageSizeKey, "a4") == "letter" ? PageSizeChoice::Letter : PageSizeChoice::A4;
}
void OfficeSettings::SetPageSize(PageSizeChoice size) {
    store_.Set(kPageSizeKey, std::string(size == PageSizeChoice::Letter ? "letter" : "a4"));
}

bool OfficeSettings::GetShowNavigator() const { return store_.GetBool(kNavigatorKey, false); }
void OfficeSettings::SetShowNavigator(bool show) { store_.Set(kNavigatorKey, show); }

DocumentFit OfficeSettings::GetDocumentFit() const {
    const std::string fit = store_.GetString(kDocumentFitKey, "pageWidth");
    if (fit == "actual") return DocumentFit::Actual;
    if (fit == "wholePage") return DocumentFit::WholePage;
    if (fit == "widthShare") return DocumentFit::WidthShare;
    return DocumentFit::PageWidth;
}
void OfficeSettings::SetDocumentFit(DocumentFit fit) {
    const char* name = fit == DocumentFit::Actual      ? "actual"
                     : fit == DocumentFit::WholePage   ? "wholePage"
                     : fit == DocumentFit::WidthShare  ? "widthShare"
                                                       : "pageWidth";
    store_.Set(kDocumentFitKey, std::string(name));
}

float OfficeSettings::GetWidthShare() const {
    const double share = store_.GetNumber(kWidthShareKey, kDefaultShare);
    return std::isfinite(share) ? std::clamp(static_cast<float>(share), 0.1f, 1.0f) : kDefaultShare;
}
void OfficeSettings::SetWidthShare(float share) {
    if (!std::isfinite(share)) return;
    // Whole percents: the list and the box show them so.
    const double rounded = std::round(static_cast<double>(std::clamp(share, 0.1f, 1.0f)) * 100.0) / 100.0;
    store_.Set(kWidthShareKey, rounded);
}

// ===== SPREADSHEETS =====

bool OfficeSettings::GetShowGridlines() const { return store_.GetBool(kGridlinesKey, true); }
void OfficeSettings::SetShowGridlines(bool show) { store_.Set(kGridlinesKey, show); }

// ===== PRESENTATIONS =====

SlideSizeChoice OfficeSettings::GetSlideSize() const {
    return store_.GetString(kSlideSizeKey, "16:9") == "4:3" ? SlideSizeChoice::Standard : SlideSizeChoice::Widescreen;
}
void OfficeSettings::SetSlideSize(SlideSizeChoice size) {
    store_.Set(kSlideSizeKey, std::string(size == SlideSizeChoice::Standard ? "4:3" : "16:9"));
}

bool OfficeSettings::GetShowOnSecondScreen() const { return store_.GetBool(kSecondScreenKey, true); }
void OfficeSettings::SetShowOnSecondScreen(bool second) { store_.Set(kSecondScreenKey, second); }
bool OfficeSettings::GetPresenterView() const { return store_.GetBool(kPresenterKey, true); }
void OfficeSettings::SetPresenterView(bool on) { store_.Set(kPresenterKey, on); }

} // namespace UltraOffice
