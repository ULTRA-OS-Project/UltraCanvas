// Tests/UltraCanvasStart/test_project.cpp
// The project scaffold and the assistant texts, over a temporary folder.
// Version: 0.2.0 - RepointPresets / RepointProjectSdk, SdkPrefixVersion
// Version: 0.1.2 - the skeleton is compiled against the SDK ULTRACANVAS_SDK_PREFIX names
// Version: 0.1.1 - main.cpp uses the framework's CreateWindow
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "StartAi.h"
#include "StartBuild.h"
#include "StartProject.h"
#include "StartSdk.h"
#include "StartSystem.h"

#include "UltraCanvasPathUtf8.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace UltraCanvasStart;
namespace fs = std::filesystem;

namespace {
std::string ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}
} // namespace

TEST(IdentifierFrom_makes_a_cpp_name) {
    REQUIRE_EQ(IdentifierFrom("My App!"), std::string("MyApp"));
    REQUIRE_EQ(IdentifierFrom("3d-viewer"), std::string("A3dviewer"));
    REQUIRE_EQ(IdentifierFrom(""), std::string("MyApp"));
}

TEST(ScaffoldProject_writes_the_files_and_refuses_to_overwrite) {
    // A name outside ASCII, since every path goes through PathFromUtf8.
    const fs::path folder = fs::temp_directory_path() / UltraCanvas::PathFromUtf8("UltraCanvasStart-Прóба");
    std::error_code ec;
    fs::remove_all(folder, ec);

    ProjectOptions options;
    options.folder = UltraCanvas::PathToUtf8(folder);
    options.appName = "Demo App";
    options.useSdk = true;
    options.sdkPrefix = "/opt/UltraCanvas-SDK-Linux-0.9.147-x86_64";
    const ProjectResult result = ScaffoldProject(options);
    REQUIRE(result.ok);
    REQUIRE_EQ(result.written.size(), size_t(5));
    REQUIRE(fs::is_regular_file(folder / "CMakeLists.txt"));
    REQUIRE(fs::is_regular_file(folder / "CLAUDE.md"));

    const std::string cmake = ReadAll(folder / "CMakeLists.txt");
    REQUIRE(cmake.find("project(DemoApp") != std::string::npos);
    REQUIRE(cmake.find("find_package(UltraCanvas CONFIG REQUIRED)") != std::string::npos);
    REQUIRE(cmake.find("UltraCanvas::UltraCanvasAllFormats") != std::string::npos);
    const std::string presets = ReadAll(folder / "CMakePresets.json");
    REQUIRE(presets.find("\"CMAKE_PREFIX_PATH\": \"/opt/UltraCanvas-SDK-Linux-0.9.147-x86_64\"") != std::string::npos);
    const std::string main = ReadAll(folder / "main.cpp");
    REQUIRE(main.find("app.Initialize(\"DemoApp\")") != std::string::npos);
    REQUIRE(main.find("config.title  = \"Demo App\"") != std::string::npos);
    // The window comes from the framework's CreateWindow(config): the
    // application class has no CreateWindow, and 0.3.0's skeleton did not
    // compile because it called one. CI builds the skeleton against the SDK
    // (--create in build.yml), this only pins the line.
    REQUIRE(main.find("    auto window = CreateWindow(config);") != std::string::npos);
    REQUIRE(main.find("app.CreateWindow") == std::string::npos);

    const ProjectResult again = ScaffoldProject(options);
    REQUIRE(!again.ok);
    REQUIRE(again.error.find("already has a CMakeLists.txt") != std::string::npos);
    REQUIRE(ScaffoldProject(options, /*overwrite=*/true).ok);

    fs::remove_all(folder, ec);
}

TEST(Scaffold_from_source_uses_add_subdirectory) {
    ProjectOptions options;
    options.useSdk = false;
    options.appName = "Src";
    const std::string cmake = ProjectCMakeLists(options);
    REQUIRE(cmake.find("add_subdirectory(") != std::string::npos);
    REQUIRE(cmake.find("find_package(") == std::string::npos);
    REQUIRE(ProjectPresets(options).find("CMAKE_PREFIX_PATH") == std::string::npos);
}

