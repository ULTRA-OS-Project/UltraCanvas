// Tests/VirtualFSExtractSafetyTest.cpp
// ExtractAll writes nothing outside the destination, whatever the archive
// names ("zip slip").
//
// An archive chooses the paths its entries are written to. ExtractAll joined
// each one to the destination as it was, with none of libarchive's secure
// extraction flags set, so an entry named "../../.bashrc" landed outside the
// folder the user picked, an absolute "/etc/..." name went where it said,
// and an archive that first extracted "link -> ../outside" could then write
// "link/pwned.txt" through it. Hard links were joined to nothing at all: their
// target stayed relative to the archive root and resolved against the
// process's working directory. This test checks that:
//   * "../escape.txt", "/abs-<pid>.txt" and "a/../../escape2.txt" are refused
//     and named in the error, nothing is written outside, and the result is
//     not Success;
//   * a symbolic link entry pointing outside may exist, but nothing is
//     written through it;
//   * the benign entries next to them - files, a folder, an in-archive link -
//     still extract;
//   * a hard link lands under the destination, pointing at the entry it names
//     there, even when the working directory is somewhere else; one whose
//     target climbs out is refused;
//   * a destination reached through a symbolic link of its own still
//     extracts (the secure-symlink guard must only see the archive's links).
// Version: 1.0.0
// Last Modified: 2026-09-24
// Author: UltraCanvas Framework

#include "VirtualFS/VirtualFS.h"
#include "VirtualFSLibArchiveProvider.h"
#include "VirtualFSTestZip.h"

#include <archive.h>
#include <archive_entry.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace VirtualFS;
using VirtualFSTestZip::BuildZip;
using VirtualFSTestZip::ZipItem;

namespace {

int failures = 0;

void Check(bool cond, const std::string& msg) {
    std::printf("  %s  %s\n", cond ? "PASS" : "FAIL", msg.c_str());
    if (!cond) ++failures;
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void WriteFile(const fs::path& path, const std::string& data) {
    std::ofstream(path, std::ios::binary) << data;
}

bool Exists(const fs::path& path) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(path, ec));
}

// A pax tar with hard links, written through libarchive: ZIP cannot hold one.
struct TarItem {
    std::string name;
    std::string data;
    std::string hardlink;   // non-empty: a hard link to this archive path
};

