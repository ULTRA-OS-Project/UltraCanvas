// Tests/TrashTest.cpp
// MoveToTrash on freedesktop.org desktops (UltraCanvas/OS/Linux/
// UltraCanvasLinuxTrash.cpp): what "Move to the Trash" in the Filer's delete
// confirmation does, checked against the Trash specification 1.0.
//
//   * a file, a folder with its contents and a symbolic link (the link, not
//     its target) land in $XDG_DATA_HOME/Trash/files, each with a .trashinfo
//     whose Path= is the percent-encoded absolute origin - what "Restore" in
//     the desktop's file manager reads;
//   * UTF-8 names (German, Thai) keep their bytes, encoded byte by byte in
//     Path=;
//   * a second "Report.pdf" becomes "Report.2.pdf" rather than overwriting;
//   * the trash itself, anything already in it, a folder holding it and a
//     missing path are refused with a reason, and nothing moves;
//   * where a second file system is at hand (/dev/shm is usually tmpfs), a
//     file there goes to that drive's own .Trash-$uid with a Path= relative
//     to the drive's top directory - never copied into the home trash.
// The test points XDG_DATA_HOME at its own folder, so the user's real trash
// is never touched.
// Version: 1.0.0
// Last Modified: 2026-09-24
// Author: UltraCanvas Framework

#include "UltraCanvasTrash.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void Touch(const fs::path& path, const std::string& text = "x") {
    std::ofstream(path, std::ios::binary) << text;
}

bool Trash(const fs::path& path, std::string* errorOut = nullptr) {
    std::string error;
    const bool ok = MoveToTrash(path.string(), error);
    if (errorOut) *errorOut = error;
    if (!ok) std::cout << "         (" << path.string() << ": " << error << ")\n";
    return ok;
}

} // namespace

