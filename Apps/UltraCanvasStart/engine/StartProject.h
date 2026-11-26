// Apps/UltraCanvasStart/engine/StartProject.h
// Writes the skeleton of a new application: a CMakeLists.txt that finds the
// framework (SDK prefix or a sibling checkout), a main.cpp with one window,
// CMakePresets.json pointing at the prefix, the assistant's instruction file and
// a README. The files are the ones Docs/GettingStarted.md walks through.
// Version: 0.2.0 - RepointPresets / RepointProjectSdk: move a project to another SDK
// Version: 0.1.1 - the instruction file is the chosen assistant's (CLAUDE.md, AGENTS.md, GEMINI.md)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartTypes.h"

#include <string>
#include <vector>

namespace UltraCanvasStart {

struct ProjectOptions {
    std::string folder;         // UTF-8; created if missing
    std::string appName = "MyApp";
    bool useSdk = true;         // find_package(UltraCanvas) against sdkPrefix
    std::string sdkPrefix;      // UTF-8; may be empty (the user sets CMAKE_PREFIX_PATH later)
    bool withAiNotes = true;    // write the assistant's instruction file
    Assistant assistant = Assistant::ClaudeCode;   // which file: CLAUDE.md, AGENTS.md, GEMINI.md
};

struct ProjectResult {
    bool ok = false;
    std::vector<std::string> written;   // UTF-8 paths
    std::string error;
};

// The file contents, exposed so the tests and the GUI preview can show them.
std::string ProjectCMakeLists(const ProjectOptions& options);
std::string ProjectMainCpp(const ProjectOptions& options);
std::string ProjectPresets(const ProjectOptions& options);
// The assistant's instruction file (AssistantInstructionFile(options.assistant)).
std::string ProjectAssistantMd(const ProjectOptions& options);
// The same, for Claude Code (kept for the callers that say so).
std::string ProjectClaudeMd(const ProjectOptions& options);
std::string ProjectReadme(const ProjectOptions& options);

// Writes the files. Refuses to overwrite a folder that already has a
// CMakeLists.txt unless `overwrite` is true.
ProjectResult ScaffoldProject(const ProjectOptions& options, bool overwrite = false);

// ===== MOVING A PROJECT TO ANOTHER SDK =====
// `presets` (a CMakePresets.json) with every "CMAKE_PREFIX_PATH" string set
// to `sdkPrefix` (forward slashes), or, when it names none, the key added to
// the first "cacheVariables". The rest of the file is kept as it is. False,
// with `error` saying why, for an empty prefix, a prefix that is not a
// string, or a file with no cacheVariables; `oldPrefix` is the first value
// it replaced, or empty.
bool RepointPresets(const std::string& presets, const std::string& sdkPrefix,
                    std::string& out, std::string& oldPrefix, std::string& error);

struct RepointResult {
    bool ok = false;
    std::string presets;         // the CMakePresets.json (UTF-8)
    std::string oldPrefix;       // the SDK it named before, or empty
    bool changed = false;        // false when it named this SDK already
    bool cacheRemoved = false;   // build/CMakeCache.txt was removed
    std::string error;
};

// Points the project in `projectFolder` at the SDK in `sdkPrefix` (both
// UTF-8; a relative prefix is made absolute first): RepointPresets over its CMakePresets.json, written beside it and
// renamed over it, then build/CMakeCache.txt removed, because find_package
// keeps the old SDK's folder in UltraCanvas_DIR. The next
// `cmake --preset default` configures against the new SDK.
RepointResult RepointProjectSdk(const std::string& projectFolder, const std::string& sdkPrefix);

// The step that clones the framework repository into `destination` (UTF-8).
PlanStep CloneStep(const std::string& destination);

// A C++ identifier from an application name: "My App!" -> "MyApp".
std::string IdentifierFrom(const std::string& name);

} // namespace UltraCanvasStart
