// Tests/VirtualFSNameEncodingTest.cpp
// Archive entry names reach the file display, and the disk, as UTF-8.
//
// A ZIP entry without the "language encoding" flag (general-purpose bit 11)
// stores its name in the DOS code page of the machine that made it — IBM437
// by the ZIP specification, and what Windows Explorer, WinZip and older 7-Zip
// write for "Namensänderung" (ä = 0x84). libarchive hands those bytes on
// untouched on Linux, so listing the archive showed "Namens�nderung" and
// extracting it created a folder whose name is not UTF-8 at all — drawn by the
// Filer as "Namens•nderung". This test builds such an archive byte by byte and
// checks that listing, reading and extracting it all produce the UTF-8 name,
// that UTF-8 flagged names (Thai, Russian, Chinese) survive unchanged, and
// that an unflagged name which already is valid UTF-8 (Info-ZIP on Linux
// writes those) is not re-decoded into mojibake.
// Version: 1.0.1
// Last Modified: 2026-09-24
// Author: UltraCanvas Framework

#include "VirtualFS/VirtualFS.h"
#include "VirtualFSLibArchiveProvider.h"
#include "VirtualFSTestZip.h"

#include <algorithm>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace VirtualFS;

static int failures = 0;

static void Check(bool cond, const std::string& msg) {
    std::printf("  %s  %s\n", cond ? "PASS" : "FAIL", msg.c_str());
    if (!cond) ++failures;
}

// The archive is written by hand (Tests/VirtualFSTestZip.h): libarchive and
// miniz always write UTF-8 names, and the point here is a legacy code page.
namespace {

using VirtualFSTestZip::BuildZip;

bool HasName(const std::vector<std::string>& names, const std::string& want) {
    return std::find(names.begin(), names.end(), want) != names.end();
}

bool IsValidUtf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3
                   : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + len > s.size()) return false;
        for (size_t k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += len;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    // The locale of the environment, as an application has it: the names must
    // come out right in "C" (a test runner, a service) as in a UTF-8 one.
    std::setlocale(LC_ALL, "");

    const fs::path work = fs::path(argc > 1 ? argv[1] : "vfs-name-encoding-test-out");
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work);

    // "Namensänderung/Grüße.txt" as Windows writes it: IBM437, no UTF-8 flag
    // (ä = 0x84, ü = 0x81, ß = 0xE1).
    const std::string cp437Dir  = "Namens\x84nderung/";
    const std::string cp437File = "Namens\x84nderung/Gr\x81\xE1" "e.txt";
    const std::string germanDir  = "Namens\xC3\xA4nderung";                 // Namensänderung
    const std::string germanFile = "Gr\xC3\xBC\xC3\x9F" "e.txt";           // Grüße.txt
    const std::string thai    = "\xE0\xB8\xA0\xE0\xB8\xB2\xE0\xB8\xA9\xE0\xB8\xB2"
                                "\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2.txt";  // ภาษาไทย.txt
    const std::string russian = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82.txt";  // Привет.txt
    const std::string chinese = "\xE4\xB8\xAD\xE6\x96\x87.txt";            // 中文.txt
    // Info-ZIP on Linux stores UTF-8 without setting the flag.
    const std::string unflaggedUtf8 = "\xC3\x9C" "bersicht.txt";          // Übersicht.txt

    const std::string zip = BuildZip({
        {cp437Dir, false, ""},
        {cp437File, false, "hallo"},
        {thai, true, "thai"},
        {russian, true, "russian"},
        {chinese, true, "chinese"},
        {unflaggedUtf8, false, "uebersicht"},
    });
    const fs::path archivePath = work / "names.zip";
    {
        std::ofstream f(archivePath, std::ios::binary);
        f.write(zip.data(), static_cast<std::streamsize>(zip.size()));
    }

    std::printf("Listing\n");
    VirtualFSLibArchiveProvider provider;
    Check(provider.Open(archivePath.string()) == VirtualFSResult::Success, "archive opens");

    std::vector<std::string> paths;
    bool allUtf8 = true;
    for (const VirtualFSEntry& e : provider.ListAll()) {
        paths.push_back(e.path);
        allUtf8 = allUtf8 && IsValidUtf8(e.path) && IsValidUtf8(e.name);
    }
    Check(allUtf8, "every listed name is valid UTF-8");
    Check(HasName(paths, germanDir) || HasName(paths, germanDir + "/"),
          "IBM437 folder name lists as \"Namensänderung\"");
    Check(HasName(paths, germanDir + "/" + germanFile),
          "IBM437 file name lists as \"Namensänderung/Grüße.txt\"");
    Check(HasName(paths, thai), "Thai name lists unchanged");
    Check(HasName(paths, russian), "Russian name lists unchanged");
    Check(HasName(paths, chinese), "Chinese name lists unchanged");
    Check(HasName(paths, unflaggedUtf8),
          "unflagged name that is already UTF-8 is kept, not re-decoded");

    std::vector<uint8_t> data;
    Check(provider.ReadFile(germanDir + "/" + germanFile, data) == VirtualFSResult::Success &&
          std::string(data.begin(), data.end()) == "hallo",
          "an IBM437-named entry reads by its UTF-8 name");
    Check(provider.ReadFile(thai, data) == VirtualFSResult::Success &&
          std::string(data.begin(), data.end()) == "thai",
          "a Thai-named entry reads by its name");

    std::printf("Extracting\n");
    const fs::path dest = work / "out";
    Check(provider.ExtractAll(dest.string()) == VirtualFSResult::Success, "ExtractAll succeeds");

    std::vector<std::string> onDisk;
    bool diskUtf8 = true;
    for (auto it = fs::recursive_directory_iterator(dest, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const std::string rel = fs::relative(it->path(), dest).generic_string();
        onDisk.push_back(rel);
        diskUtf8 = diskUtf8 && IsValidUtf8(rel);
    }
    Check(diskUtf8, "every extracted file name is valid UTF-8");
    Check(HasName(onDisk, germanDir), "folder extracts as \"Namensänderung\"");
    Check(HasName(onDisk, germanDir + "/" + germanFile),
          "file extracts as \"Namensänderung/Grüße.txt\"");
    Check(HasName(onDisk, thai) && HasName(onDisk, russian) && HasName(onDisk, chinese),
          "Thai, Russian and Chinese names extract unchanged");
    Check(HasName(onDisk, unflaggedUtf8), "unflagged UTF-8 name extracts unchanged");

    provider.Close();
    fs::remove_all(work, ec);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
