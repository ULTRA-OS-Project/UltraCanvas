// Tests/UltraNet/test_ftp_parser.cpp
// Unit tests for the MLSD and UNIX `ls -l` listing parsers used by
// UltraNet_FtpListDirectory. These parsers are the heart of the rich
// FTP/SFTP listing surface; live-server tests are out of scope for CI.
#include "test_framework.h"

#include <UltraNet/UltraNetFtp.h>

#include "../../UltraCanvas/core/UltraNet/UltraNetFtpInternal.h"

#include <string>
#include <vector>

using ultranet_internal::ftp::ParseMlsdLine;
using ultranet_internal::ftp::ParseUnixLine;
using ultranet_internal::ftp::SplitLines;

// ===== MLSD =====

TEST(ftp_mlsd_parses_file_entry) {
    UltraNetFtpEntry e;
    const std::string line =
        "type=file;size=12345;modify=20231215120000;perm=adfr;"
        "UNIX.mode=0644;UNIX.owner=alice;UNIX.group=users; readme.txt";
    REQUIRE(ParseMlsdLine(line, e));
    REQUIRE_EQ(e.name, std::string{"readme.txt"});
    REQUIRE_EQ(e.type, UltraNetFtpEntryType::File);
    REQUIRE_EQ(e.size, static_cast<int64_t>(12345));
    REQUIRE_EQ(e.modificationTime, std::string{"2023-12-15T12:00:00Z"});
    REQUIRE_EQ(e.permissions, std::string{"adfr"});
    REQUIRE_EQ(e.owner, std::string{"alice"});
    REQUIRE_EQ(e.group, std::string{"users"});
}

TEST(ftp_mlsd_parses_dir_entry) {
    UltraNetFtpEntry e;
    REQUIRE(ParseMlsdLine(
        "type=dir;modify=20240101000000;perm=cdeflp; my_folder", e));
    REQUIRE_EQ(e.name, std::string{"my_folder"});
    REQUIRE_EQ(e.type, UltraNetFtpEntryType::Directory);
}

TEST(ftp_mlsd_parses_symlink_entry) {
    UltraNetFtpEntry e;
    REQUIRE(ParseMlsdLine("type=OS.unix=slink;size=0; alias.lnk", e));
    REQUIRE_EQ(e.name, std::string{"alias.lnk"});
    REQUIRE_EQ(e.type, UltraNetFtpEntryType::Symlink);
}

TEST(ftp_mlsd_synthesises_perms_from_unix_mode) {
    UltraNetFtpEntry e;
    // 0755 = rwxr-xr-x
    REQUIRE(ParseMlsdLine("type=dir;UNIX.mode=0755; folder", e));
    REQUIRE_EQ(e.permissions, std::string{"rwxr-xr-x"});
}

TEST(ftp_mlsd_keys_are_case_insensitive) {
    UltraNetFtpEntry e;
    REQUIRE(ParseMlsdLine(
        "TYPE=FILE;Size=42;MODIFY=20230101000000; X.bin", e));
    REQUIRE_EQ(e.type, UltraNetFtpEntryType::File);
    REQUIRE_EQ(e.size, static_cast<int64_t>(42));
}

TEST(ftp_mlsd_rejects_non_mlsd_line) {
    UltraNetFtpEntry e;
    CHECK(!ParseMlsdLine("just a filename with no facts", e));
    CHECK(!ParseMlsdLine("", e));
    CHECK(!ParseMlsdLine("type=file;", e));   // no space-then-name
}

TEST(ftp_mlsd_rejects_dot_entries) {
    UltraNetFtpEntry e;
    CHECK(!ParseMlsdLine("type=cdir; .",  e));
    CHECK(!ParseMlsdLine("type=pdir; ..", e));
}

// ===== UNIX ls -l =====

TEST(ftp_unix_parses_file_line) {
    UltraNetFtpEntry e;
    REQUIRE(ParseUnixLine(
        "-rw-r--r-- 1 alice users 1234 Jan  3  2023 file.txt", e));
    REQUIRE_EQ(e.name, std::string{"file.txt"});
    REQUIRE_EQ(e.type, UltraNetFtpEntryType::File);
    REQUIRE_EQ(e.size, static_cast<int64_t>(1234));
    REQUIRE_EQ(e.owner, std::string{"alice"});
    REQUIRE_EQ(e.group, std::string{"users"});
    REQUIRE_EQ(e.permissions, std::string{"rw-r--r--"});
}

TEST(ftp_unix_parses_directory_line) {
    UltraNetFtpEntry e;
    REQUIRE(ParseUnixLine(
        "drwxr-xr-x 2 alice users 4096 Dec 15 12:00 myfolder", e));
    REQUIRE_EQ(e.name, std::string{"myfolder"});
    REQUIRE_EQ(e.type, UltraNetFtpEntryType::Directory);
    REQUIRE_EQ(e.permissions, std::string{"rwxr-xr-x"});
}

TEST(ftp_unix_parses_symlink_and_strips_target) {
    UltraNetFtpEntry e;
    REQUIRE(ParseUnixLine(
        "lrwxrwxrwx 1 alice users 7 Mar  1 09:00 link -> target", e));
    REQUIRE_EQ(e.name, std::string{"link"});
    REQUIRE_EQ(e.type, UltraNetFtpEntryType::Symlink);
}

TEST(ftp_unix_rejects_ls_total_header) {
    UltraNetFtpEntry e;
    CHECK(!ParseUnixLine("total 1234", e));
}

TEST(ftp_unix_rejects_garbage_line) {
    UltraNetFtpEntry e;
    CHECK(!ParseUnixLine("hello world", e));
    CHECK(!ParseUnixLine("", e));
}

// ===== SplitLines =====

TEST(ftp_splitlines_handles_lf_and_crlf) {
    auto lines = SplitLines("first\r\nsecond\nthird\r\n");
    REQUIRE_EQ(lines.size(), static_cast<std::size_t>(3));
    REQUIRE_EQ(lines[0], std::string{"first"});
    REQUIRE_EQ(lines[1], std::string{"second"});
    REQUIRE_EQ(lines[2], std::string{"third"});
}

TEST(ftp_splitlines_skips_empty_lines) {
    auto lines = SplitLines("\n\nfoo\n\n");
    REQUIRE_EQ(lines.size(), static_cast<std::size_t>(1));
    REQUIRE_EQ(lines[0], std::string{"foo"});
}