TEST(FindSdkPrefix_finds_the_config_file) {
    const fs::path root = fs::temp_directory_path() / "UltraCanvasStart-sdk-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path prefix = root / "UltraCanvas-SDK-Linux-0.9.147-x86_64";
    fs::create_directories(prefix / "lib" / "cmake" / "UltraCanvas", ec);
    std::ofstream(prefix / "lib" / "cmake" / "UltraCanvas" / "UltraCanvasConfig.cmake") << "# test\n";
    REQUIRE_EQ(FindSdkPrefix(UltraCanvas::PathToUtf8(root)), UltraCanvas::PathToUtf8(prefix));
    REQUIRE_EQ(FindSdkPrefix(UltraCanvas::PathToUtf8(prefix)), UltraCanvas::PathToUtf8(prefix));
    REQUIRE(FindSdkPrefix(UltraCanvas::PathToUtf8(root / "nowhere")).empty());
    fs::remove_all(root, ec);
}

TEST(FirstPrompt_names_the_app_and_the_workflow) {
    Choices choices;
    choices.appName = "Demo App";
    choices.useSdk = true;
    choices.cloudOnly = true;
    const std::string prompt = FirstPrompt(choices);
    REQUIRE(prompt.find("DemoApp") != std::string::npos);
    REQUIRE(prompt.find("UltraCanvasSDK.md") != std::string::npos);
    REQUIRE(prompt.find("draft pull request") != std::string::npos);
    choices.cloudOnly = false;
    choices.useSdk = false;
    const std::string local = FirstPrompt(choices);
    REQUIRE(local.find("../UltraCanvas") != std::string::npos);
    REQUIRE(local.find("pull request") == std::string::npos);
    REQUIRE(!CloudChecklist().empty());
    REQUIRE(!ClaudeInstallInstructions(Platform::Windows).empty());
    choices.assistant = Assistant::Codex;
    REQUIRE(FirstPrompt(choices).rfind("Read AGENTS.md, then", 0) == 0);
}

TEST(Scaffold_writes_the_chosen_assistants_file) {
    ProjectOptions options;
    options.assistant = Assistant::Gemini;
    const std::string text = ProjectAssistantMd(options);
    REQUIRE(text.rfind("# GEMINI.md", 0) == 0);
    REQUIRE(text.find("AGENTS.md") != std::string::npos);
    REQUIRE(ProjectClaudeMd(options).rfind("# CLAUDE.md", 0) == 0);
}

TEST(BuildCommands_are_the_programmers_two_commands) {
    const std::string text = BuildCommandsText("/tmp/My App");
    REQUIRE(text.find("cd \"/tmp/My App\"\n") != std::string::npos);
    REQUIRE(text.find("cmake --preset default\n") != std::string::npos);
    REQUIRE(text.find("cmake --build --preset default\n") != std::string::npos);
    const SystemProfile profile = DetectSystem();
    const auto commands = BuildCommands("/tmp/My App", profile);
    // Without cmake there is nothing to run; with it, the configure of the
    // default preset in the folder, then the build of the preset's build dir.
    if (!commands.empty()) {
        REQUIRE_EQ(commands.size(), size_t(2));
        REQUIRE(commands[0].size() == 5 && commands[0][1] == "-S" && commands[0][2] == "/tmp/My App" &&
                commands[0][3] == "--preset" && commands[0][4] == "default");
        REQUIRE(commands[1].size() == 5 && commands[1][1] == "--build" &&
                commands[1][2] == "/tmp/My App/build" && commands[1][3] == "--parallel");
    }
    REQUIRE(!BuildProject("", profile).started);
}

// The skeleton compiled against a real SDK: the guard that catches a
// skeleton which no longer builds before CI's SDK step does. It needs an
// installed SDK, named by ULTRACANVAS_SDK_PREFIX (a cmake --install prefix
// or an unpacked SDK archive); without the variable the test is skipped.
TEST(BuildProject_compiles_the_skeleton_against_the_sdk) {
    const char* prefix = std::getenv("ULTRACANVAS_SDK_PREFIX");
    if (!prefix || !*prefix) {
        std::printf("    (skipped: ULTRACANVAS_SDK_PREFIX is not set)\n");
        return;
    }
    const fs::path folder = fs::temp_directory_path() / UltraCanvas::PathFromUtf8("UltraCanvasStart-build-check");
    std::error_code ec;
    fs::remove_all(folder, ec);

    ProjectOptions options;
    options.folder = UltraCanvas::PathToUtf8(folder);
    options.appName = "Build Check";
    options.useSdk = true;
    options.sdkPrefix = prefix;
    REQUIRE(ScaffoldProject(options).ok);

    const SystemProfile profile = DetectSystem();
    const BuildResult result = BuildProject(options.folder, profile);
    if (!result.Succeeded()) std::printf("%s\n", (result.output + result.error).c_str());
    REQUIRE(result.started);
    REQUIRE(result.configured);
    REQUIRE(result.built);
    REQUIRE(fs::is_directory(folder / "build"));
    fs::remove_all(folder, ec);
}

