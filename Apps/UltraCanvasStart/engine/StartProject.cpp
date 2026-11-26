// Apps/UltraCanvasStart/engine/StartProject.cpp
// Version: 0.2.0 - RepointPresets / RepointProjectSdk: an existing project moved
//                  to another SDK; the preset's path is written as a JSON string
// Version: 0.1.2 - main.cpp calls the framework's CreateWindow(config), not a
//                  method of the application (which has none); the skeleton
//                  builds against the SDK again
// Version: 0.1.1 - the instruction file is the chosen assistant's
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartProject.h"

#include "UltraCanvasPathUtf8.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace UltraCanvasStart {

std::string IdentifierFrom(const std::string& name) {
    std::string id;
    for (unsigned char c : name) {
        if (std::isalnum(c) || c == '_') id.push_back(static_cast<char>(c));
    }
    if (id.empty()) id = "MyApp";
    if (std::isdigit(static_cast<unsigned char>(id[0]))) id.insert(id.begin(), 'A');
    return id;
}

namespace {

// Forward slashes, the way CMake wants a path on every platform.
std::string CMakePath(const std::string& utf8) {
    std::string out = utf8;
    for (auto& c : out) if (c == '\\') c = '/';
    return out;
}

// `text` as a JSON string literal, quotes included.
std::string JsonString(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += "\"";
    return out;
}

size_t SkipSpace(const std::string& text, size_t pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    return pos;
}

// The end (one past the closing quote) of the JSON string that starts at
// `open` (its opening quote); npos when it does not end.
size_t StringEnd(const std::string& text, size_t open) {
    for (size_t i = open + 1; i < text.size(); ++i) {
        if (text[i] == '\\') { ++i; continue; }
        if (text[i] == '"') return i + 1;
    }
    return std::string::npos;
}

// A JSON string's contents with the escapes CMakePresets.json uses undone
// (\" and \\); for the message that says what the prefix was.
std::string JsonUnquote(const std::string& literal) {
    std::string out;
    for (size_t i = 1; i + 1 < literal.size(); ++i) {
        if (literal[i] == '\\' && i + 2 < literal.size()) ++i;
        out += literal[i];
    }
    return out;
}

} // namespace

std::string ProjectCMakeLists(const ProjectOptions& options) {
    const std::string app = IdentifierFrom(options.appName);
    std::string text;
    text += "cmake_minimum_required(VERSION 3.16)\n";
    text += "project(" + app + " LANGUAGES CXX)\n\n";
    text += "set(CMAKE_CXX_STANDARD 20)\n";
    text += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";
    if (options.useSdk) {
        text += "# The framework, prebuilt: the UltraCanvas SDK (Docs/UltraCanvasSDK.md).\n";
        text += "# Point CMAKE_PREFIX_PATH at the unpacked SDK, as CMakePresets.json does.\n";
        text += "find_package(UltraCanvas CONFIG REQUIRED)\n\n";
        text += "add_executable(" + app + " main.cpp)\n";
        text += "target_link_libraries(" + app + " PRIVATE\n";
        text += "    UltraCanvas::UltraCanvas\n";
        text += "    ${ULTRACANVAS_PLUGIN_TARGETS}\n";
        text += "    UltraCanvas::UltraCanvasAllFormats)\n";
    } else {
        text += "# The framework from source: a checkout next to this folder, built with\n";
        text += "# the bundled applications switched off (Docs/GettingStarted.md, step 4).\n";
        text += "set(BUILD_DEMO_APP OFF CACHE BOOL \"\" FORCE)\n";
        text += "add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../UltraCanvas/UltraCanvas UltraCanvas)\n\n";
        text += "add_executable(" + app + " main.cpp)\n";
        text += "target_include_directories(" + app + " PRIVATE ${ULTRACANVAS_INCLUDE_DIRS})\n";
        text += "target_link_libraries(" + app + " PRIVATE ${ULTRACANVAS_LIBRARY} ${ULTRACANVAS_PLUGIN_TARGETS})\n";
    }
    text += "set_target_properties(" + app + " PROPERTIES ENABLE_EXPORTS ON)\n";
    return text;
}

