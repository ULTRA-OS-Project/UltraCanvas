// OS/MSWindows/UltraCanvasWindowsFileRemoval.cpp
// Windows backend for UltraCanvasFileRemoval: a delete that tells "access
// denied" apart. Win32 reports four different refusals with that one error
// (UltraCanvasFileRemoval.h lists them); the NTSTATUS behind it, and the
// point at which the system refused, say which one it was:
//
//   opening for DELETE fails, STATUS_DELETE_PENDING   already deleted; a
//                                                     program still holds it
//   opening for DELETE fails, read-only attribute     FAT and exFAT refuse
//                                                     the open itself: lift it
//   opening for DELETE fails otherwise                the access list says no
//                                                     (NotPermitted)
//   the open succeeded, the delete is refused         STATUS_CANNOT_DELETE:
//                                                     read-only (lift it) or
//                                                     mapped / running (InUse)
//
// Once the system has granted DELETE access it has answered the permission
// question, so a refusal after that is never one an administrator can get
// past - which is exactly the case UltraFiler used to send to the elevated
// helper, to be told "access denied" a second time.
//
// The delete itself is FILE_DISPOSITION_INFO_EX with POSIX semantics and
// "ignore read-only" where the system has it (Windows 10 1809+, NTFS): the
// name leaves the folder at once even while another program holds the file
// with delete sharing, so the folder can go straight after. Elsewhere - FAT,
// exFAT, network shares, older Windows - the classic disposition, after
// lifting the read-only attribute, which is put back if the delete fails.
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework
#include "UltraCanvasFileRemoval.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace fs = std::filesystem;

namespace UltraCanvas {

    namespace {

        // The NTSTATUS a delete-pending entry answers every open with; Win32
        // flattens it into ERROR_ACCESS_DENIED.
        constexpr LONG kStatusDeletePending = static_cast<LONG>(0xC0000056L);

        // FILE_DISPOSITION_INFO_EX and its flags (Windows 10 1607 / 1809),
        // spelled out here because older SDK and MinGW headers lack them.
        // The classic FILE_DISPOSITION_INFO is spelled out too: its member is
        // named DeleteFile, which <windows.h> turns into DeleteFileW.
        struct DispositionInfoEx { DWORD flags; };
        struct DispositionInfoClassic { BOOLEAN deleteFile; };
        constexpr DWORD kDispositionDelete = 0x00000001;
        constexpr DWORD kDispositionPosixSemantics = 0x00000002;
        constexpr DWORD kDispositionForceImageSectionCheck = 0x00000004;
        constexpr DWORD kDispositionIgnoreReadOnly = 0x00000010;
        constexpr int kFileDispositionInfoEx = 21;   // FILE_INFO_BY_HANDLE_CLASS

        // How long a folder whose last entries are delete pending is waited
        // for, step by step: about two seconds in all. Long enough for a
        // scanner or an indexer to let go, short enough to stay unnoticed
        // inside a delete's progress window.
        constexpr DWORD kPendingWaitsMs[] = {10, 20, 40, 80, 150, 250, 400, 500, 500};
        constexpr int kPendingWaitCount =
                static_cast<int>(sizeof(kPendingWaitsMs) / sizeof(kPendingWaitsMs[0]));

        using RtlGetLastNtStatusFn = LONG (WINAPI*)();

        // ntdll's RtlGetLastNtStatus: the NTSTATUS behind the thread's last
        // Win32 error. Resolved before the call whose status it reads, so
        // nothing runs between that call failing and the status being read.
        RtlGetLastNtStatusFn NtStatusReader() {
            static const RtlGetLastNtStatusFn reader = []() -> RtlGetLastNtStatusFn {
                HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
                if (!ntdll) return nullptr;
                return reinterpret_cast<RtlGetLastNtStatusFn>(reinterpret_cast<void (*)()>(
                        GetProcAddress(ntdll, "RtlGetLastNtStatus")));
            }();
            return reader;
        }

        // The last failure on this thread: its Win32 error and, for "access
        // denied", whether it really meant "delete pending". Read straight
        // after the failing call.
        struct Failure {
            DWORD error = ERROR_SUCCESS;
            bool deletePending = false;
        };

        Failure LastFailure(RtlGetLastNtStatusFn status) {
            Failure f;
            f.error = GetLastError();
            if (f.error == ERROR_ACCESS_DENIED && status)
                f.deletePending = status() == kStatusDeletePending;
            return f;
        }