bool BuildTar(const fs::path& path, const std::vector<TarItem>& items) {
    struct archive* a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    if (archive_write_open_filename(a, path.c_str()) != ARCHIVE_OK) {
        archive_write_free(a);
        return false;
    }
    for (const TarItem& it : items) {
        struct archive_entry* e = archive_entry_new();
        archive_entry_set_pathname(e, it.name.c_str());
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        if (!it.hardlink.empty()) {
            archive_entry_set_hardlink(e, it.hardlink.c_str());
            archive_entry_set_size(e, 0);
        } else {
            archive_entry_set_size(e, static_cast<la_int64_t>(it.data.size()));
        }
        archive_write_header(a, e);
        if (it.hardlink.empty() && !it.data.empty())
            archive_write_data(a, it.data.data(), it.data.size());
        archive_entry_free(e);
    }
    archive_write_close(a);
    archive_write_free(a);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const fs::path work = fs::absolute(fs::path(argc > 1 ? argv[1]
                                                         : "vfs-extract-safety-test-out"));
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work / "outside");
    fs::create_directories(work / "elsewhere");
    const std::string pid = std::to_string(getpid());
    const std::string absName = "/tmp/uc-zip-slip-abs-" + pid + ".txt";
    fs::remove(absName, ec);

    std::printf("A hostile ZIP\n");
    {
        const std::string zip = BuildZip({
            {"good/", false, "", 040755},
            {"good/ok.txt", false, "ok", 0100644},
            {"../escape.txt", false, "evil", 0100644},
            {absName, false, "evil", 0100644},
            {"a/../../escape2.txt", false, "evil", 0100644},
            {"link", false, "../outside", 0120777},
            {"link/pwned.txt", false, "evil", 0100644},
            {"safe_link", false, "good/ok.txt", 0120777},
            {"good/after.txt", false, "after", 0100644},
        });
        const fs::path archivePath = work / "hostile.zip";
        WriteFile(archivePath, zip);

        const fs::path dest = work / "dest";
        VirtualFSLibArchiveProvider provider;
        Check(provider.Open(archivePath.string()) == VirtualFSResult::Success, "archive opens");
        const VirtualFSResult result = provider.ExtractAll(dest.string());
        const std::string error = provider.GetLastError();
        std::printf("         result %d, lastError: %s\n", static_cast<int>(result), error.c_str());

        Check(result != VirtualFSResult::Success, "the extraction does not report Success");
        Check(error.find("../escape.txt") != std::string::npos &&
              error.find(absName) != std::string::npos &&
              error.find("a/../../escape2.txt") != std::string::npos,
              "lastError names every refused entry");
        Check(!Exists(work / "escape.txt"), "\"../escape.txt\" was not written outside");
        Check(!Exists(absName), "the absolute \"" + absName + "\" was not written");
        Check(!Exists(dest / absName.substr(1)), "nor under the destination");
        Check(!Exists(work / "escape2.txt"), "\"a/../../escape2.txt\" was not written outside");
        Check(!Exists(work / "outside" / "pwned.txt"),
              "nothing was written through the symbolic link \"link\"");

        Check(ReadFile(dest / "good" / "ok.txt") == "ok", "a benign file still extracts");
        Check(ReadFile(dest / "good" / "after.txt") == "after",
              "and so do the entries after the refused ones");
        Check(fs::is_symlink(fs::symlink_status(dest / "safe_link", ec)),
              "an in-archive link still extracts");
        provider.Close();
    }

    std::printf("Hard links in a tar, from another working directory\n");
    {
        WriteFile(work / "victim.txt", "victim");
        const fs::path archivePath = work / "links.tar";
        Check(BuildTar(archivePath, {
                  {"h/target.txt", "T", ""},
                  {"h/hard.txt", "", "h/target.txt"},
                  {"h/evil.txt", "", "../victim.txt"},
              }), "tar written");

        const fs::path dest = work / "tar-dest";
        const fs::path oldCwd = fs::current_path();
        fs::current_path(work / "elsewhere");
        VirtualFSLibArchiveProvider provider;
        Check(provider.Open(archivePath.string()) == VirtualFSResult::Success, "archive opens");
        const VirtualFSResult result = provider.ExtractAll(dest.string());
        const std::string error = provider.GetLastError();
        fs::current_path(oldCwd);
        std::printf("         result %d, lastError: %s\n", static_cast<int>(result), error.c_str());

        Check(ReadFile(dest / "h" / "hard.txt") == "T", "the hard link extracts under the destination");
        struct stat a{}, b{};
        Check(stat((dest / "h" / "hard.txt").c_str(), &a) == 0 &&
              stat((dest / "h" / "target.txt").c_str(), &b) == 0 && a.st_ino == b.st_ino,
              "and is the same file as the entry it names");
        Check(!Exists(dest / "h" / "evil.txt"), "a hard link climbing out is refused");
        Check(error.find("h/evil.txt") != std::string::npos, "and named in lastError");
        Check(ReadFile(work / "victim.txt") == "victim", "the file it aimed at is untouched");
        provider.Close();
    }

    std::printf("A destination reached through a symbolic link\n");
    {
        const std::string zip = BuildZip({{"inner/file.txt", false, "fine", 0100644}});
        const fs::path archivePath = work / "plain.zip";
        WriteFile(archivePath, zip);
        fs::create_directories(work / "real-dest");
        fs::create_directory_symlink(work / "real-dest", work / "dest-link", ec);
        VirtualFSLibArchiveProvider provider;
        Check(provider.Open(archivePath.string()) == VirtualFSResult::Success, "archive opens");
        Check(provider.ExtractAll((work / "dest-link").string()) == VirtualFSResult::Success,
              "extracts: the destination's own link is not the archive's");
        Check(ReadFile(work / "real-dest" / "inner" / "file.txt") == "fine", "the file is there");
        provider.Close();
    }

    fs::remove(absName, ec);
    fs::remove_all(work, ec);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