std::string ProjectMainCpp(const ProjectOptions& options) {
    const std::string app = IdentifierFrom(options.appName);
    std::string text;
    text += "// " + app + " - an UltraCanvas application. Start here.\n";
    text += "// Read Docs/GettingStarted.md and Docs/UltraCanvas/UltraCanvasUIElements.md\n";
    text += "// in the framework before adding elements: every widget has a page.\n";
    text += "#include \"UltraCanvasApplication.h\"\n";
    text += "#include \"UltraCanvasButton.h\"\n";
    text += "#include \"UltraCanvasLabel.h\"\n";
    text += "#include \"UltraCanvasWindow.h\"\n\n";
    text += "using namespace UltraCanvas;\n\n";
    text += "int main() {\n";
    text += "    UltraCanvasApplication app;\n";
    text += "    if (!app.Initialize(\"" + app + "\")) return 1;\n\n";
    text += "    WindowConfig config;\n";
    text += "    config.title  = \"" + options.appName + "\";\n";
    text += "    config.width  = 640;\n";
    text += "    config.height = 400;\n";
    text += "    // CreateWindow is the framework's function (UltraCanvasWindow.h);\n";
    text += "    // the application object has no such method.\n";
    text += "    auto window = CreateWindow(config);\n";
    text += "    if (!window) return 1;\n\n";
    text += "    auto label = CreateLabel(\"greeting\", 20, 20, 400, 28, \"Hello from " + app + "\");\n";
    text += "    window->AddChild(label);\n\n";
    text += "    auto button = CreateButton(\"quit\", 20, 60, 120, 32, \"Quit\");\n";
    text += "    // Capture the raw pointer, not a shared_ptr: a callback stored on a\n";
    text += "    // widget that owns a shared_ptr to it is a cycle (AGENTS.md).\n";
    text += "    button->onClick = [&app]() { app.RequestExit(); };\n";
    text += "    window->AddChild(button);\n\n";
    text += "    window->Show();\n";
    text += "    app.Run();\n";
    text += "    return 0;\n";
    text += "}\n";
    return text;
}

std::string ProjectPresets(const ProjectOptions& options) {
    std::string text;
    text += "{\n";
    text += "  \"version\": 3,\n";
    text += "  \"configurePresets\": [\n";
    text += "    {\n";
    text += "      \"name\": \"default\",\n";
    text += "      \"displayName\": \"Default\",\n";
    text += "      \"binaryDir\": \"${sourceDir}/build\",\n";
    text += "      \"cacheVariables\": {\n";
    text += "        \"CMAKE_BUILD_TYPE\": \"Debug\"";
    if (options.useSdk && !options.sdkPrefix.empty()) {
        text += ",\n        \"CMAKE_PREFIX_PATH\": " + JsonString(CMakePath(options.sdkPrefix));
    }
    text += "\n      }\n";
    text += "    }\n";
    text += "  ],\n";
    text += "  \"buildPresets\": [\n";
    text += "    { \"name\": \"default\", \"configurePreset\": \"default\" }\n";
    text += "  ]\n";
    text += "}\n";
    return text;
}

std::string ProjectAssistantMd(const ProjectOptions& options) {
    const std::string app = IdentifierFrom(options.appName);
    std::string text;
    text += "# " + AssistantInstructionFile(options.assistant) + "\n\n";
    text += app + " is an UltraCanvas application. The framework's conventions apply here:\n\n";
    text += "- Read the framework's `AGENTS.md` and `Docs/GettingStarted.md` first";
    if (options.useSdk) {
        text += " (the SDK's README is `Docs/UltraCanvasSDK.md`; the sources are at\n"
                "  https://github.com/ULTRA-OS-Project/UltraCanvas)";
    } else {
        text += " (the checkout is in `../UltraCanvas`)";
    }
    text += ".\n";
    text += "- Use the elements from `Docs/UltraCanvas/UltraCanvasUIElements.md`; consult a\n"
            "  widget's page before using it. Never paint a control by hand.\n";
    text += "- Paths are UTF-8: `PathToUtf8` / `PathFromUtf8` (`UltraCanvasPathUtf8.h`),\n"
            "  never `p.string()` or `fs::path(str)`.\n";
    text += "- Numbers written to files use a dot decimal, whatever the locale.\n";
    text += "- No Win32 A/W macro names as identifiers (`CreateFile`, `LoadImage`, `SendMessage`).\n";
    text += "- A callback stored on a widget captures the widget or its containers raw, not as\n"
            "  a `shared_ptr`.\n";
    text += "- Build: `cmake --preset default && cmake --build --preset default`.\n";
    return text;
}

