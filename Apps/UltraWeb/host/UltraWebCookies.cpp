// Apps/UltraWeb/host/UltraWebCookies.cpp
// UltraWeb's shared cookie jar (UltraWebCookies.h).
// Version: 0.2.0 - the rules moved to UltraNetCookieJar
// Version: 0.1.0
// Last Modified: 2026-10-22
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebCookies.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"

#include <filesystem>

using namespace UltraCanvas;

namespace UltraWeb {

std::shared_ptr<UltraNetCookieJar> SharedCookieJar() {
    static std::shared_ptr<UltraNetCookieJar> jar = []() {
        const std::filesystem::path settings = UltraCanvasSettingsFolder();
        return std::make_shared<UltraNetCookieJar>(settings.empty() ? std::string()
                                                                    : PathToUtf8(settings / "UltraWeb" / "cookies.txt"));
    }();
    return jar;
}

} // namespace UltraWeb
