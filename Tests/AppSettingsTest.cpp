// Tests/AppSettingsTest.cpp
// UltraCanvasAppSettings on a temp folder: typed values in and out, dotted
// keys as nested objects, a list and a recent list on it, the file written
// pretty and read back, a missing or broken file as the defaults, and the
// file named under the settings folder the environment names. Headless.
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasAppSettings.h"
#include "UltraCanvasPathUtf8.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_checks = 0;
static int g_failures = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

static std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

int main() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() /
        ("appsettings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path file = dir / "UltraPress" / "settings.json";

    std::printf("Nothing yet\n");
    UltraCanvasAppSettings s = UltraCanvasAppSettings::AtFile(PathToUtf8(file));
    Check(s.File() == PathToUtf8(file), "the file is the one named");
    Check(!s.Load(), "a missing file does not load");
    Check(!s.Has("look") && s.GetString("look", "light") == "light" && s.GetInt("window.width", 1500) == 1500 &&
              !s.GetBool("dark", false) && s.GetList("recent").empty() && s.GetNumber("zoom", 1.5) == 1.5,
          "and every value is its fallback");

    std::printf("Values\n");
    s.Set("look", "dark");
    s.Set("dark", true);
    s.Set("window.width", 1200);
    s.Set("window.height", 800);
    s.Set("zoom", 1.25);
    s.Set("recent", std::vector<std::string>{"/sites/one", "/sites/two"});
    Check(s.Has("look") && s.GetString("look") == "dark" && s.GetBool("dark") && s.GetInt("window.width") == 1200 &&
              s.GetInt("window.height") == 800 && s.GetNumber("zoom") == 1.25 &&
              s.GetList("recent") == std::vector<std::string>{"/sites/one", "/sites/two"},
          "typed values come back as set");
    Check(s.Has("window") && s.Document()["window"].IsObject() && s.Document()["window"]["width"].GetInteger() == 1200,
          "a dotted key is a nested object");
    Check(s.GetString("dark", "x") == "x" && s.GetInt("look", 7) == 7 && s.GetBool("window.width", true) &&
              s.GetList("look").empty() && s.GetInt("window", 3) == 3,
          "a value of another type is the fallback");
    s.Set("window", 5);
    Check(s.GetInt("window") == 5 && !s.Has("window.width"), "a scalar replaces the object at its key");
    s.Set("window.width", 1000);
    Check(s.GetInt("window.width") == 1000 && !s.Has("window.height"), "and an object the scalar in its way");
    Check(s.Remove("zoom") && !s.Has("zoom") && !s.Remove("zoom") && !s.Remove("no.such.key"), "a value is removed once");
    s.Set("", 1);
    s.Set(".", 2);
    Check(!s.Has("") && s.Document().GetSize() == 4, "an empty key sets nothing");

    std::printf("A recent list\n");
    s.Remember("recent", "/sites/two");
    Check(s.GetList("recent") == std::vector<std::string>{"/sites/two", "/sites/one"}, "an entry remembered again moves to the front, once");
    for (int i = 0; i < 10; ++i) s.Remember("recent", "/sites/more" + std::to_string(i));
    Check(s.GetList("recent").size() == 8 && s.GetList("recent").front() == "/sites/more9", "eight are kept, the newest first");
    s.Remember("recent", "/sites/few", 2);
    Check(s.GetList("recent") == std::vector<std::string>{"/sites/few", "/sites/more9"}, "or as many as asked");
    s.Forget("recent", "/sites/more9");
    s.Forget("recent", "/sites/never");
    Check(s.GetList("recent") == std::vector<std::string>{"/sites/few"}, "an entry is forgotten, an unknown one ignored");
    s.Remember("recent", "");
    Check(s.GetList("recent").size() == 1, "an empty entry is not remembered");
    s.Remember("fresh", "a");
    Check(s.GetList("fresh") == std::vector<std::string>{"a"}, "a list begins with its first entry");

    std::printf("The file\n");
    Check(s.Save() && fs::is_regular_file(file, ec), "saved, the folder made first");
    const std::string text = ReadAll(file);
    Check(text.find("\n  \"look\": \"dark\"") != std::string::npos && text.find("\"width\": 1000") != std::string::npos &&
              text.back() == '\n',
          "pretty, two spaces, a newline at the end");
    UltraCanvasAppSettings back = UltraCanvasAppSettings::AtFile(PathToUtf8(file));
    Check(back.Load() && back.GetString("look") == "dark" && back.GetBool("dark") && back.GetInt("window.width") == 1000 &&
              back.GetList("recent") == std::vector<std::string>{"/sites/few"} && !back.Has("zoom"),
          "and read back whole");
    back.Set("look", "light");
    back.Load();
    Check(back.GetString("look") == "dark", "Load replaces what was held");
    {
        std::ofstream broken(file, std::ios::trunc);
        broken << "{ not json";
    }
    Check(!back.Load() && !back.Has("look") && back.GetString("look", "light") == "light", "a broken file is the defaults");
    {
        std::ofstream array(file, std::ios::trunc);
        array << "[1, 2]";
    }
    Check(!back.Load() && back.Document().IsObject(), "so is a file that is not an object");
    UltraCanvasAppSettings none = UltraCanvasAppSettings::AtFile("");
    Check(!none.Load() && !none.Save() && none.File().empty(), "without a file nothing is read or written");

    std::printf("The application's file\n");
#ifndef _WIN32
    setenv("XDG_CONFIG_HOME", PathToUtf8(dir / "config").c_str(), 1);
    UltraCanvasAppSettings app("UltraPress");
    Check(app.File() == PathToUtf8(dir / "config" / "UltraCanvas" / "UltraPress" / "settings.json"),
          "<settings folder>/<app>/settings.json: " + app.File());
    app.Set("dark", true);
    Check(app.Save() && fs::is_regular_file(dir / "config" / "UltraCanvas" / "UltraPress" / "settings.json", ec),
          "written there");
#endif
    Check(UltraCanvasAppSettings("").File().empty(), "no application, no file");

    fs::remove_all(dir, ec);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
