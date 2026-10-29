// Tests/ArtCreatorSettingsTest.cpp
// ArtCreator's settings file remembers the colour picker: the design chosen in
// its list (picker.design) and whether its channels are open
// (picker.channels), so the next window opens the picker the way it was left.
//
// The designs are written under the framework's names for them
// (ColorPickerWheelStyleName / ColorPickerWheelStyleFromName), the names
// UltraPaint's settings use too: each of the four goes to its name and back,
// and a name that is none of them changes nothing.
//
// It also keeps the rotation handle's look (handles.rotation) and the
// transform bar's centre fields (transform.centre), and two windows that
// change different settings through Update keep each other's changes.
//
// The settings folder is pointed at a scratch folder first, so the user's own
// settings are never read or written. Headless.
// Version: 1.1.0 - the rotation keys; Update keeps another window's changes; no
//                  temporary file is left behind
// Version: 1.0.0
// Last Modified: 2026-10-29
// Author: UltraCanvas Framework

#include "ArtCreatorSettings.h"
#include "UltraCanvasPathUtf8.h"

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

const ColorPickerWheelStyle kDesigns[] = {
    ColorPickerWheelStyle::Ring, ColorPickerWheelStyle::Bar,
    ColorPickerWheelStyle::HueLightnessField, ColorPickerWheelStyle::HueLightnessSliders,
};

} // namespace