std::string ProjectClaudeMd(const ProjectOptions& options) {
    ProjectOptions claude = options;
    claude.assistant = Assistant::ClaudeCode;
    return ProjectAssistantMd(claude);
}

std::string ProjectReadme(const ProjectOptions& options) {
    std::string text;
    text += "# " + options.appName + "\n\n";
    text += "An application built on the UltraCanvas framework, started with UltraCanvasStart.\n\n";
    text += "## Build\n\n```\ncmake --preset default\ncmake --build --preset default\n```\n\n";
    if (options.useSdk) {
        text += "The preset points `CMAKE_PREFIX_PATH` at the UltraCanvas SDK";
        if (!options.sdkPrefix.empty()) text += " in `" + options.sdkPrefix + "`";
        text += ". A newer SDK: UltraCanvasStart's Framework step downloads it and "
                "*Move a project...* points this project at it (on the command line: "
                "`UltraCanvasStart --update-sdk <folder>`, then "
                "`UltraCanvasStart --update-project <this folder> --sdk <its prefix>`).\n";
    } else {
        text += "`CMakeLists.txt` expects the framework checkout at `../UltraCanvas`.\n";
    }
    return text;
}

ProjectResult ScaffoldProject(const ProjectOptions& options, bool overwrite) {
    ProjectResult result;
    if (options.folder.empty()) {
        result.error = "No project folder was given.";
        return result;
    }
    std::error_code ec;
    const fs::path folder = UltraCanvas::PathFromUtf8(options.folder);
    fs::create_directories(folder, ec);
    if (ec) {
        result.error = "Could not create " + options.folder + ": " + ec.message();
        return result;
    }
    if (!overwrite && fs::exists(folder / "CMakeLists.txt", ec)) {
        result.error = options.folder + " already has a CMakeLists.txt; choose an empty folder.";
        return result;
    }

    struct File { const char* name; std::string content; };
    std::vector<File> files = {
        { "CMakeLists.txt",    ProjectCMakeLists(options) },
        { "main.cpp",          ProjectMainCpp(options) },
        { "CMakePresets.json", ProjectPresets(options) },
        { "README.md",         ProjectReadme(options) },
    };
    const std::string instructionFile = AssistantInstructionFile(options.assistant);
    if (options.withAiNotes) files.push_back({ instructionFile.c_str(), ProjectAssistantMd(options) });

    for (const auto& file : files) {
        const fs::path path = folder / file.name;
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            result.error = "Could not write " + UltraCanvas::PathToUtf8(path);
            return result;
        }
        out << file.content;
        result.written.push_back(UltraCanvas::PathToUtf8(path));
    }
    result.ok = true;
    return result;
}

