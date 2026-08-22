// Apps/UltraCleaner/engine/UltraCleanerRemover.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerRemover.h"

#include "UltraCleanerPaths.h"
#include "UltraCleanerSafety.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <shellapi.h>
#endif

namespace UltraCleaner {
namespace {

namespace fs = std::filesystem;

// A name that does not collide with anything already in the trash. The XDG
// spec and the Finder both expect the caller to disambiguate.
std::string UniqueTrashName(const std::string& directory,
                            const std::string& baseName) {
    std::error_code ec;
    if (!fs::exists(fs::path(directory) / baseName, ec)) return baseName;

    const fs::path base(baseName);
    const std::string stem = base.stem().string();
    const std::string extension = base.extension().string();
    for (int suffix = 1; suffix < 10000; ++suffix) {
        const std::string candidate =
            stem + "." + std::to_string(suffix) + extension;
        if (!fs::exists(fs::path(directory) / candidate, ec)) return candidate;
    }
    return baseName + ".overflow";
}

// XDG local time stamp: "2026-08-22T13:57:01".
std::string TrashInfoTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%S", &local);
    return buffer;
}

// Percent-encode the characters the .trashinfo format reserves.
std::string PercentEncodePath(const std::string& path) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(path.size() + 8);
    for (unsigned char c : path) {
        const bool unreserved =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~' || c == '/';
        if (unreserved) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

// Rename first — it is atomic and keeps hard links — and fall back to a
// copy+remove when the trash lives on another filesystem.
bool MoveDirectoryOrFile(const fs::path& from, const fs::path& to,
                         std::string& error) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) return true;

    std::error_code copyEc;
    fs::copy(from, to,
             fs::copy_options::recursive | fs::copy_options::copy_symlinks,
             copyEc);
    if (copyEc) {
        error = copyEc.message();
        return false;
    }
    std::error_code removeEc;
    fs::remove_all(from, removeEc);
    if (removeEc) {
        error = removeEc.message();
        return false;
    }
    return true;
}

#if !defined(_WIN32) && !defined(_WIN64) && !defined(__APPLE__)
// freedesktop.org trash: the file goes to <trash>/files/<name> and a
// .trashinfo alongside it records where it came from, which is what makes
// "Restore" work in the desktop's file manager.
bool MoveToXdgTrash(const std::string& path, std::string& error) {
    const std::string trash = TrashDir();
    if (trash.empty()) {
        error = "no trash directory for this session";
        return false;
    }
    const fs::path filesDir = fs::path(trash) / "files";
    const fs::path infoDir  = fs::path(trash) / "info";

    std::error_code ec;
    fs::create_directories(filesDir, ec);
    fs::create_directories(infoDir, ec);
    if (ec) {
        error = "cannot create the trash directory: " + ec.message();
        return false;
    }

    const std::string baseName = fs::path(path).filename().string();
    const std::string unique = UniqueTrashName(filesDir.string(), baseName);

    // The info file is written first: a file in files/ without its info is a
    // stray the desktop cannot restore, while the reverse is harmless.
    std::ofstream info(infoDir / (unique + ".trashinfo"), std::ios::binary);
    if (!info) {
        error = "cannot write the trash info file";
        return false;
    }
    info << "[Trash Info]\n"
         << "Path=" << PercentEncodePath(path) << "\n"
         << "DeletionDate=" << TrashInfoTimestamp() << "\n";
    info.close();

    if (!MoveDirectoryOrFile(fs::path(path), filesDir / unique, error)) {
        std::error_code cleanupEc;
        fs::remove(infoDir / (unique + ".trashinfo"), cleanupEc);
        return false;
    }
    return true;
}
#endif

#if defined(__APPLE__)
bool MoveToMacTrash(const std::string& path, std::string& error) {
    const std::string trash = TrashDir();
    if (trash.empty()) {
        error = "no ~/.Trash for this user";
        return false;
    }
    std::error_code ec;
    fs::create_directories(fs::path(trash), ec);
    const std::string baseName = fs::path(path).filename().string();
    const std::string unique = UniqueTrashName(trash, baseName);
    return MoveDirectoryOrFile(fs::path(path), fs::path(trash) / unique, error);
}
#endif

#if defined(_WIN32) || defined(_WIN64)
std::wstring Widen(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        wide.data(), size);
    return wide;
}