TEST(RepointPresets_replaces_the_prefix_and_keeps_the_rest) {
    ProjectOptions options;
    options.sdkPrefix = "/opt/UltraCanvas-SDK-Linux-0.9.147-x86_64";
    const std::string before = ProjectPresets(options);
    std::string after, old, error;
    REQUIRE(RepointPresets(before, "C:\\SDKs\\UltraCanvas-SDK-Windows-0.9.604-x86_64", after, old, error));
    REQUIRE_EQ(old, std::string("/opt/UltraCanvas-SDK-Linux-0.9.147-x86_64"));
    // Forward slashes, as CMake wants them on Windows too.
    REQUIRE(after.find("\"CMAKE_PREFIX_PATH\": \"C:/SDKs/UltraCanvas-SDK-Windows-0.9.604-x86_64\"") != std::string::npos);
    REQUIRE(after.find("0.9.147") == std::string::npos);
    REQUIRE(after.find("\"CMAKE_BUILD_TYPE\": \"Debug\"") != std::string::npos);
    REQUIRE(after.find("\"buildPresets\"") != std::string::npos);
    // Pointed at the prefix it names already: the same text.
    std::string again;
    REQUIRE(RepointPresets(after, "C:/SDKs/UltraCanvas-SDK-Windows-0.9.604-x86_64", again, old, error));
    REQUIRE_EQ(again, after);
}

TEST(RepointPresets_adds_the_prefix_where_none_was) {
    ProjectOptions options;   // no prefix: the user was to set it later
    const std::string before = ProjectPresets(options);
    REQUIRE(before.find("CMAKE_PREFIX_PATH") == std::string::npos);
    std::string after, old, error;
    REQUIRE(RepointPresets(before, "/sdk/a \"quoted\" name", after, old, error));
    REQUIRE(old.empty());
    REQUIRE(after.find("\"CMAKE_PREFIX_PATH\": \"/sdk/a \\\"quoted\\\" name\",") != std::string::npos);
    REQUIRE(after.find("\"CMAKE_BUILD_TYPE\": \"Debug\"") != std::string::npos);

    // An empty cacheVariables object gets no trailing comma.
    const std::string empty = "{ \"configurePresets\": [ { \"name\": \"default\", \"cacheVariables\": {} } ] }";
    REQUIRE(RepointPresets(empty, "/sdk", after, old, error));
    REQUIRE(after.find("\"CMAKE_PREFIX_PATH\": \"/sdk\"\n") != std::string::npos);
    REQUIRE(after.find("\"/sdk\",") == std::string::npos);
}

TEST(RepointPresets_refuses_what_it_cannot_edit) {
    std::string after, old, error;
    REQUIRE(!RepointPresets("{ \"cacheVariables\": {} }", "", after, old, error));
    REQUIRE(!error.empty());
    error.clear();
    REQUIRE(!RepointPresets("{ \"cacheVariables\": { \"CMAKE_PREFIX_PATH\": { \"type\": \"PATH\", \"value\": \"/x\" } } }",
                            "/sdk", after, old, error));
    REQUIRE(error.find("not a plain string") != std::string::npos);
    error.clear();
    REQUIRE(!RepointPresets("{ \"version\": 3 }", "/sdk", after, old, error));
    REQUIRE(error.find("no cacheVariables") != std::string::npos);
}

