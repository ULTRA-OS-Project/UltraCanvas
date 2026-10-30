// Tests/VirtualFSArchiveWriteOptionsTest.cpp
// VirtualFSOpenOptions::archiveWrite - what UltraFiler's Settings > Archives >
// TAR hands to CreateArchive - is honoured by the libarchive provider:
//   1. the compressor level reaches xz and zstd (level 0/1 against 9/19 on
//      compressible data gives a different size), threads and zstd's long
//      mode leave a readable archive;
//   2. the TAR header format is the one asked for (pax / GNU / ustar);
//   3. a file keeps its own modification time and permission bits, or the
//      packing time and 0644 when asked not to; the owner is left out by
//      default;
//   4. a symbolic link is stored as a link, or as the file it points to;
//   5. the exclusions leave out a folder with all it holds, a name pattern,
//      and a pattern with a '/' matched against the path;
//   6. VirtualFSOpenOptions::compressionLevel reaches ZIP, 7-Zip and a
//      stream compressor (Store stores a ZIP), and Normal writes what the
//      library default does.
//
// Archives are written below the directory given as the first argument
// (ctest passes one in the build tree), else the system temp directory.
// Version: 1.1.0 - the compressionLevel preset; repeat.bin, the data the
//                  levels are told apart by (on the text alone xz -1 beats -9)
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "VirtualFS/VirtualFS.h"

#include <archive.h>
#include <archive_entry.h>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <sys/stat.h>
#include "../UltraCanvas/include/UltraCanvasPathUtf8.h"

namespace fs = std::filesystem;
using namespace VirtualFS;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

static int failures = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (cond) {                                                         \
            std::printf("  PASS  %s\n", msg);                               \
        } else {                                                            \
            std::printf("  FAIL  %s (line %d)\n", msg, __LINE__);           \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

// What a test needs to know of one entry of a written archive.
struct EntryFacts {
    int         type = 0;
    int         perm = 0;
    time_t      mtime = 0;
    int64_t     uid = -1;
    std::string uname;
    std::string linkTarget;
    std::string content;
};

struct ArchiveFacts {
    bool opened = false;
    int  format = 0;
    std::map<std::string, EntryFacts> entries;   // name without a trailing '/'
};

static ArchiveFacts ReadArchive(const fs::path& path) {
    ArchiveFacts facts;
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, PathToUtf8(path).c_str(), 64 * 1024) != ARCHIVE_OK) {
        archive_read_free(a);
        return facts;
    }
    facts.opened = true;
    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) >= ARCHIVE_WARN) {
        facts.format = archive_format(a);
        std::string name = archive_entry_pathname(entry) ? archive_entry_pathname(entry) : "";
        while (!name.empty() && name.back() == '/') name.pop_back();
        EntryFacts e;
        e.type = static_cast<int>(archive_entry_filetype(entry));
        e.perm = static_cast<int>(archive_entry_perm(entry));
        e.mtime = archive_entry_mtime(entry);
        e.uid = archive_entry_uid(entry);
        e.uname = archive_entry_uname(entry) ? archive_entry_uname(entry) : "";
        e.linkTarget = archive_entry_symlink(entry) ? archive_entry_symlink(entry) : "";
        char buffer[8192];
        la_ssize_t got;
        while ((got = archive_read_data(a, buffer, sizeof(buffer))) > 0)
            e.content.append(buffer, static_cast<size_t>(got));
        facts.entries[name] = e;
    }
    archive_read_free(a);
    return facts;
}