bool MoveToWindowsRecycleBin(const std::string& path, std::string& error) {
    std::wstring wide = Widen(path);
    for (auto& c : wide) if (c == L'/') c = L'\\';
    wide.push_back(L'\0');          // SHFileOperation wants a double-null list
    wide.push_back(L'\0');

    SHFILEOPSTRUCTW operation{};
    operation.wFunc  = FO_DELETE;
    operation.pFrom  = wide.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION |
                       FOF_NOERRORUI | FOF_SILENT | FOF_NOCONFIRMMKDIR;
    const int result = SHFileOperationW(&operation);
    if (result != 0 || operation.fAnyOperationsAborted) {
        error = "the shell refused to recycle the item (code " +
                std::to_string(result) + ")";
        return false;
    }
    return true;
}
#endif

} // namespace

bool MoveToPlatformTrash(const std::string& path, std::string& error) {
#if defined(_WIN32) || defined(_WIN64)
    return MoveToWindowsRecycleBin(path, error);
#elif defined(__APPLE__)
    return MoveToMacTrash(path, error);
#else
    return MoveToXdgTrash(path, error);
#endif
}

bool EmptyPlatformRecycleBin(std::string& error) {
#if defined(_WIN32) || defined(_WIN64)
    const HRESULT result = SHEmptyRecycleBinW(
        nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    // S_OK, and "already empty" reported as E_UNEXPECTED, are both success.
    if (SUCCEEDED(result)) return true;
    error = "the shell could not empty the recycle bin";
    return false;
#else
    error = "this platform has no shell-managed recycle bin";
    return false;
#endif
}

RemovalReport Remover::Remove(const ScanReport& report,
                              const RemovalOptions& options,
                              ProgressCallback onProgress) {
    cancelRequested_ = false;

    RemovalReport result;
    result.simulated = options.mode == RemovalMode::Simulate;

    // The guard is rebuilt from the scan's own roots, not from the items:
    // an item list that was tampered with still cannot reach outside the
    // locations the rule table resolved.
    PathGuard guard;
    for (const auto& root : report.allowedRoots) guard.AllowRoot(root);

    std::vector<const CleanItem*> selected;
    for (const auto& item : report.items) {
        if (item.selected) selected.push_back(&item);
    }

    size_t done = 0;
    for (const CleanItem* item : selected) {
        if (cancelRequested_) {
            result.cancelled = true;
            break;
        }
        ++done;
        if (onProgress) onProgress(done, selected.size(), item->path);

        // The recycle bin is not a path — it goes through the shell.
        if (item->ruleId == "trash.windows") {
            if (options.mode == RemovalMode::Simulate) {
                ++result.removedItems;
                result.freedBytes += item->sizeBytes;
                continue;
            }
            std::string error;
            if (EmptyPlatformRecycleBin(error)) {
                ++result.removedItems;
                result.freedBytes += item->sizeBytes;
            } else {
                result.failures.push_back({ item->path, error });
            }
            continue;
        }

        const SafetyCheck check = guard.Check(item->path);
        if (!check.Allowed()) {
            ++result.refusedByGuard;
            result.failures.push_back({ item->path, check.reason });
            if (options.failureLimit != 0 &&
                result.failures.size() >= options.failureLimit) {
                break;
            }
            continue;
        }

        std::error_code ec;
        if (!fs::exists(fs::symlink_status(fs::path(item->path), ec)) || ec) {
            ++result.skippedMissing;
            continue;
        }

        if (options.mode == RemovalMode::Simulate) {
            ++result.removedItems;
            result.freedBytes += item->sizeBytes;
            continue;
        }

        std::string error;
        bool removed = false;
        if (options.mode == RemovalMode::MoveToTrash) {
            removed = MoveToPlatformTrash(item->path, error);
        } else {
            std::error_code removeEc;
            const uintmax_t count = fs::remove_all(fs::path(item->path), removeEc);
            removed = !removeEc && count > 0;
            if (removeEc) error = removeEc.message();
            else if (count == 0) error = "nothing was removed";
        }

        if (removed) {
            ++result.removedItems;
            result.freedBytes += item->sizeBytes;
        } else {
            result.failures.push_back({ item->path, error });
            if (options.failureLimit != 0 &&
                result.failures.size() >= options.failureLimit) {
                break;
            }
        }
    }
    if (cancelRequested_) result.cancelled = true;
    return result;
}

} // namespace UltraCleaner