TEST(RepointProjectSdk_rewrites_the_presets_and_drops_the_cache) {
    const fs::path folder = fs::temp_directory_path() / UltraCanvas::PathFromUtf8("UltraCanvasStart-repoint-Ünï");
    std::error_code ec;
    fs::remove_all(folder, ec);

    REQUIRE(!RepointProjectSdk(UltraCanvas::PathToUtf8(folder), "/sdk").ok);   // no project there

    ProjectOptions options;
    options.folder = UltraCanvas::PathToUtf8(folder);
    options.sdkPrefix = "/opt/UltraCanvas-SDK-Linux-0.9.147-x86_64";
    REQUIRE(ScaffoldProject(options).ok);
    fs::create_directories(folder / "build", ec);
    std::ofstream(folder / "build" / "CMakeCache.txt") << "UltraCanvas_DIR:PATH=/opt/old\n";
    std::ofstream(folder / "build" / "main.o") << "object\n";

    const RepointResult moved = RepointProjectSdk(options.folder, "/opt/UltraCanvas-SDK-Linux-0.9.604-x86_64");
    REQUIRE(moved.ok);
    REQUIRE(moved.changed);
    REQUIRE(moved.cacheRemoved);
    REQUIRE_EQ(moved.oldPrefix, std::string("/opt/UltraCanvas-SDK-Linux-0.9.147-x86_64"));
    REQUIRE(ReadAll(folder / "CMakePresets.json").find("UltraCanvas-SDK-Linux-0.9.604-x86_64") != std::string::npos);
    REQUIRE(!fs::exists(folder / "build" / "CMakeCache.txt"));
    REQUIRE(fs::exists(folder / "build" / "main.o"));          // only the cache goes
    REQUIRE(!fs::exists(folder / "CMakePresets.json.part"));

    const RepointResult same = RepointProjectSdk(options.folder, "/opt/UltraCanvas-SDK-Linux-0.9.604-x86_64");
    REQUIRE(same.ok);
    REQUIRE(!same.changed);
    REQUIRE(!same.cacheRemoved);

    // A relative prefix goes in absolute, without a trailing slash.
    const RepointResult relative = RepointProjectSdk(options.folder, "relative/UltraCanvas-SDK-Linux-0.9.605-x86_64/");
    REQUIRE(relative.ok);
    std::string expected = UltraCanvas::PathToUtf8(
        (fs::current_path() / "relative" / "UltraCanvas-SDK-Linux-0.9.605-x86_64").lexically_normal());
    for (char& c : expected) if (c == '\\') c = '/';   // the presets' forward slashes
    REQUIRE(ReadAll(folder / "CMakePresets.json").find("\"" + expected + "\"") != std::string::npos);

    fs::remove_all(folder, ec);
}

TEST(SdkPrefixVersion_reads_the_package_version) {
    const fs::path root = fs::temp_directory_path() / "UltraCanvasStart-sdk-version-test";
    std::error_code ec;
    fs::remove_all(root, ec);

    // The ConfigVersion file write_basic_package_version_file writes wins
    // over the folder's name.
    const fs::path prefix = root / "UltraCanvas-SDK-Linux-0.9.147-x86_64";
    fs::create_directories(prefix / "lib" / "cmake" / "UltraCanvas", ec);
    std::ofstream(prefix / "lib" / "cmake" / "UltraCanvas" / "UltraCanvasConfigVersion.cmake")
        << "# This is a basic version file for the Config-mode of find_package().\n"
           "set(PACKAGE_VERSION \"0.9.604\")\n"
           "if(PACKAGE_FIND_VERSION_RANGE)\n";
    REQUIRE_EQ(SdkPrefixVersion(UltraCanvas::PathToUtf8(prefix)), std::string("0.9.604"));

    // Without it, the folder's name; with a trailing slash too.
    const fs::path bare = root / "UltraCanvas-SDK-MacOS-0.9.601-arm64";
    fs::create_directories(bare, ec);
    REQUIRE_EQ(SdkPrefixVersion(UltraCanvas::PathToUtf8(bare)), std::string("0.9.601"));
    REQUIRE_EQ(SdkPrefixVersion(UltraCanvas::PathToUtf8(bare) + "/"), std::string("0.9.601"));

    // Neither: empty.
    REQUIRE(SdkPrefixVersion(UltraCanvas::PathToUtf8(root)).empty());
    REQUIRE(SdkPrefixVersion("").empty());
    REQUIRE_EQ(ParsePackageVersion("set( PACKAGE_VERSION   \"1.2.3\" )"), std::string("1.2.3"));
    REQUIRE(ParsePackageVersion("set(PACKAGE_VERSION_EXACT TRUE)").empty());

    fs::remove_all(root, ec);
}