int main() {
    std::cerr << "========================================" << std::endl;
    std::cerr << "   ArtCreator Settings Suite"              << std::endl;
    std::cerr << "========================================" << std::endl;

    // ===== The framework's names for the designs =====
    {
        TEST("the circle is \"circle\"",
             std::string(ColorPickerWheelStyleName(ColorPickerWheelStyle::Ring)) == "circle");
        TEST("the slider and square is \"square\"",
             std::string(ColorPickerWheelStyleName(ColorPickerWheelStyle::Bar)) == "square");
        TEST("the rectangle is \"rectangle\"",
             std::string(ColorPickerWheelStyleName(ColorPickerWheelStyle::HueLightnessField)) == "rectangle");
        TEST("the two sliders are \"sliders\"",
             std::string(ColorPickerWheelStyleName(ColorPickerWheelStyle::HueLightnessSliders)) == "sliders");

        bool all = true;
        for (ColorPickerWheelStyle design : kDesigns) {
            ColorPickerWheelStyle back = design == ColorPickerWheelStyle::Ring ? ColorPickerWheelStyle::Bar
                                                                               : ColorPickerWheelStyle::Ring;
            all = all && ColorPickerWheelStyleFromName(ColorPickerWheelStyleName(design), back) &&
                  back == design;
        }
        TEST("each name stands for its design", all);

        ColorPickerWheelStyle kept = ColorPickerWheelStyle::HueLightnessField;
        TEST("a name that is none of them is refused",
             !ColorPickerWheelStyleFromName("hexagon", kept) && !ColorPickerWheelStyleFromName("", kept) &&
             !ColorPickerWheelStyleFromName("Circle", kept));
        TEST("and leaves the design as it was", kept == ColorPickerWheelStyle::HueLightnessField);
    }

    // Every variable any platform finds its settings folder by, at a scratch
    // folder of the test's own.
    const std::filesystem::path scratch =
            std::filesystem::temp_directory_path() / "ArtCreatorSettingsTest";
    std::filesystem::remove_all(scratch);
    std::filesystem::create_directories(scratch);
    const std::string root = PathToUtf8(scratch);
    SetEnv("APPDATA", root);
    SetEnv("XDG_CONFIG_HOME", root);
    SetEnv("HOME", root);

    const std::filesystem::path path = ArtCreatorSettings::ConfigPath();
    const std::string pathText = PathToUtf8(path);
    if (path.empty() || pathText.compare(0, root.size(), root) != 0) {
        std::cerr << "FAIL: the settings file is not in the scratch folder: " << pathText << std::endl;
        return 1;
    }
    TEST("the file is ArtCreator's own config.ini",
         path.filename() == "config.ini" && path.parent_path().filename() == "ArtCreator");

    // ===== Defaults =====
    {
        ArtCreatorSettings s;
        TEST("with no file there is nothing to load", !s.Load());
        TEST("the picker starts as the colour circle", s.pickerDesign == ColorPickerWheelStyle::Ring);
        TEST("with its channels folded", !s.pickerChannelsOpen);
    }

    // ===== The two keys round trip =====
    {
        ArtCreatorSettings s;
        s.pickerDesign = ColorPickerWheelStyle::Bar;
        s.pickerChannelsOpen = true;
        TEST("the settings are saved", s.Save());
        const std::string text = ReadFile(path);
        TEST("the file names the design", text.find("picker.design = square\n") != std::string::npos);
        TEST("and says the channels are open", text.find("picker.channels = open\n") != std::string::npos);

        ArtCreatorSettings loaded;
        TEST("they load", loaded.Load());
        TEST("the design comes back", loaded.pickerDesign == ColorPickerWheelStyle::Bar);
        TEST("the open channels come back", loaded.pickerChannelsOpen);
    }
    {
        bool all = true;
        for (ColorPickerWheelStyle design : kDesigns) {
            ArtCreatorSettings s;
            s.pickerDesign = design;
            s.Save();
            ArtCreatorSettings loaded;
            loaded.pickerDesign = design == ColorPickerWheelStyle::Ring ? ColorPickerWheelStyle::Bar
                                                                        : ColorPickerWheelStyle::Ring;
            loaded.pickerChannelsOpen = true;
            loaded.Load();
            all = all && loaded.pickerDesign == design && !loaded.pickerChannelsOpen;
        }
        TEST("each of the four designs comes back, folded channels too", all);
    }

    // ===== What the file cannot mean =====
    {
        WriteFile(path, "# by hand\r\n\r\npicker.design = hexagon\r\npicker.channels = maybe\r\n");
        ArtCreatorSettings s;
        s.pickerDesign = ColorPickerWheelStyle::HueLightnessSliders;
        s.pickerChannelsOpen = true;
        TEST("a file of unknown values still loads", s.Load());
        TEST("an unknown design leaves the one there was",
             s.pickerDesign == ColorPickerWheelStyle::HueLightnessSliders);
        TEST("an unknown channels state leaves the one there was", s.pickerChannelsOpen);
    }
    {
        WriteFile(path, "  picker.design\t=  rectangle  \r\npicker.channels=folded\r\n");
        ArtCreatorSettings s;
        s.pickerChannelsOpen = true;
        s.Load();
        TEST("spaces, tabs and Windows line ends around a value are read past",
             s.pickerDesign == ColorPickerWheelStyle::HueLightnessField && !s.pickerChannelsOpen);
    }

    // ===== The rotation keys round trip =====
    {
        std::filesystem::remove(path);
        ArtCreatorSettings s;
        TEST("the rotation handle starts as a dial", s.rotationHandle == VectorRotationHandleStyle::Dial);
        TEST("and the centre fields on the page", !s.centreRelative);
        s.rotationHandle = VectorRotationHandleStyle::Stem;
        s.centreRelative = true;
        s.Save();
        ArtCreatorSettings loaded;
        loaded.Load();
        TEST("a stem and relative centre fields come back",
             loaded.rotationHandle == VectorRotationHandleStyle::Stem && loaded.centreRelative);
        std::filesystem::path temporary = path;
        temporary += ".new";
        TEST("saving leaves no temporary file behind", !std::filesystem::exists(temporary));
    }

    // ===== Two windows, each changing its own setting =====
    {
        std::filesystem::remove(path);
        ArtCreatorSettings first, second;   // both opened before either changed anything
        first.Load();
        second.Load();
        first.Update([](ArtCreatorSettings& s) { s.rotationHandle = VectorRotationHandleStyle::Stem; });
        TEST("Update changes the window's own copy too", first.rotationHandle == VectorRotationHandleStyle::Stem);
        second.Update([](ArtCreatorSettings& s) { s.pickerDesign = ColorPickerWheelStyle::Bar; });
        ArtCreatorSettings loaded;
        loaded.Load();
        TEST("the second window's change is saved", loaded.pickerDesign == ColorPickerWheelStyle::Bar);
        TEST("and the first window's stays", loaded.rotationHandle == VectorRotationHandleStyle::Stem);
        TEST("the second window still shows what it opened with for the rest",
             second.rotationHandle == VectorRotationHandleStyle::Dial);
        first.Update([](ArtCreatorSettings& s) { s.centreRelative = true; });
        loaded.Load();
        TEST("a later change by the first keeps the second's",
             loaded.pickerDesign == ColorPickerWheelStyle::Bar && loaded.centreRelative &&
             loaded.rotationHandle == VectorRotationHandleStyle::Stem);
    }

    std::filesystem::remove_all(scratch);
    std::cerr << "========================================" << std::endl;
    std::cerr << "Tests: " << testCount << ", failed: " << failCount << std::endl;
    return failCount == 0 ? 0 : 1;
}
