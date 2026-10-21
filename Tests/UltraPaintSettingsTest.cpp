// Tests/UltraPaintSettingsTest.cpp
// UltraPaint's settings file remembers the colour picker: the design chosen in
// its list (picker.design) and whether its channels are open
// (picker.channels), so the next editor opens the picker the way it was left.
//
// The picker's own changes are saved with SavePicker(), which writes only
// those two keys and keeps whatever else the file holds: UltraPaint can have
// several windows, each with its own copy of the settings, and one window's
// copy must not put back a magnifier size or a palette mode that another
// window's settings window has changed since.
//
// The settings folder is pointed at a scratch folder first, so the user's own
// settings are never read or written. Headless.
// Version: 1.0.0
// Last Modified: 2026-10-16
// Author: UltraCanvas Framework

#include "UltraPaintSettings.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

namespace {

void SetEnv(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

void WriteFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << text;
}

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   UltraPaint Settings Suite"              << std::endl;
    std::cerr << "========================================" << std::endl;

    // Every variable any platform finds its settings folder by, at a scratch
    // folder of the test's own.
    const std::filesystem::path scratch =
            std::filesystem::temp_directory_path() / "UltraPaintSettingsTest";
    std::filesystem::remove_all(scratch);
    std::filesystem::create_directories(scratch);
    const std::string root = PathToUtf8(scratch);
    SetEnv("APPDATA", root);
    SetEnv("XDG_CONFIG_HOME", root);
    SetEnv("HOME", root);

    const std::filesystem::path path = UltraPaintSettings::ConfigPath();
    const std::string pathText = PathToUtf8(path);
    if (path.empty() || pathText.compare(0, root.size(), root) != 0) {
        std::cerr << "FAIL: the settings file is not in the scratch folder: " << pathText << std::endl;
        return 1;
    }

    // ===== Defaults =====
    {
        UltraPaintSettings s;
        TEST("with no file there is nothing to load", !s.Load());
        TEST("the picker starts as the colour circle", s.pickerDesign == ColorPickerWheelStyle::Ring);
        TEST("with its channels folded", !s.pickerChannelsOpen);
    }

    // ===== The two keys round trip =====
    {
        UltraPaintSettings s;
        s.pickerDesign = ColorPickerWheelStyle::HueLightnessSliders;
        s.pickerChannelsOpen = true;
        TEST("the settings are saved", s.Save());
        const std::string text = ReadFile(path);
        TEST("the file names the design", text.find("picker.design = sliders\n") != std::string::npos);
        TEST("and says the channels are open", text.find("picker.channels = open\n") != std::string::npos);

        UltraPaintSettings loaded;
        TEST("they load", loaded.Load());
        TEST("the design comes back",
             loaded.pickerDesign == ColorPickerWheelStyle::HueLightnessSliders);
        TEST("the open channels come back", loaded.pickerChannelsOpen);
    }
    {
        bool all = true;
        for (ColorPickerWheelStyle design : {ColorPickerWheelStyle::Ring, ColorPickerWheelStyle::Bar,
                                             ColorPickerWheelStyle::HueLightnessField,
                                             ColorPickerWheelStyle::HueLightnessSliders}) {
            UltraPaintSettings s;
            s.pickerDesign = design;
            s.Save();
            UltraPaintSettings loaded;
            loaded.pickerDesign = design == ColorPickerWheelStyle::Ring ? ColorPickerWheelStyle::Bar
                                                                        : ColorPickerWheelStyle::Ring;
            loaded.Load();
            all = all && loaded.pickerDesign == design && !loaded.pickerChannelsOpen;
        }
        TEST("each of the four designs comes back, folded channels too", all);
    }

    // ===== Values the file cannot mean =====
    {
        WriteFile(path, "picker.design = hexagon\npicker.channels = maybe\n");
        UltraPaintSettings s;
        s.pickerDesign = ColorPickerWheelStyle::Bar;
        s.pickerChannelsOpen = true;
        s.Load();
        TEST("an unknown design leaves the one there was",
             s.pickerDesign == ColorPickerWheelStyle::Bar);
        TEST("an unknown channels state leaves the one there was", s.pickerChannelsOpen);
    }

    // ===== SavePicker keeps the rest of the file =====
    {
        // Another window's settings window saved these.
        UltraPaintSettings other;
        other.paletteAdaptive = false;
        other.magnifierSize = 300;
        other.Save();

        // This window read the file before that, so its copy is out of date.
        UltraPaintSettings stale;
        stale.pickerDesign = ColorPickerWheelStyle::HueLightnessField;
        stale.pickerChannelsOpen = true;
        TEST("the picker's state is saved", stale.SavePicker());

        UltraPaintSettings loaded;
        loaded.Load();
        TEST("SavePicker writes the design",
             loaded.pickerDesign == ColorPickerWheelStyle::HueLightnessField);
        TEST("and the channels", loaded.pickerChannelsOpen);
        TEST("and keeps the palette mode the file had, not its own copy's", !loaded.paletteAdaptive);
        TEST("and the magnifier size", loaded.magnifierSize == 300);
    }
    {
        std::filesystem::remove(path);
        UltraPaintSettings s;
        s.pickerDesign = ColorPickerWheelStyle::Bar;
        TEST("with no file yet SavePicker writes one", s.SavePicker());
        UltraPaintSettings loaded;
        TEST("that loads", loaded.Load());
        TEST("with the design, and the defaults beside it",
             loaded.pickerDesign == ColorPickerWheelStyle::Bar && loaded.paletteAdaptive &&
             loaded.magnifierSize == UltraPaintSettings::kDefaultMagnifierSize);
    }

    std::filesystem::remove_all(scratch);
    std::cerr << "========================================" << std::endl;
    std::cerr << "Tests: " << testCount << ", failed: " << failCount << std::endl;
    return failCount == 0 ? 0 : 1;
}