int main() {
    std::cout << "===== MoveToTrash (freedesktop.org Trash 1.0) =====\n";
    Check(TrashAvailable(), "this platform has a trash");
    Check(TrashDisplayName() == "Trash", "it is called \"Trash\"");

    std::error_code ec;
    const fs::path root = fs::temp_directory_path() /
                          ("uc-trash-test-" + std::to_string(getpid()));
    fs::remove_all(root, ec);
    const fs::path xdg = root / "xdg";
    const fs::path work = root / "work";
    fs::create_directories(xdg);
    fs::create_directories(work);
    setenv("XDG_DATA_HOME", xdg.c_str(), 1);
    const fs::path trash = xdg / "Trash";

    std::cout << "\n-- A file --\n";
    Touch(work / "Report.pdf", "first");
    Check(Trash(work / "Report.pdf"), "moved");
    Check(!fs::exists(work / "Report.pdf"), "gone from its folder");
    Check(ReadFile(trash / "files" / "Report.pdf") == "first", "in Trash/files, content intact");
    const std::string info = ReadFile(trash / "info" / "Report.pdf.trashinfo");
    Check(info.rfind("[Trash Info]\n", 0) == 0, ".trashinfo opens with [Trash Info]");
    Check(info.find("\nPath=" + (work / "Report.pdf").string() + "\n") != std::string::npos,
          ".trashinfo records the absolute origin");
    Check(info.find("\nDeletionDate=") != std::string::npos, ".trashinfo records the date");
    {
        struct stat st;
        Check(stat((trash / "files").c_str(), &st) == 0 && (st.st_mode & 0777) == 0700,
              "the trash is created private (0700)");
    }

    std::cout << "\n-- The same name again --\n";
    Touch(work / "Report.pdf", "second");
    Check(Trash(work / "Report.pdf"), "moved");
    Check(ReadFile(trash / "files" / "Report.pdf") == "first", "the first one is untouched");
    Check(ReadFile(trash / "files" / "Report.2.pdf") == "second", "the second is Report.2.pdf");
    Check(fs::exists(trash / "info" / "Report.2.pdf.trashinfo"), "with its own .trashinfo");

    std::cout << "\n-- A folder, with names in other scripts --\n";
    const std::string german = "Namens\xC3\xA4nderung";                       // Namensänderung
    const std::string thai = "\xE0\xB8\xA0\xE0\xB8\xB2\xE0\xB8\xA9\xE0\xB8\xB2.txt";  // ภาษา.txt
    fs::create_directories(work / german / "inner");
    Touch(work / german / thai, "thai");
    Touch(work / german / "inner" / "deep.txt", "deep");
    Check(Trash(work / german), "moved");
    Check(!fs::exists(work / german), "gone from its folder");
    Check(ReadFile(trash / "files" / german / thai) == "thai" &&
          ReadFile(trash / "files" / german / "inner" / "deep.txt") == "deep",
          "in the trash with everything in it");
    const std::string folderInfo = ReadFile(trash / "info" / (german + ".trashinfo"));
    Check(folderInfo.find("Namens%C3%A4nderung\n") != std::string::npos,
          "Path= encodes the UTF-8 name byte by byte");

    std::cout << "\n-- A symbolic link --\n";
    Touch(work / "target.txt", "target");
    fs::create_symlink(work / "target.txt", work / "link.txt", ec);
    Check(!ec && Trash(work / "link.txt"), "moved");
    Check(fs::is_symlink(fs::symlink_status(trash / "files" / "link.txt")),
          "the link itself is in the trash");
    Check(ReadFile(work / "target.txt") == "target", "its target is left alone");

    std::cout << "\n-- What is refused --\n";
    std::string error;
    Check(!Trash(work / "no-such-file", &error) && !error.empty(), "a missing path");
    Check(!Trash(trash, &error), "the trash itself");
    Check(!Trash(trash / "files" / "Report.pdf", &error) && fs::exists(trash / "files" / "Report.pdf"),
          "something already in the trash");
    Check(!Trash(xdg, &error) && fs::exists(xdg), "a folder holding the trash");
    Check(!MoveToTrash("", error), "an empty path");

    std::cout << "\n-- A file on another drive --\n";
    struct stat shm, home;
    const fs::path shmDir = fs::path("/dev/shm") / ("uc-trash-test-" + std::to_string(getpid()));
    if (stat("/dev/shm", &shm) == 0 && stat(root.c_str(), &home) == 0 &&
        shm.st_dev != home.st_dev && fs::create_directories(shmDir, ec)) {
        Touch(shmDir / "usb.txt", "usb");
        Check(Trash(shmDir / "usb.txt"), "moved");
        const fs::path driveTrash = fs::path("/dev/shm") / (".Trash-" + std::to_string(getuid()));
        const std::string name = "usb.txt";
        // Another run may have left a usb.txt behind; find ours by its info.
        std::string claimed;
        for (auto& e : fs::directory_iterator(driveTrash / "info", ec)) {
            const std::string text = ReadFile(e.path());
            if (text.find("Path=" + shmDir.filename().string() + "/usb.txt\n") != std::string::npos)
                claimed = e.path().filename().string();
        }
        Check(!claimed.empty(), "to the drive's own .Trash-$uid, Path= relative to the drive");
        Check(!fs::exists(trash / "files" / name), "not copied into the home trash");
        if (!claimed.empty()) {
            const std::string stored = claimed.substr(0, claimed.size() - 10);   // ".trashinfo"
            fs::remove(driveTrash / "files" / stored, ec);
            fs::remove(driveTrash / "info" / claimed, ec);
        }
        fs::remove(driveTrash / "files", ec);   // only if empty
        fs::remove(driveTrash / "info", ec);
        fs::remove(driveTrash, ec);
        fs::remove_all(shmDir, ec);
    } else {
        std::cout << "  (skipped: no second file system at /dev/shm)\n";
    }

    fs::remove_all(root, ec);
    std::cout << "\n" << (g_failures ? "FAILED" : "ALL PASSED") << " (" << g_failures
              << " failure" << (g_failures == 1 ? "" : "s") << ")\n";
    return g_failures ? 1 : 0;
}
