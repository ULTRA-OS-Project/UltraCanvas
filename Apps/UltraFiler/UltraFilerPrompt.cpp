// Apps/UltraFiler/UltraFilerPrompt.cpp
// Launches the operating system's command line program for
// "Extras > Open prompt". The started process is detached from UltraFiler
// (its own session on POSIX, ShellExecuteEx on Windows) so closing the file
// manager never takes the terminal down with it, and it starts in the folder
// the active tab is showing.
// Version: 1.0.0
// Last Modified: 2026-08-08
// Author: UltraCanvas Framework

#include "UltraFilerPrompt.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#include "UltraCanvasUtils.h"   // Utf8ToWide

#include <windows.h>
#include <shellapi.h>
#else
#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace UltraCanvas {
namespace UltraFilerPrompt {

namespace {

#if !defined(_WIN32) && !defined(_WIN64)

    // Is `command` runnable? A path (anything containing a separator) is
    // checked directly, a bare command name is looked up along $PATH the way
    // execvp would.
    bool IsExecutable(const std::string& command) {
        if (command.empty()) return false;
        if (command.find('/') != std::string::npos)
            return ::access(command.c_str(), X_OK) == 0;

        const char* pathEnv = std::getenv("PATH");
        if (!pathEnv) return false;
        const std::string path(pathEnv);
        size_t start = 0;
        while (start <= path.size()) {
            const size_t colon = path.find(':', start);
            const std::string dir = path.substr(
                    start, colon == std::string::npos ? std::string::npos : colon - start);
            if (!dir.empty() && ::access((dir + "/" + command).c_str(), X_OK) == 0)
                return true;
            if (colon == std::string::npos) break;
            start = colon + 1;
        }
        return false;
    }

    // Starts `argv` in `workingDirectory`, fully detached: the intermediate
    // child is reaped right away and the grandchild that execs gets its own
    // session, so UltraFiler leaves no zombie behind and the terminal
    // survives the file manager.
    bool LaunchDetached(const std::vector<std::string>& argv,
                        const std::string& workingDirectory,
                        std::string& outError) {
        if (argv.empty()) return false;

        const pid_t pid = ::fork();
        if (pid < 0) {
            outError = std::string("Could not start the command line program: ") +
                       std::strerror(errno);
            return false;
        }
        if (pid == 0) {
            if (::fork() == 0) {
                ::setsid();
                if (!workingDirectory.empty()) {
                    if (::chdir(workingDirectory.c_str()) != 0) {
                        // Fall back to the inherited directory - a terminal in
                        // the wrong folder still beats no terminal at all.
                    }
                }
                std::vector<char*> args;
                args.reserve(argv.size() + 1);
                for (const std::string& a : argv)
                    args.push_back(const_cast<char*>(a.c_str()));
                args.push_back(nullptr);
                ::execvp(args[0], args.data());
                ::_exit(127);
            }
            ::_exit(0);
        }
        int status = 0;
        ::waitpid(pid, &status, 0);
        return true;
    }

#endif  // !_WIN32

#if defined(__APPLE__)
    bool IsAppBundle(const std::string& path) {
        return path.size() >= 4 &&
               path.compare(path.size() - 4, 4, ".app") == 0;
    }
#endif

