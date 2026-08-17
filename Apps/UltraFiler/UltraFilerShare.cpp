// Apps/UltraFiler/UltraFilerShare.cpp
// "Extras > Share" implementation: opens the platform's e-mail composer with
// the given files attached. Windows goes through Simple MAPI (the same
// mechanism as Explorer's "Send to > Mail recipient"), macOS hands the files
// to Mail.app, Linux uses xdg-email, which forwards to the desktop's default
// mail client.
// Version: 1.0.0
// Last Modified: 2026-08-17
// Author: UltraCanvas Framework

#include "UltraFilerShare.h"

#include <filesystem>
#include <system_error>

#if defined(_WIN32) || defined(_WIN64)
#include "UltraCanvasUtils.h"   // Utf8ToWide

#include <windows.h>
#else
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace UltraCanvas {
namespace UltraFilerShare {

namespace {

#if defined(_WIN32) || defined(_WIN64)

    // Simple MAPI, Unicode variant (Windows 8+). Declared locally because
    // MinGW's <mapi.h> only ships the ANSI structures, which would garble
    // non-ASCII file names.
    struct UcMapiFileDescW {
        ULONG  ulReserved = 0;
        ULONG  flFlags = 0;
        ULONG  nPosition = 0xFFFFFFFF;   // let the client place the attachment
        PWSTR  lpszPathName = nullptr;
        PWSTR  lpszFileName = nullptr;
        void*  lpFileType = nullptr;
    };

    struct UcMapiMessageW {
        ULONG  ulReserved = 0;
        PWSTR  lpszSubject = nullptr;
        PWSTR  lpszNoteText = nullptr;
        PWSTR  lpszMessageType = nullptr;
        PWSTR  lpszDateReceived = nullptr;
        PWSTR  lpszConversationID = nullptr;
        ULONG  flFlags = 0;
        void*  lpOriginator = nullptr;
        ULONG  nRecipCount = 0;
        void*  lpRecips = nullptr;
        ULONG  nFileCount = 0;
        UcMapiFileDescW* lpFiles = nullptr;
    };

    using MAPISendMailWFn = ULONG (WINAPI*)(LPVOID lhSession, ULONG_PTR ulUIParam,
                                            UcMapiMessageW* lpMessage,
                                            ULONG flFlags, ULONG ulReserved);

    constexpr ULONG kMapiLogonUI    = 0x00000001;   // MAPI_LOGON_UI
    constexpr ULONG kMapiDialog     = 0x00000008;   // MAPI_DIALOG
    constexpr ULONG kMapiUserAbort  = 1;            // MAPI_E_USER_ABORT

#else

    // Is `command` runnable? A bare command name is looked up along $PATH the
    // way execvp would (same logic as UltraFilerPrompt).
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

    // Starts `argv` fully detached (own session, intermediate child reaped),
    // so the mail client outlives UltraFiler. Same pattern as
    // UltraFilerPrompt's terminal launch.
    bool LaunchDetached(const std::vector<std::string>& argv, std::string& outError) {
        if (argv.empty()) return false;

        const pid_t pid = ::fork();
        if (pid < 0) {
            outError = std::string("Could not start the e-mail program: ") +
                       std::strerror(errno);
            return false;
        }
        if (pid == 0) {
            if (::fork() == 0) {
                ::setsid();
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

#endif  // platform

} // namespace

bool ShareByEmail(const std::vector<std::string>& paths, std::string& outError) {
    outError.clear();

    // Only existing regular files can be attached.
    std::vector<std::string> files;
    for (const std::string& p : paths) {
        std::error_code ec;
        if (fs::is_regular_file(p, ec) && !ec) files.push_back(p);
    }
    if (files.empty()) {
        outError = "There are no files to share - folders cannot be sent "
                   "as e-mail attachments.";
        return false;
    }

#if defined(_WIN32) || defined(_WIN64)
    HMODULE mapi = LoadLibraryW(L"mapi32.dll");
    if (!mapi) {
        outError = "No MAPI e-mail client is installed on this system.";
        return false;
    }
    auto sendMail = reinterpret_cast<MAPISendMailWFn>(
            GetProcAddress(mapi, "MAPISendMailW"));
    if (!sendMail) {
        FreeLibrary(mapi);
        outError = "The installed MAPI e-mail client does not support "
                   "Unicode mail (MAPISendMailW).";
        return false;
    }

    // Keep the wide strings alive for the duration of the call.
    std::vector<std::wstring> pathsW, namesW;
    pathsW.reserve(files.size());
    namesW.reserve(files.size());
    for (const std::string& f : files) {
        pathsW.push_back(Utf8ToWide(f));
        namesW.push_back(Utf8ToWide(fs::path(f).filename().string()));
    }
    std::vector<UcMapiFileDescW> descs(files.size());
    for (size_t i = 0; i < files.size(); ++i) {
        descs[i].lpszPathName = pathsW[i].data();
        descs[i].lpszFileName = namesW[i].data();
    }

    UcMapiMessageW message;
    message.nFileCount = static_cast<ULONG>(descs.size());
    message.lpFiles = descs.data();

    const ULONG rc = sendMail(nullptr, 0, &message, kMapiDialog | kMapiLogonUI, 0);
    FreeLibrary(mapi);

    // The user closing the compose window unsent is not an error.
    if (rc == 0 || rc == kMapiUserAbort) return true;
    outError = "The e-mail client could not be opened (MAPI error " +
               std::to_string(rc) + ").";
    return false;

#elif defined(__APPLE__)
    // Mail.app creates a new message with the handed files attached.
    std::vector<std::string> argv = {"open", "-a", "Mail"};
    for (const std::string& f : files) argv.push_back(f);
    return LaunchDetached(argv, outError);

#else
    // xdg-email forwards to the desktop's configured mail client.
    if (!IsExecutable("xdg-email")) {
        outError = "xdg-email was not found - install xdg-utils (or a desktop "
                   "environment) to share files by e-mail.";
        return false;
    }
    std::vector<std::string> argv = {"xdg-email"};
    for (const std::string& f : files) {
        argv.push_back("--attach");
        argv.push_back(f);
    }
    return LaunchDetached(argv, outError);
#endif
}

} // namespace UltraFilerShare
} // namespace UltraCanvas