bool RepointPresets(const std::string& presets, const std::string& sdkPrefix,
                    std::string& out, std::string& oldPrefix, std::string& error) {
    oldPrefix.clear();
    if (sdkPrefix.empty()) {
        error = "No SDK prefix to point the project at.";
        return false;
    }
    const std::string value = JsonString(CMakePath(sdkPrefix));
    const std::string key = "\"CMAKE_PREFIX_PATH\"";
    std::string text = presets;
    bool replaced = false;
    size_t from = 0;
    // Every preset that names a prefix gets the new one.
    for (size_t at = text.find(key, from); at != std::string::npos; at = text.find(key, from)) {
        size_t pos = SkipSpace(text, at + key.size());
        if (pos >= text.size() || text[pos] != ':') { from = at + key.size(); continue; }
        pos = SkipSpace(text, pos + 1);
        if (pos >= text.size() || text[pos] != '"') {
            error = "CMAKE_PREFIX_PATH in CMakePresets.json is not a plain string; "
                    "set it to " + CMakePath(sdkPrefix) + " by hand.";
            return false;
        }
        const size_t end = StringEnd(text, pos);
        if (end == std::string::npos) {
            error = "CMakePresets.json ends inside the CMAKE_PREFIX_PATH value.";
            return false;
        }
        if (!replaced) oldPrefix = JsonUnquote(text.substr(pos, end - pos));
        text.replace(pos, end - pos, value);
        from = pos + value.size();
        replaced = true;
    }
    if (!replaced) {
        // A project written without a prefix: add it to the first preset's
        // cache variables.
        const size_t at = text.find("\"cacheVariables\"");
        size_t pos = at == std::string::npos ? at : SkipSpace(text, at + 16);
        if (pos != std::string::npos && pos < text.size() && text[pos] == ':') pos = SkipSpace(text, pos + 1);
        if (pos == std::string::npos || pos >= text.size() || text[pos] != '{') {
            error = "CMakePresets.json has no cacheVariables to add CMAKE_PREFIX_PATH to; "
                    "add \"CMAKE_PREFIX_PATH\": " + value + " to its configure preset by hand.";
            return false;
        }
        const size_t next = SkipSpace(text, pos + 1);
        const bool empty = next < text.size() && text[next] == '}';
        text.insert(pos + 1, "\n        \"CMAKE_PREFIX_PATH\": " + value + (empty ? "\n      " : ","));
    }
    out = text;
    return true;
}

RepointResult RepointProjectSdk(const std::string& projectFolder, const std::string& sdkPrefix) {
    RepointResult result;
    std::error_code ec;
    const fs::path folder = UltraCanvas::PathFromUtf8(projectFolder);
    const fs::path presetsPath = folder / "CMakePresets.json";
    result.presets = UltraCanvas::PathToUtf8(presetsPath);
    if (projectFolder.empty() || !fs::is_regular_file(presetsPath, ec)) {
        result.error = "There is no CMakePresets.json in " + projectFolder +
                       ": choose the folder of a project UltraCanvasStart created.";
        return result;
    }
    std::string presets;
    {
        std::ifstream in(presetsPath, std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        presets = buffer.str();
    }
    // Absolute: a relative prefix in the presets would be read against the
    // build folder, not against where it was typed.
    std::string prefix = sdkPrefix;
    if (!prefix.empty()) {
        const fs::path absolute = fs::absolute(UltraCanvas::PathFromUtf8(prefix), ec);
        if (!ec) prefix = UltraCanvas::PathToUtf8(absolute.lexically_normal());
        while (prefix.size() > 1 && (prefix.back() == '/' || prefix.back() == '\\')) prefix.pop_back();
    }
    std::string rewritten;
    if (!RepointPresets(presets, prefix, rewritten, result.oldPrefix, result.error)) return result;
    if (rewritten != presets) {
        // Beside it first, then over it: a failed write leaves the old file.
        const fs::path part = folder / "CMakePresets.json.part";
        {
            std::ofstream out(part, std::ios::binary);
            out << rewritten;
            if (!out) {
                result.error = "Could not write " + UltraCanvas::PathToUtf8(part);
                fs::remove(part, ec);
                return result;
            }
        }
        fs::rename(part, presetsPath, ec);
        if (ec) {
            result.error = "Could not replace " + result.presets + ": " + ec.message();
            fs::remove(part, ec);
            return result;
        }
        result.changed = true;
    }
    // find_package caches where it found the old SDK (UltraCanvas_DIR); the
    // cache goes, the build folder's objects stay and are rebuilt as needed.
    const fs::path cache = folder / "build" / "CMakeCache.txt";
    if (fs::is_regular_file(cache, ec) && fs::remove(cache, ec)) result.cacheRemoved = true;
    result.ok = true;
    return result;
}

PlanStep CloneStep(const std::string& destination) {
    PlanStep step;
    step.kind = StepKind::Clone;
    step.title = "Clone the UltraCanvas repository";
    step.description = "git clone of https://github.com/ULTRA-OS-Project/UltraCanvas into " + destination;
    step.argv = { "git", "clone", "--depth", "1",
                  "https://github.com/ULTRA-OS-Project/UltraCanvas.git", destination };
    return step;
}

} // namespace UltraCanvasStart