static void WriteFile(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

static bool Pack(const fs::path& archive, const fs::path& folder,
                 const VirtualFSArchiveWriteOptions& write,
                 VirtualFSCompressionLevel preset = VirtualFSCompressionLevel::Normal) {
    VirtualFSOpenOptions options = VirtualFSOpenOptions::ForCreating(preset);
    options.archiveWrite = write;
    std::error_code ec;
    fs::remove(archive, ec);
    const VirtualFSResult r = VirtualFS_CreateArchive(
            PathToUtf8(archive), {PathToUtf8(folder)}, options);
    if (r != VirtualFSResult::Ok)
        std::printf("  CreateArchive(%s): %s\n", PathToUtf8(archive).c_str(),
                    VirtualFS_GetErrorMessage(r).c_str());
    return r == VirtualFSResult::Ok;
}

static uintmax_t SizeOf(const fs::path& path) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    return ec ? 0 : size;
}

int main(int argc, char** argv) {
    VirtualFS_Initialize();
    const fs::path root = (argc > 1 ? PathFromUtf8(argv[1]) : fs::temp_directory_path())
                        / "vfs-archive-write-options";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path src = root / "src";
    fs::create_directories(src / "sub", ec);
    fs::create_directories(src / ".git" / "objects", ec);

    // Compressible text, which every level of every compressor shrinks.
    std::string text;
    for (int i = 0; i < 40000; ++i)
        text += "line " + std::to_string(i % 977) + " of the sample text, " +
                std::to_string((i * 7919) % 104729) + "\n";
    WriteFile(src / "data.txt", text);
    // What tells the levels apart for certain: a 1.2 MB block of noise, then
    // the same block again. Only a match window longer than the block finds
    // the repeat - xz/7-Zip from level 6 (8 MB and more) and zstd from level
    // 19 do, while xz 0/1 (256 KB / 1 MB) and zstd 1 (512 KB) cannot - so
    // the higher level is smaller by about the block. On the text alone the
    // levels do not even order: xz -1 packs it smaller than xz -9.
    {
        std::string block(1200 * 1024, '\0');
        uint32_t state = 2463534242u;
        for (char& c : block) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            c = static_cast<char>(state & 0xFF);
        }
        WriteFile(src / "repeat.bin", block + block);
    }
    WriteFile(src / "sub" / "note.md", "keep me");
    WriteFile(src / "sub" / "scratch.tmp", "excluded by name");
    WriteFile(src / "sub" / "drop.log", "excluded by path");
    WriteFile(src / "top.log", "kept: the path pattern names sub/");
    WriteFile(src / ".git" / "objects" / "abc", "excluded with its folder");

    // A fixed modification time and permission bits to look for.
    const auto fixedTime = fs::last_write_time(src / "sub" / "note.md") -
                           std::chrono::hours(24 * 365 * 3);
    fs::last_write_time(src / "sub" / "note.md", fixedTime, ec);
    const time_t expectedMtime = [&]() {
        struct stat st {};
        return ::stat(PathToUtf8(src / "sub" / "note.md").c_str(), &st) == 0
                ? st.st_mtime : time_t(0);
    }();
#if !defined(_WIN32)
    fs::permissions(src / "sub" / "note.md",
                    fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_read | fs::perms::owner_exec, ec);
    fs::create_symlink("note.md", src / "sub" / "link.md", ec);
    const bool haveLink = !ec;
#else
    const bool haveLink = false;