    bool DirectoryExists(const std::string& path) {
        std::error_code ec;
        return !path.empty() && fs::is_directory(path, ec) && !ec;
    }

} // namespace

// ===== PLATFORM DEFAULT =====

std::string DetectSystemPrompt() {
#if defined(_WIN32) || defined(_WIN64)
    // The Windows command processor; %COMSPEC% points at it on every install.
    const char* comspec = std::getenv("COMSPEC");
    return comspec ? std::string(comspec) : std::string("cmd.exe");
#elif defined(__APPLE__)
    static const char* const kTerminals[] = {
            "/System/Applications/Utilities/Terminal.app",
            "/Applications/Utilities/Terminal.app",
    };
    for (const char* t : kTerminals)
        if (DirectoryExists(t)) return t;
    return {};
#else
    // Honour the user's preference first, then the Debian alternative, then
    // the terminals shipped by the common desktops.
    if (const char* preferred = std::getenv("TERMINAL"))
        if (IsExecutable(preferred)) return preferred;

    static const char* const kTerminals[] = {
            "x-terminal-emulator", "gnome-terminal", "konsole", "xfce4-terminal",
            "mate-terminal", "lxterminal", "tilix", "terminator", "kitty",
            "alacritty", "wezterm", "foot", "urxvt", "xterm",
    };
    for (const char* t : kTerminals)
        if (IsExecutable(t)) return t;
    return {};
#endif
}

// ===== LAUNCH =====

bool Launch(const std::string& application, const std::string& workingDirectory,
            std::string& outError) {
    outError.clear();

    std::string app = application;
    const bool configured = !app.empty();
    if (app.empty()) app = DetectSystemPrompt();
    if (app.empty()) {
        outError = "No command line program was found on this system.\n"
                   "Set one in Settings > Extras > Open prompt.";
        return false;
    }

    const std::string dir = DirectoryExists(workingDirectory) ? workingDirectory
                                                              : std::string();

#if defined(_WIN32) || defined(_WIN64)
    const std::wstring fileW = Utf8ToWide(app);
    const std::wstring dirW  = Utf8ToWide(dir);

    SHELLEXECUTEINFOW info = {};
    info.cbSize      = sizeof(info);
    info.fMask       = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb      = L"open";
    info.lpFile      = fileW.c_str();
    info.lpDirectory = dirW.empty() ? nullptr : dirW.c_str();
    info.nShow       = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info)) return true;

    outError = "Could not start \"" + app + "\".";
    if (configured) outError += "\nCheck the path in Settings > Extras > Open prompt.";
    return false;
#else
    std::vector<std::string> argv;
#if defined(__APPLE__)
    if (IsAppBundle(app)) {
        // `open -a <bundle> <folder>` hands the folder to the application,
        // which is how Terminal.app is told where to start.
        if (!DirectoryExists(app)) {
            outError = "The application \"" + app + "\" was not found.";
            if (configured) outError += "\nCheck the path in Settings > Extras > Open prompt.";
            return false;
        }
        argv = {"open", "-a", app};
        if (!dir.empty()) argv.push_back(dir);
        return LaunchDetached(argv, dir, outError);
    }
#endif
    if (!IsExecutable(app)) {
        outError = "The application \"" + app + "\" was not found or is not executable.";
        if (configured) outError += "\nCheck the path in Settings > Extras > Open prompt.";
        return false;
    }
    argv = {app};
    return LaunchDetached(argv, dir, outError);
#endif
}

// ===== FILE DIALOG HELPERS =====

ApplicationFilter GetApplicationFilter() {
#if defined(_WIN32) || defined(_WIN64)
    return {"Applications", {"exe", "com", "bat", "cmd"}};
#elif defined(__APPLE__)
    return {"Applications", {"app"}};
#else
    // Linux executables carry no extension - everything has to stay visible.
    return {"Applications", {"*"}};
#endif
}

std::string GetApplicationsDirectory() {
#if defined(_WIN32) || defined(_WIN64)
    const char* systemRoot = std::getenv("SystemRoot");
    const std::string system32 =
            systemRoot ? std::string(systemRoot) + "\\System32" : std::string();
    return DirectoryExists(system32) ? system32 : std::string();
#elif defined(__APPLE__)
    if (DirectoryExists("/Applications/Utilities")) return "/Applications/Utilities";
    return DirectoryExists("/Applications") ? "/Applications" : std::string();
#else
    return DirectoryExists("/usr/bin") ? "/usr/bin" : std::string();
#endif
}

} // namespace UltraFilerPrompt
} // namespace UltraCanvas
