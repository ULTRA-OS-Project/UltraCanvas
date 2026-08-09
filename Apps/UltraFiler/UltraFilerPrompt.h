// Apps/UltraFiler/UltraFilerPrompt.h
// "Extras > Open prompt" support: launching the operating system's command
// line program (terminal) in a folder. The application started is the one
// configured in Settings > Extras > Open prompt; when that setting is empty
// the platform default is detected at run time (cmd.exe on Windows,
// Terminal.app on macOS, the first installed terminal emulator on Linux).
// Version: 1.0.0
// Last Modified: 2026-08-08
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>

namespace UltraCanvas {
namespace UltraFilerPrompt {

// Command or absolute path of the OS command line program used when no
// application is configured; "" when none could be found.
std::string DetectSystemPrompt();

// Starts `application` (empty = DetectSystemPrompt()) detached from
// UltraFiler, with `workingDirectory` as its working directory - so the shell
// opens in the folder the user is browsing. Returns false and fills
// `outError` with a message fit for an alert when nothing could be started.
bool Launch(const std::string& application, const std::string& workingDirectory,
            std::string& outError);

// File-dialog filter for picking an application: description plus the
// extensions to match (Windows executables / macOS bundles; on Linux
// executables carry no extension, so this is the all-files pattern "*").
struct ApplicationFilter {
    std::string              description;
    std::vector<std::string> extensions;
};
ApplicationFilter GetApplicationFilter();

// Folder the "choose an application" dialog starts in (the directory
// applications usually live in on this platform).
std::string GetApplicationsDirectory();

} // namespace UltraFilerPrompt
} // namespace UltraCanvas