#endif

    std::printf("== compressor level ==\n");
    {
        VirtualFSArchiveWriteOptions low, high;
        low.filterLevel = 0;
        high.filterLevel = 9;
        high.threads = 0;   // one per core
        CHECK(Pack(root / "low.tar.xz", src, low), "xz level 0 packs");
        CHECK(Pack(root / "high.tar.xz", src, high), "xz level 9, all cores, packs");
        CHECK(SizeOf(root / "high.tar.xz") < SizeOf(root / "low.tar.xz"),
              "xz level 9 is smaller than level 0");
        CHECK(ReadArchive(root / "high.tar.xz").entries.count("src/data.txt") == 1,
              "the multi-threaded xz archive reads back");

        VirtualFSArchiveWriteOptions fast, strong;
        fast.filterLevel = 1;
        strong.filterLevel = 19;
        strong.threads = 0;
        strong.zstdLongWindowLog = 27;
        CHECK(Pack(root / "fast.tar.zst", src, fast), "zstd level 1 packs");
        CHECK(Pack(root / "strong.tar.zst", src, strong), "zstd level 19 --long=27 packs");
        CHECK(SizeOf(root / "strong.tar.zst") < SizeOf(root / "fast.tar.zst"),
              "zstd level 19 is smaller than level 1");
        const ArchiveFacts strongFacts = ReadArchive(root / "strong.tar.zst");
        CHECK(strongFacts.entries.count("src/data.txt") == 1 &&
              strongFacts.entries.at("src/data.txt").content == text,
              "the long-mode zstd archive reads back byte-identical");

        VirtualFSArchiveWriteOptions gz;
        gz.filterLevel = 1;
        CHECK(Pack(root / "fast.tar.gz", src, gz), "gzip level 1 packs");
        CHECK(Pack(root / "fast.tar.bz2", src, gz), "bzip2 level 1 packs");
    }

    std::printf("== TAR header format ==\n");
    {
        VirtualFSArchiveWriteOptions gnu, ustar, pax;
        gnu.tarFormat = VirtualFSTarFormat::Gnu;
        ustar.tarFormat = VirtualFSTarFormat::Ustar;
        CHECK(Pack(root / "gnu.tar", src, gnu), "GNU tar packs");
        CHECK(Pack(root / "ustar.tar", src, ustar), "ustar packs");
        CHECK(Pack(root / "pax.tar.gz", src, pax), "pax packs");
        CHECK(ReadArchive(root / "gnu.tar").format == ARCHIVE_FORMAT_TAR_GNUTAR,
              "GNU tar headers are GNU");
        CHECK(ReadArchive(root / "ustar.tar").format == ARCHIVE_FORMAT_TAR_USTAR,
              "ustar headers are ustar");
        const int paxFormat = ReadArchive(root / "pax.tar.gz").format;
        CHECK(paxFormat == ARCHIVE_FORMAT_TAR_USTAR ||
              paxFormat == ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE,
              "pax headers are ustar with pax extensions where needed");
    }

    std::printf("== what is kept of a file ==\n");
    {
        VirtualFSArchiveWriteOptions keep;   // the defaults
        CHECK(Pack(root / "keep.tar", src, keep), "defaults pack");
        const ArchiveFacts kept = ReadArchive(root / "keep.tar");
        const EntryFacts note = kept.entries.count("src/sub/note.md")
                ? kept.entries.at("src/sub/note.md") : EntryFacts{};
        CHECK(note.content == "keep me", "a file's content is stored");
        CHECK(note.mtime == expectedMtime, "its own modification time is kept");
        CHECK(note.uid == 0 && note.uname.empty(), "the owner is left out by default");
#if !defined(_WIN32)
        CHECK(note.perm == 0740, "its own permission bits are kept");
#endif

        VirtualFSArchiveWriteOptions normalized;
        normalized.keepTimestamps = false;
        normalized.keepPermissions = false;
        const time_t before = std::time(nullptr);
        CHECK(Pack(root / "normalized.tar", src, normalized), "normalized pack");
        const ArchiveFacts plain = ReadArchive(root / "normalized.tar");
        const EntryFacts flat = plain.entries.count("src/sub/note.md")
                ? plain.entries.at("src/sub/note.md") : EntryFacts{};
        CHECK(flat.mtime >= before, "without timestamps a file carries the packing time");
        CHECK(flat.perm == 0644, "without permissions a file is 0644");
        CHECK(plain.entries.count("src/sub") == 1 &&
              plain.entries.at("src/sub").perm == 0755,
              "without permissions a folder is 0755");
    }

    if (haveLink) {
        std::printf("== symbolic links ==\n");
        VirtualFSArchiveWriteOptions links;
        CHECK(Pack(root / "links.tar", src, links), "links stored as links pack");
        const ArchiveFacts asLinks = ReadArchive(root / "links.tar");
        CHECK(asLinks.entries.count("src/sub/link.md") == 1 &&
              asLinks.entries.at("src/sub/link.md").type == AE_IFLNK &&
              asLinks.entries.at("src/sub/link.md").linkTarget == "note.md",
              "a link is stored as a link to its target");

        VirtualFSArchiveWriteOptions follow;
        follow.storeSymlinks = false;
        CHECK(Pack(root / "follow.tar", src, follow), "followed links pack");
        const ArchiveFacts followed = ReadArchive(root / "follow.tar");
        CHECK(followed.entries.count("src/sub/link.md") == 1 &&
              followed.entries.at("src/sub/link.md").type == AE_IFREG &&
              followed.entries.at("src/sub/link.md").content == "keep me",
              "a followed link is stored as the file it points to");
    }

    std::printf("== exclusions ==\n");
    {
        VirtualFSArchiveWriteOptions skip;
        skip.excludePatterns = {".git", "*.TMP", "sub/*.log"};
        CHECK(Pack(root / "skip.tar.zst", src, skip), "packing with exclusions");
        const ArchiveFacts facts = ReadArchive(root / "skip.tar.zst");
        CHECK(facts.entries.count("src/.git") == 0 &&
              facts.entries.count("src/.git/objects/abc") == 0,
              "an excluded folder is left out with all it holds");
        CHECK(facts.entries.count("src/sub/scratch.tmp") == 0,
              "a name pattern excludes, case-insensitively");
        CHECK(facts.entries.count("src/sub/drop.log") == 0,
              "a pattern with '/' matches the path below the packed folder");
        CHECK(facts.entries.count("src/top.log") == 1,
              "... and only that path");
        CHECK(facts.entries.count("src/sub/note.md") == 1, "everything else is packed");
        CHECK(VirtualFSWildcardMatch("node_modules", "node_modules") &&
              !VirtualFSWildcardMatch("*.o", "main.obj") &&
              VirtualFSWildcardMatch("?ain.*", "main.cpp"),
              "the wildcard matcher");
    }

    std::printf("== the compressionLevel preset ==\n");
    {
        using L = VirtualFSCompressionLevel;
        const VirtualFSArchiveWriteOptions none;   // filterLevel -1: the preset decides
        for (const char* ext : {"zip", "7z", "tar.gz", "tar.xz", "tar.zst"}) {
            const fs::path fastest = root / (std::string("fastest.") + ext);
            const fs::path best = root / (std::string("best.") + ext);
            const bool packed = Pack(fastest, src, none, L::Fastest) &&
                                Pack(best, src, none, L::Best);
            const std::string what = std::string(ext) + ": Best is smaller than Fastest";
            CHECK(packed && SizeOf(best) < SizeOf(fastest), what.c_str());
        }
        CHECK(Pack(root / "store.zip", src, none, L::Store), "a stored ZIP packs");
        CHECK(SizeOf(root / "store.zip") > text.size(),
              "Store keeps a ZIP uncompressed");
        const ArchiveFacts stored = ReadArchive(root / "store.zip");
        CHECK(stored.entries.count("src/data.txt") == 1 &&
              stored.entries.at("src/data.txt").content == text,
              "the stored ZIP reads back");

        VirtualFSArchiveWriteOptions six;
        six.filterLevel = 6;   // gzip's own default
        CHECK(Pack(root / "normal.tar.gz", src, none, L::Normal) &&
              Pack(root / "six.tar.gz", src, six, L::Normal) &&
              SizeOf(root / "normal.tar.gz") == SizeOf(root / "six.tar.gz"),
              "Normal leaves gzip at its default level");
        VirtualFSArchiveWriteOptions one;
        one.filterLevel = 1;
        CHECK(Pack(root / "override.tar.gz", src, one, L::Best) &&
              SizeOf(root / "override.tar.gz") == SizeOf(root / "fast.tar.gz"),
              "filterLevel wins over the preset");
    }

    fs::remove_all(root, ec);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
