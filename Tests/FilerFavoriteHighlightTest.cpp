// Tests/FilerFavoriteHighlightTest.cpp
// Display > Favorite folders: how a favorite stands out in the file display -
// no highlight, a solid background, a border or a shade, in one colour, with
// or without the heart. Two places carry it:
//   * the file display (UltraCanvasFilerWidget::SetFavoriteHighlight /
//     SetFavoriteHeartShown): nothing beyond the heart by default, each look
//     and colour taken as given;
//   * UltraFiler's config file (Apps/UltraFiler/UltraFilerSettings.h), read
//     from a temporary folder here: every look and the colour across a
//     restart, an unknown look read as none, and the colour the highlight is
//     drawn in - the default rose turned into the heart's red for a border
//     and into a dark tint under a dark theme, a colour of one's own kept.
// Headless.
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraFilerSettings.h"        // Apps/UltraFiler

#include <cstdlib>   // setenv
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
// Declared rather than taken from <windows.h>, for the reason
// UltraCanvasPathUtf8.h gives for GetEnvironmentVariableW.
extern "C" __declspec(dllimport) int __stdcall SetEnvironmentVariableW(
        const wchar_t* lpName, const wchar_t* lpValue);
#endif

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

void RedirectConfigDirectory(const std::filesystem::path& root) {
#if defined(_WIN32) || defined(_WIN64)
    SetEnvironmentVariableW(L"APPDATA", root.c_str());
#elif defined(__APPLE__)
    setenv("HOME", PathToUtf8(root).c_str(), 1);
#else
    setenv("XDG_CONFIG_HOME", PathToUtf8(root).c_str(), 1);
#endif
}

void WriteConfig(const std::string& lines) {
    std::error_code ec;
    std::filesystem::create_directories(
            PathFromUtf8(UltraFilerSettings::GetConfigDirectory()), ec);
    std::ofstream file(PathFromUtf8(UltraFilerSettings::GetConfigPath()),
                       std::ios::trunc);
    file << "# UltraFiler Configuration\n\n" << lines;
}

UltraFilerSettings Loaded() {
    UltraFilerSettings settings;
    settings.Load();
    return settings;
}

// ===== THE FILE DISPLAY =====
void TestWidget() {
    std::cout << "\n-- the file display --\n";
    UltraCanvasFilerWidget filer("test-filer", 0, 0, 400, 300);
    Check(filer.GetFavoriteHighlight() == FilerFavoriteHighlight::NoHighlight,
          "no highlight by default");
    Check(filer.IsFavoriteHeartShown(), "the heart is shown by default");

    const Color green(46, 125, 50, 255);
    for (FilerFavoriteHighlight look : {FilerFavoriteHighlight::Fill, FilerFavoriteHighlight::Border,
                                        FilerFavoriteHighlight::Shade, FilerFavoriteHighlight::NoHighlight}) {
        filer.SetFavoriteHighlight(look, green);
        Check(filer.GetFavoriteHighlight() == look && filer.GetFavoriteHighlightColor() == green,
              "look " + std::to_string(static_cast<int>(look)) + " and its colour are taken");
    }
    filer.SetFavoriteHeartShown(false);
    Check(!filer.IsFavoriteHeartShown(), "the heart can be left out");
    filer.SetFavoriteHeartShown(true);
    Check(filer.IsFavoriteHeartShown(), "and shown again");
}

// ===== ULTRAFILER'S SETTING =====
void TestSetting() {
    std::cout << "\n-- UltraFiler's saved choice --\n";
    std::error_code ec;
    const std::filesystem::path root =
            std::filesystem::temp_directory_path(ec) / "ultrafiler-favorite-highlight-test";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    if (ec) {
        Check(false, "a temporary config directory can be made");
        return;
    }
    RedirectConfigDirectory(root);

    {
        const UltraFilerSettings fresh = Loaded();
        Check(fresh.favoriteHighlight == FilerFavoriteHighlight::NoHighlight,
              "no config file: no highlight, as before");
        Check(fresh.favoriteHeart, "no config file: the heart, as before");
        Check(fresh.favoriteHighlightColor == UltraFilerSettings::kDefaultFavoriteHighlightColor,
              "no config file: the default colour");
    }

    const Color blue(30, 90, 200, 255);
    for (FilerFavoriteHighlight look : {FilerFavoriteHighlight::Fill, FilerFavoriteHighlight::Border,
                                        FilerFavoriteHighlight::Shade, FilerFavoriteHighlight::NoHighlight}) {
        UltraFilerSettings settings;
        settings.favoriteHighlight = look;
        settings.favoriteHighlightColor = blue;
        settings.favoriteHeart = look != FilerFavoriteHighlight::Fill;
        Check(settings.Save(), "the choice is saved with the other settings");
        const UltraFilerSettings back = Loaded();
        Check(back.favoriteHighlight == look && back.favoriteHighlightColor == blue &&
                      back.favoriteHeart == (look != FilerFavoriteHighlight::Fill),
              "comes back after a restart: look " + std::to_string(static_cast<int>(look)));
    }

    WriteConfig("display.favorites.highlight = sparkles\n");
    Check(Loaded().favoriteHighlight == FilerFavoriteHighlight::NoHighlight,
          "an unknown look is read as none");

    std::cout << "\n-- the colour it is drawn in --\n";
    const Color lightGround(250, 250, 250, 255), darkGround(34, 34, 34, 255);
    UltraFilerSettings s;
    s.favoriteHighlight = FilerFavoriteHighlight::Fill;
    Check(s.EffectiveFavoriteHighlightColor(false, lightGround) ==
                  UltraFilerSettings::kDefaultFavoriteHighlightColor,
          "default colour, light theme: the rose itself");
    const Color tint = s.EffectiveFavoriteHighlightColor(true, darkGround);
    Check(tint.r > darkGround.r && tint.r < UltraFilerSettings::kFavoriteHeartRed.r &&
                  tint.g < 100 && tint.b < 100,
          "default colour, dark theme: a dark tint of the heart's red");
    s.favoriteHighlight = FilerFavoriteHighlight::Border;
    Check(s.EffectiveFavoriteHighlightColor(false, lightGround) == UltraFilerSettings::kFavoriteHeartRed &&
                  s.EffectiveFavoriteHighlightColor(true, darkGround) == UltraFilerSettings::kFavoriteHeartRed,
          "default colour, border: the heart's red in either theme");
    s.favoriteHighlightColor = blue;
    for (FilerFavoriteHighlight look : {FilerFavoriteHighlight::Fill, FilerFavoriteHighlight::Border,
                                        FilerFavoriteHighlight::Shade}) {
        s.favoriteHighlight = look;
        Check(s.EffectiveFavoriteHighlightColor(false, lightGround) == blue &&
                      s.EffectiveFavoriteHighlightColor(true, darkGround) == blue,
              "a colour of one's own is used as it is: look " + std::to_string(static_cast<int>(look)));
    }

    std::filesystem::remove_all(root, ec);
}

}  // namespace

int main() {
    std::cout << "=== Filer favorite highlight ===\n";
    TestWidget();
    TestSetting();
    std::cout << "\n" << (g_failures == 0 ? "All favorite highlight tests passed."
                                          : "SOME FAVORITE HIGHLIGHT TESTS FAILED.")
              << "\n";
    return g_failures == 0 ? 0 : 1;
}
