// Tests/VirtualFSExtractDatesTest.cpp
// A file taken out of an archive keeps its dates.
//
// ExtractFile wrote an entry's bytes with an ofstream and nothing else, so a
// single file extracted from an archive was dated the moment it was written,
// whatever the archive said - on every system. ExtractAll set the dates
// always, preserveTimestamps or not. And a ZIP's or a 7z's creation date was
// lost on the way out: libarchive reads it into the entry's change time,
// while its disk writer sets a file's creation date from the birth time only
// (Windows, macOS - Linux has no way to set one, so that last part is checked
// where it can be, by the build rows of those systems through the same code).
// This test checks that:
//   * ExtractFile gives the file the entry's modification date, from a ZIP, a
//     7z and a tar - to the second - and still writes the bytes;
//   * ExtractAll does the same for files and for a folder;
//   * preserveTimestamps = false leaves the extracted file dated now;
//   * ExtractFile refuses a name that is taken unless told to overwrite, and
//     names an entry that is not there;
//   * an archive packed by the provider keeps its files' modification dates.
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "VirtualFS/VirtualFS.h"
#include "VirtualFSLibArchiveProvider.h"

#include <archive.h>
#include <archive_entry.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>

namespace fs = std::filesystem;
using namespace VirtualFS;

namespace {

int failures = 0;

void Check(bool cond, const std::string& msg) {
    std::printf("  %s  %s\n", cond ? "PASS" : "FAIL", msg.c_str());
    if (!cond) ++failures;
}

// 2010-01-01 00:00:00 UTC, and a creation date a year before it.
constexpr time_t kModified = 1262304000;
constexpr time_t kCreated = 1230768000;

std::string ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

time_t ModifiedOf(const fs::path& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) return 0;
    return st.st_mtime;
}

using Writer = int (*)(struct archive*);

// An archive holding "docs/" and "docs/a.txt", both dated kModified, the file
// created at kCreated (written as ZIP's and 7z's third date, the ctime).
bool Build(const fs::path& path, Writer format) {
    struct archive* a = archive_write_new();
    format(a);
    if (archive_write_open_filename(a, path.c_str()) != ARCHIVE_OK) {
        archive_write_free(a);
        return false;
    }
    struct archive_entry* dir = archive_entry_new();
    archive_entry_set_pathname(dir, "docs/");
    archive_entry_set_filetype(dir, AE_IFDIR);
    archive_entry_set_perm(dir, 0755);
    archive_entry_set_mtime(dir, kModified, 0);
    archive_write_header(a, dir);
    archive_entry_free(dir);

    const std::string data = "hello, archive";
    struct archive_entry* e = archive_entry_new();
    archive_entry_set_pathname(e, "docs/a.txt");
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_entry_set_size(e, static_cast<la_int64_t>(data.size()));
    archive_entry_set_mtime(e, kModified, 0);
    archive_entry_set_ctime(e, kCreated, 0);
    archive_write_header(a, e);
    archive_write_data(a, data.data(), data.size());
    archive_entry_free(e);
    archive_write_close(a);
    archive_write_free(a);
    return true;
}

void Exercise(const fs::path& work, const std::string& label, const std::string& ext, Writer format) {
    std::printf("%s\n", label.c_str());
    const fs::path archivePath = work / ("dated." + ext);
    Check(Build(archivePath, format), "archive written");

    VirtualFSLibArchiveProvider provider;
    Check(provider.Open(archivePath.string()) == VirtualFSResult::Ok, "archive opens");

    // One file, out on its own.
    const fs::path single = work / (ext + "-single") / "taken out.txt";
    Check(provider.ExtractFile("docs/a.txt", single.string()) == VirtualFSResult::Ok,
          "ExtractFile succeeds");
    Check(ReadAll(single) == "hello, archive", "... with the entry's bytes");
    Check(ModifiedOf(single) == kModified, "... dated as the entry is, not now");

    Check(provider.ExtractFile("docs/a.txt", single.string()) == VirtualFSResult::AlreadyExists,
          "a name that is taken is refused");
    Check(provider.ExtractFile("docs/a.txt", single.string(), VirtualFSExtractOptions::Overwrite()) ==
              VirtualFSResult::Ok,
          "... unless told to overwrite");
    Check(ModifiedOf(single) == kModified, "... and the overwritten file is dated too");
    Check(provider.ExtractFile("docs/missing.txt", (work / "none.txt").string()) ==
              VirtualFSResult::NotFound,
          "an entry that is not there is named");
    Check(!fs::exists(work / "none.txt"), "... and nothing is written for it");

    VirtualFSExtractOptions undated;
    undated.preserveTimestamps = false;
    const fs::path fresh = work / (ext + "-undated.txt");
    const time_t before = std::time(nullptr) - 5;
    Check(provider.ExtractFile("docs/a.txt", fresh.string(), undated) == VirtualFSResult::Ok,
          "ExtractFile without the dates succeeds");
    Check(ModifiedOf(fresh) >= before, "... and the file is dated now");

    // The whole archive.
    const fs::path all = work / (ext + "-all");
    Check(provider.ExtractAll(all.string()) == VirtualFSResult::Ok, "ExtractAll succeeds");
    Check(ModifiedOf(all / "docs" / "a.txt") == kModified, "... the file keeps its date");
    Check(ModifiedOf(all / "docs") == kModified, "... and so does the folder");

    const fs::path allUndated = work / (ext + "-all-undated");
    Check(provider.ExtractAll(allUndated.string(), undated) == VirtualFSResult::Ok,
          "ExtractAll without the dates succeeds");
    Check(ModifiedOf(allUndated / "docs" / "a.txt") >= before,
          "... and follows preserveTimestamps = false");
    provider.Close();
}

} // namespace

int main(int argc, char** argv) {
    const fs::path work = fs::absolute(fs::path(argc > 1 ? argv[1] : "vfs-extract-dates-test-out"));
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work);

    Exercise(work, "ZIP", "zip", archive_write_set_format_zip);
    Exercise(work, "7z", "7z", archive_write_set_format_7zip);
    Exercise(work, "tar", "tar", archive_write_set_format_pax_restricted);

    std::printf("Packing\n");
    {
        const fs::path source = work / "source.txt";
        { std::ofstream(source, std::ios::binary) << "packed"; }
        const auto stamp = fs::file_time_type::clock::now() - std::chrono::hours(24 * 365 * 10);
        fs::last_write_time(source, stamp, ec);
        const time_t sourceTime = ModifiedOf(source);
        const fs::path packed = work / "packed.zip";
        VirtualFSLibArchiveProvider writer;
        Check(writer.CreateArchive(packed.string(), VirtualFSOpenOptions::ForCreating()) ==
                  VirtualFSResult::Ok,
              "an archive is created");
        Check(writer.AddFile(source.string(), "source.txt", VirtualFSOpenOptions::ForCreating()) ==
                  VirtualFSResult::Ok,
              "a file is added");
        writer.Close();
        VirtualFSLibArchiveProvider reader;
        Check(reader.Open(packed.string()) == VirtualFSResult::Ok, "the archive opens");
        const fs::path back = work / "unpacked" / "source.txt";
        Check(reader.ExtractFile("source.txt", back.string()) == VirtualFSResult::Ok,
              "the file comes back out");
        Check(ModifiedOf(back) == sourceTime, "... with the date it had when it was packed");
        reader.Close();
    }

    fs::remove_all(work, ec);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