        bool IsGone(DWORD error) {
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
        }

        FileRemovalProblem ClassifyWin32(DWORD error) {
            switch (error) {
                case ERROR_SHARING_VIOLATION:
                case ERROR_LOCK_VIOLATION:
                case ERROR_USER_MAPPED_FILE:
                case ERROR_BUSY:
                    return FileRemovalProblem::InUse;
                case ERROR_ACCESS_DENIED:
                case ERROR_PRIVILEGE_NOT_HELD:
                    return FileRemovalProblem::NotPermitted;
                case ERROR_DIR_NOT_EMPTY:
                    return FileRemovalProblem::NotEmpty;
                default:
                    return FileRemovalProblem::Other;
            }
        }

        HANDLE OpenEntry(const std::wstring& path, DWORD access) {
            // BACKUP_SEMANTICS opens a folder; OPEN_REPARSE_POINT the link or
            // junction itself, never its target. Every sharing flag on our
            // side, so this handle is never what another program trips over.
            return CreateFileW(path.c_str(), access,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                               nullptr);
        }

        bool IsDeletePending(const std::wstring& path) {
            const RtlGetLastNtStatusFn status = NtStatusReader();
            HANDLE h = OpenEntry(path, FILE_READ_ATTRIBUTES);
            if (h == INVALID_HANDLE_VALUE) return LastFailure(status).deletePending;
            // Openable, yet marked: a delete set on a handle still open.
            FILE_STANDARD_INFO info = {};
            const bool pending = GetFileInformationByHandleEx(h, FileStandardInfo,
                                                              &info, sizeof(info)) &&
                                 info.DeletePending;
            CloseHandle(h);
            return pending;
        }

        // Whether a folder that answered "not empty" holds nothing but
        // entries already deleted and waiting for a program to let go -
        // worth waiting for. Nothing listed at all counts too: the last one
        // went in the meantime and a retry will do.
        bool OnlyDeletePendingEntries(const std::wstring& folder) {
            std::wstring prefix = folder;
            if (!prefix.empty() && prefix.back() != L'\\' && prefix.back() != L'/')
                prefix += L'\\';
            WIN32_FIND_DATAW data = {};
            HANDLE find = FindFirstFileW((prefix + L"*").c_str(), &data);
            if (find == INVALID_HANDLE_VALUE)
                return GetLastError() == ERROR_FILE_NOT_FOUND;
            bool other = false;
            do {
                const std::wstring name = data.cFileName;
                if (name == L"." || name == L"..") continue;
                if (!IsDeletePending(prefix + name)) { other = true; break; }
            } while (FindNextFileW(find, &data));
            FindClose(find);
            return !other;
        }

        // The attributes SetFileAttributesW accepts, from what
        // GetFileAttributesW reported.
        DWORD SettableAttributes(DWORD attributes) {
            const DWORD settable = attributes &
                    (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM |
                     FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_OFFLINE |
                     FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
            return settable ? settable : FILE_ATTRIBUTE_NORMAL;
        }

        // Lifts the read-only attribute. True only when it was set and is
        // now lifted - the caller then owes the entry its attribute back if
        // the delete still fails.
        bool LiftReadOnly(const std::wstring& path, DWORD attributes) {
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                !(attributes & FILE_ATTRIBUTE_READONLY))
                return false;
            DWORD writable = SettableAttributes(attributes) & ~FILE_ATTRIBUTE_READONLY;
            if (writable == 0) writable = FILE_ATTRIBUTE_NORMAL;
            return SetFileAttributesW(path.c_str(), writable) != FALSE;
        }

        void RestoreAttributes(const std::wstring& path, DWORD attributes) {
            SetFileAttributesW(path.c_str(), SettableAttributes(attributes));
        }

