// Apps/UltraCanvasStart/engine/StartProject.h
// Writes the skeleton of a new application: a CMakeLists.txt that finds the
// framework (SDK prefix or a sibling checkout), a main.cpp with one window,
// CMakePresets.json pointing at the prefix, a CLAUDE.md for the assistant and
// a README. The files are the ones Docs/GettingStarted.md walks through.
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
    bool withAiNotes = true;    // write CLAUDE.md
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
std::string ProjectClaudeMd(const ProjectOptions& options);
std::string ProjectReadme(const ProjectOptions& options);

// Writes the files. Refuses to overwrite a folder that already has a
// CMakeLists.txt unless `overwrite` is true.
ProjectResult ScaffoldProject(const ProjectOptions& options, bool overwrite = false);

// The step that clones the framework repository into `destination` (UTF-8).
PlanStep CloneStep(const std::string& destination);

// A C++ identifier from an application name: "My App!" -> "MyApp".
std::string IdentifierFrom(const std::string& name);

} // namespace UltraCanvasStart
