// OS/Android/UltraCanvasAndroidSystemLanguage.cpp
// SystemLanguageTagsNative on Android: the language and country of the
// device configuration the activity was started with, as "de-AT". Empty
// before the native activity exists (a headless engine), when the POSIX
// variables - which Android does not set either - leave the caller at "en".
// Version: 1.0.1 - the application base is included before the platform header
// Version: 1.0.0
// Last Modified: 2026-11-02
// Author: UltraCanvas Framework
#include "UltraCanvasSystemLanguage.h"

#include "UltraCanvasApplication.h"          // before the platform header, which completes the base it derives from
#include "UltraCanvasAndroidApplication.h"

#include <android/configuration.h>
#include <android_native_app_glue.h>

namespace UltraCanvas {

std::vector<std::string> SystemLanguageTagsNative() {
    std::vector<std::string> tags;
    android_app* app = UltraCanvasAndroidApplication::GetAndroidApp();
    if (!app || !app->config) return tags;
    char language[3] = {0, 0, 0};
    char country[3] = {0, 0, 0};
    AConfiguration_getLanguage(app->config, language);
    AConfiguration_getCountry(app->config, country);
    if (language[0] == '\0') return tags;
    std::string tag(language);
    if (country[0] != '\0') tag += std::string("-") + country;
    tags.push_back(tag);
    return tags;
}

}  // namespace UltraCanvas