        // Marks the open entry deleted: POSIX semantics where the system and
        // the file system have them, the classic flag otherwise. `error` is
        // the reason when neither took.
        bool MarkDeleted(HANDLE h, DWORD& error) {
            // DeleteFileW's own flags on current Windows, plus "ignore
            // read-only". The image-section check keeps a running program's
            // executable refused, as Explorer refuses it, rather than
            // unlinked from under the program.
            DispositionInfoEx ex = {kDispositionDelete | kDispositionPosixSemantics |
                                    kDispositionForceImageSectionCheck |
                                    kDispositionIgnoreReadOnly};
            if (SetFileInformationByHandle(h, static_cast<FILE_INFO_BY_HANDLE_CLASS>(
                                                      kFileDispositionInfoEx),
                                           &ex, sizeof(ex)))
                return true;
            error = GetLastError();
            // A folder with entries left: the classic flag says the same.
            if (error == ERROR_DIR_NOT_EMPTY) return false;
            // Anything else - an older Windows, FAT or exFAT, a network
            // share that knows no POSIX delete - gets the classic one.
            DispositionInfoClassic classic = {TRUE};
            if (SetFileInformationByHandle(h, FileDispositionInfo, &classic, sizeof(classic)))
                return true;
            error = GetLastError();
            return false;
        }

    } // namespace

    FileRemovalProblem NativeClassifyFileRemovalError(const std::error_code& ec) {
        if (ec.category() == std::system_category())
            return ClassifyWin32(static_cast<DWORD>(ec.value()));
        // A runtime that reports errno values instead. libc++ maps a
        // sharing violation to EACCES as well, which is why a caller with a
        // lock probe asks it who holds the file before believing this.
        if (ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted)
            return FileRemovalProblem::NotPermitted;
        if (ec == std::errc::device_or_resource_busy || ec == std::errc::text_file_busy ||
            ec == std::errc::no_lock_available)
            return FileRemovalProblem::InUse;
        if (ec == std::errc::directory_not_empty) return FileRemovalProblem::NotEmpty;
        return FileRemovalProblem::Other;
    }

    bool NativeFileSystemEntryIsDeletePending(const fs::path& path) {
        return IsDeletePending(path.native());
    }

    bool NativeRemoveFileSystemEntry(const fs::path& path, std::error_code& ec,
                                     FileRemovalProblem& problem) {
        const std::wstring p = path.native();
        const RtlGetLastNtStatusFn status = NtStatusReader();

        const DWORD attributes = GetFileAttributesW(p.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const Failure f = LastFailure(status);
            if (IsGone(f.error) || f.deletePending) return true;
            // Otherwise the open below reports what counts.
        }
        const bool isFolder = attributes != INVALID_FILE_ATTRIBUTES &&
                              (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
                              !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
        bool lifted = false;
        int waits = 0;

        auto fail = [&](DWORD error, FileRemovalProblem why) {
            if (lifted) RestoreAttributes(p, attributes);
            ec = std::error_code(static_cast<int>(error), std::system_category());
            problem = why;
            return false;
        };

        for (;;) {
            HANDLE h = OpenEntry(p, DELETE | FILE_READ_ATTRIBUTES);
            if (h == INVALID_HANDLE_VALUE) {
                const Failure f = LastFailure(status);
                if (IsGone(f.error) || f.deletePending) return true;
                // FAT and exFAT refuse even the open of a read-only entry.
                if (f.error == ERROR_ACCESS_DENIED && !lifted && LiftReadOnly(p, attributes)) {
                    lifted = true;
                    continue;
                }
                // Refused before anything was attempted: the access list.
                return fail(f.error, ClassifyWin32(f.error));
            }
            DWORD error = ERROR_SUCCESS;
            const bool marked = MarkDeleted(h, error);
            CloseHandle(h);
            if (marked) return true;

            if (error == ERROR_ACCESS_DENIED && !lifted && LiftReadOnly(p, attributes)) {
                lifted = true;
                continue;
            }
            if (error == ERROR_DIR_NOT_EMPTY && isFolder && waits < kPendingWaitCount &&
                OnlyDeletePendingEntries(p)) {
                Sleep(kPendingWaitsMs[waits++]);
                continue;
            }
            if (error == ERROR_ACCESS_DENIED) {
                // STATUS_CANNOT_DELETE with DELETE access granted and no
                // read-only attribute in the way: the file is mapped into a
                // process (a running program, a memory-mapped picture). No
                // administrator gets past that; the program letting go does.
                return fail(error, FileRemovalProblem::InUse);
            }
            if (error == ERROR_DIR_NOT_EMPTY && waits > 0) {
                // Waited for, and still held: what is left is in use.
                return fail(error, FileRemovalProblem::InUse);
            }
            return fail(error, ClassifyWin32(error));
        }
    }

} // namespace UltraCanvas
