// Apps/UltraPress/ui/UltraPressTheme.cpp
// See UltraPressTheme.h.
// Version: 0.8.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressTheme.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"

#include <filesystem>
#include <fstream>

using namespace UltraCanvas;

namespace UltraPress {

namespace {

const Color kNight(13, 12, 34, 255);
const Color kPaper(255, 255, 255, 255);
const Color kPaperSoft(243, 243, 246, 255);
const Color kOnDark(208, 208, 220, 255);
const Color kInk(32, 32, 32, 255);
const Color kInkSoft(90, 90, 98, 255);
const Color kAccent(169, 7, 7, 255);
const Color kAccentDeep(130, 5, 5, 255);
const Color kAccentOnDark(229, 72, 72, 255);
const Color kRule(220, 220, 224, 255);

const char* kLookFile = "look";

} // namespace

UltraCanvasTheme ThemeFor(bool dark) {
    UltraCanvasTheme t = dark ? UltraCanvasTheme::Dark() : UltraCanvasTheme::Light();
    t.name = dark ? "UltraPress dark" : "UltraPress light";
    ThemeRoles& r = t.roles;
    if (dark) {
        // The framework's dark greys, the brand's red lifted so it reads on them.
        r.accent = kAccentOnDark;
        r.accentHover = Color(240, 100, 100, 255);
        r.accentSoft = Color(74, 30, 30, 255);
        r.onAccent = kPaper;
        r.focus = kAccentOnDark;
    } else {
        // The paper and ink the window has had, the brand's red for the accent.
        r.ground = kPaperSoft;
        r.panel = kPaper;
        r.well = Color(248, 248, 250, 255);
        r.raised = kPaper;
        r.hover = Color(232, 232, 236, 255);
        r.pressed = kRule;
        r.line = kRule;
        r.lineStrong = Color(190, 190, 196, 255);
        r.ink = kInk;
        r.inkMuted = kInkSoft;
        r.inkFaint = Color(150, 150, 156, 255);
        r.accent = kAccent;
        r.accentHover = kAccentDeep;
        r.accentSoft = Color(255, 232, 232, 255);
        r.onAccent = kPaper;
        r.focus = kAccent;
        r.danger = kAccent;
    }
    // The side column: night blue in both looks, its ink and its fills.
    t.SetRole("press.side", kNight);
    t.SetRole("press.on-side", kOnDark);
    t.SetRole("press.on-side-strong", kPaper);
    t.SetRole("press.side-accent", dark ? kAccentOnDark : kAccent);
    t.SetRole("press.side-hover", Color(255, 255, 255, 24));
    t.SetRole("press.side-active", Color(255, 255, 255, 36));
    t.SetRole("press.side-strong", Color(255, 255, 255, 60));
    return t;
}

SideColors SideColorsOf(const UltraCanvasTheme& theme) {
    SideColors c;
    c.panel = theme.Role("press.side", kNight);
    c.ink = theme.Role("press.on-side", kOnDark);
    c.inkStrong = theme.Role("press.on-side-strong", kPaper);
    c.accent = theme.Role("press.side-accent", kAccent);
    c.hover = theme.Role("press.side-hover", Color(255, 255, 255, 24));
    c.active = theme.Role("press.side-active", Color(255, 255, 255, 36));
    c.strong = theme.Role("press.side-strong", Color(255, 255, 255, 60));
    return c;
}

std::string LookSettingsDirectory() {
    const std::filesystem::path folder = UltraCanvasSettingsFolder();
    if (folder.empty()) return "";
    return PathToUtf8(folder / "UltraPress");
}

bool LoadLook(const std::string& settingsDir) {
    if (settingsDir.empty()) return false;
    std::ifstream in(PathFromUtf8(settingsDir) / kLookFile);
    std::string word;
    if (!in || !(in >> word)) return false;
    return word == "dark";
}

bool SaveLook(const std::string& settingsDir, bool dark) {
    if (settingsDir.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(PathFromUtf8(settingsDir), ec);
    std::ofstream out(PathFromUtf8(settingsDir) / kLookFile, std::ios::trunc);
    if (!out) return false;
    out << (dark ? "dark" : "light") << '\n';
    return static_cast<bool>(out);
}

void FollowTheme(const std::shared_ptr<UltraCanvasLabel>& label, LabelVariant variant) {
    label->SetThemeVariant(variant);
    label->SetFollowTheme(true);
}

void FollowTheme(const std::shared_ptr<UltraCanvasButton>& button, ButtonVariant variant) {
    button->SetThemeVariant(variant);
    button->SetFollowTheme(true);
}

} // namespace UltraPress
