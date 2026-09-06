// OS/MSWindows/UltraCanvasWindowsFileLock.cpp
// Windows backend for UltraCanvasFileLock. Windows is the platform where the
// question actually bites: a handle another program holds without
// FILE_SHARE_WRITE / FILE_SHARE_DELETE makes the file impossible to overwrite,
// rename or delete until that program lets go - which is what Explorer means
// by "the file is open in another program", and what a running .exe or .dll
// does to its own file for as long as it runs.
//
// The probe asks for exactly the two accesses a replace needs and grants
// every sharing flag itself, so it answers the question without ever becoming
// the answer for somebody else. Nothing is written: no DELETE_ON_CLOSE, no
// disposition, no truncation - the handle is opened and closed.
//
// Who holds it is asked separately, through the Restart Manager (the same
// service the Windows installer uses to say "close Firefox first"). It costs
// a session per file, so it is only done when a caller asks for holders.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraCanvasFileLock.h"
#include "UltraCanvasUtils.h"   // Utf8ToWide / WideToUtf8

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#if __has_include(<restartmanager.h>)
#include <restartmanager.h>
#define ULTRACANVAS_HAS_RESTART_MANAGER 1
#endif

#include <string>

namespace UltraCanvas {

    namespace {

        constexpr DWORD kShareAll =
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

        // True when the failure says "somebody else has it", rather than "you
        // may not". A read-only file and a file an administrator locked out of
        // this account both fail with ACCESS_DENIED, and neither is another
        // program holding the file - reporting those as "in use" would put a
        // badge on every read-only file in a folder.
        bool IsSharingFailure(DWORD err) {
            return err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION;
        }

        // Opens for `access` with everything shared, and reports what the
        // system said. The handle is closed immediately; on success nothing
        // about the file has changed.
        bool CanOpen(const std::wstring& path, DWORD access, DWORD& err) {
            err = ERROR_SUCCESS;
            HANDLE h = ::CreateFileW(path.c_str(), access, kShareAll, nullptr,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                ::CloseHandle(h);
                return true;
            }
            err = ::GetLastError();
            return false;
        }

#ifdef ULTRACANVAS_HAS_RESTART_MANAGER
        // Names the programs holding `path`, via the Restart Manager. Loaded
        // at call time: nothing links against rstrtmgr, and a system without
        // it simply produces no names.
        void CollectHolders(const std::wstring& path, std::vector<std::string>& out) {
            using StartSessionFn = DWORD (WINAPI*)(DWORD*, DWORD, WCHAR*);
            using RegisterFn = DWORD (WINAPI*)(DWORD, UINT, LPCWSTR*, UINT,
                                               RM_UNIQUE_PROCESS*, UINT, LPCWSTR*);
            using GetListFn = DWORD (WINAPI*)(DWORD, UINT*, UINT*,
                                              RM_PROCESS_INFO*, LPDWORD);
            using EndSessionFn = DWORD (WINAPI*)(DWORD);

            HMODULE dll = ::LoadLibraryW(L"rstrtmgr.dll");
            if (!dll) return;

            auto startSession = reinterpret_cast<StartSessionFn>(
                    reinterpret_cast<void*>(::GetProcAddress(dll, "RmStartSession")));
            auto registerRes = reinterpret_cast<RegisterFn>(
                    reinterpret_cast<void*>(::GetProcAddress(dll, "RmRegisterResources")));
            auto getList = reinterpret_cast<GetListFn>(
                    reinterpret_cast<void*>(::GetProcAddress(dll, "RmGetList")));
            auto endSession = reinterpret_cast<EndSessionFn>(
                    reinterpret_cast<void*>(::GetProcAddress(dll, "RmEndSession")));
            if (!startSession || !registerRes || !getList || !endSession) {
                ::FreeLibrary(dll);
                return;
            }

            DWORD session = 0;
            WCHAR key[CCH_RM_SESSION_KEY + 1] = {};
            if (startSession(&session, 0, key) != ERROR_SUCCESS) {
                ::FreeLibrary(dll);
                return;
            }

            LPCWSTR files[1] = {path.c_str()};
            if (registerRes(session, 1, files, 0, nullptr, 0, nullptr) == ERROR_SUCCESS) {
                UINT needed = 0, count = 0;
                DWORD reason = 0;
                // First call sizes the list, second fills it. One more slot
                // than asked for absorbs a process appearing in between.
                DWORD rc = getList(session, &needed, &count, nullptr, &reason);
                if (rc == ERROR_MORE_DATA && needed > 0) {
                    std::vector<RM_PROCESS_INFO> info(needed + 1);
                    count = static_cast<UINT>(info.size());
                    if (getList(session, &needed, &count, info.data(), &reason)
                            == ERROR_SUCCESS) {
                        for (UINT i = 0; i < count && i < info.size(); ++i) {
                            std::string name = WideToUtf8(info[i].strAppName);
                            if (name.empty()) name = "Unnamed program";
                            out.push_back(name + " (" +
                                          std::to_string(info[i].Process.dwProcessId) + ")");
                        }
                    }
                }
            }
            endSession(session);
            ::FreeLibrary(dll);
        }
#else
        void CollectHolders(const std::wstring&, std::vector<std::string>&) {}
#endif

        FileLockInfo ProbeOne(const std::string& path, bool wantHolders) {
            FileLockInfo info;
            if (path.empty()) return info;

            std::wstring wide = Utf8ToWide(path);
            const DWORD attrs = ::GetFileAttributesW(wide.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES) return info;      // gone
            // A directory is held by things this probe cannot see - a process
            // whose current directory it is, above all - and opening one says
            // nothing about that. Left Unknown rather than reported Free.
            if (attrs & FILE_ATTRIBUTE_DIRECTORY) return info;

            info.state = FileLockState::Free;

            DWORD err = ERROR_SUCCESS;
            // What overwriting the file in place needs.
            if (!CanOpen(wide, GENERIC_WRITE, err) && IsSharingFailure(err))
                info.writeBlocked = true;
            // What replacing it with a new file needs - the access a copy or a
            // rename over it asks for, and the one a running program denies.
            if (!CanOpen(wide, DELETE, err) && IsSharingFailure(err))
                info.replaceBlocked = true;

            if (info.writeBlocked || info.replaceBlocked) {
                info.state = FileLockState::Locked;
                if (wantHolders) CollectHolders(wide, info.holders);
            }
            return info;
        }

    } // namespace

    bool NativeProbeFileLocks(const std::vector<std::string>& paths,
                              bool wantHolders,
                              std::vector<FileLockInfo>& out) {
        out.clear();
        out.reserve(paths.size());
        for (const std::string& p : paths)
            out.push_back(ProbeOne(p, wantHolders));
        return true;
    }

} // namespace UltraCanvas
