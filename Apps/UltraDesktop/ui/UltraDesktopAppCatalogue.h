// Apps/UltraDesktop/ui/UltraDesktopAppCatalogue.h
// The well-known applications of Windows, macOS and Linux, and the
// freedesktop categories each belongs to - for the entries that declare none
// themselves: Windows' Start-menu shortcuts and Store applications always, a
// macOS bundle without LSApplicationCategoryType, the odd Linux entry
// without Categories=. UltraDesktopAppCategories matches an entry's name,
// program and Start-menu folder against it.
//
// A key is lower-case words, matched as whole words ("word" is not found in
// "password"). A key that begins with '=' is matched only as the whole name:
// "=preview" is macOS' Preview, not "PowerToys (Preview)". The table is read
// top to bottom and the first key found wins, so a key that must beat a
// broader one comes before it. German names are folded (ä -> a, ß -> ss)
// before they are matched, and so are the keys written for them.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>
#include <vector>

namespace UltraDesktop {

struct AppGuess {
    const char* words;        // "media player", "=preview"
    const char* categories;   // as Categories= would have them: "AudioVideo;Video;Player"
};

const std::vector<AppGuess>& KnownApplications();

// macOS: an application's LSApplicationCategoryType
// ("public.app-category.productivity") as freedesktop categories
// ({"Office", "ProjectManagement"}); empty for a type it does not know.
std::vector<std::string> CategoriesOfMacCategoryType(const std::string& type);

} // namespace UltraDesktop
