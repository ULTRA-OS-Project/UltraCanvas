// Tests/UltraNetFtpQuoteTest.cpp
// The commands UltraNet sends to change something on an FTP or SFTP server
// (UltraCanvas/core/UltraNet/UltraNetFtpQuote.h).
//
// A rename or a delete of any name with a space in it used to fail with a
// 550: the name was cut from the URL still percent-encoded and sent as it
// was. And every command ran in the login folder, whatever folder the entry
// was in, because libcurl sends them before it changes folder. And an SFTP server was sent FTP commands it does not speak. Both are
// checked here, along with the line breaks that must never reach a server.
// Version: 1.0.0
// Last Modified: 2026-09-27
// Author: UltraCanvas Framework
#include "UltraNetFtpQuote.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ultranet_internal::ftpquote;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "  ok  " : " FAIL ", what);
    if (!ok) ++failures;
}

void CheckPlan(const std::string& url, Verb verb, const std::string& newName,
               const std::string& parent, const std::vector<std::string>& cmds,
               const char* what) {
    Plan p;
    std::string error;
    const bool ok = Build(url, verb, newName, p, error) && p.parentUrl == parent &&
                    p.commands == cmds;
    std::printf("%s  %s", ok ? "  ok  " : " FAIL ", what);
    if (!ok) {
        std::printf("  (error \"%s\", parent \"%s\", commands:", error.c_str(),
                    p.parentUrl.c_str());
        for (const auto& c : p.commands) std::printf(" [%s]", c.c_str());
        std::printf(")");
    }
    std::printf("\n");
    if (!ok) ++failures;
}

bool Refused(const std::string& url, Verb verb, const std::string& newName = {}) {
    Plan p;
    std::string error;
    return !Build(url, verb, newName, p, error) && !error.empty();
}

} // namespace

int main() {
    std::printf("FTP\n");
    CheckPlan("ftp://h/dir/plain.txt", Verb::Rename, "new.txt", "ftp://h/dir/",
              {"RNFR dir/plain.txt", "RNTO dir/new.txt"},
              "a plain rename, named from the login folder");
    CheckPlan("ftp://h/dir/My%20Photo%20(2).jpg", Verb::Rename, "Holiday 2.jpg",
              "ftp://h/dir/", {"RNFR dir/My Photo (2).jpg", "RNTO dir/Holiday 2.jpg"},
              "the old name is decoded, the new one sent as typed");
    CheckPlan("ftp://h/%C3%9Cbersicht.pdf", Verb::Delete, {}, "ftp://h/",
              {"DELE \xC3\x9C" "bersicht.pdf"}, "UTF-8 is decoded to its bytes");
    CheckPlan("ftp://h/a%20b/c%20d", Verb::RemoveDirectory, {}, "ftp://h/a%20b/",
              {"RMD a b/c d"}, "the parent URL stays encoded, for libcurl to decode");
    CheckPlan("ftp://h/a/new%20folder", Verb::MakeDirectory, {}, "ftp://h/a/",
              {"MKD a/new folder"}, "make a folder where it was asked for");
    CheckPlan("ftps://h:990/a/old", Verb::RemoveDirectory, {}, "ftps://h:990/a/",
              {"RMD a/old"}, "a folder with a port");
    CheckPlan("ftp://h/a%2Bb%26c", Verb::Delete, {}, "ftp://h/", {"DELE a+b&c"},
              "'+' and '&'");
    CheckPlan("ftp://h/%2Fsrv/x.txt", Verb::Delete, {}, "ftp://h/%2Fsrv/",
              {"DELE /srv/x.txt"}, "%2F first: an absolute path");
    CheckPlan("ftp://h/base/deep/er/f", Verb::Rename, "g", "ftp://h/base/deep/er/",
              {"RNFR base/deep/er/f", "RNTO base/deep/er/g"},
              "a server base path and nested folders");

    std::printf("SFTP\n");
    CheckPlan("sftp://h/home/u/My%20Photo.jpg", Verb::Rename, "Holiday.jpg",
              "sftp://h/home/u/",
              {"rename \"/home/u/My Photo.jpg\" \"/home/u/Holiday.jpg\""},
              "rename takes two full paths");
    CheckPlan("SFTP://h/x/a%22b%5Cc", Verb::Delete, {}, "SFTP://h/x/",
              {"rm \"/x/a\\\"b\\\\c\""}, "quotes and backslashes are escaped");
    CheckPlan("sftp://h/~/docs/old", Verb::RemoveDirectory, {}, "sftp://h/~/docs/",
              {"rmdir \"docs/old\""}, "the home folder, relative");
    CheckPlan("sftp://h:2222/srv/new", Verb::MakeDirectory, {}, "sftp://h:2222/srv/",
              {"mkdir \"/srv/new\""}, "make a folder");

    std::printf("Refused\n");
    Check(Refused("ftp://h/", Verb::Delete), "the server's root");
    Check(Refused("ftp://127.0.0.1:18899/probe/", Verb::MakeDirectory),
          "a URL ending in '/' names nothing to create (the ApiStatus probe's rule)");
    Check(Refused("ftp://h/a/old/", Verb::RemoveDirectory),
          "... nor anything to remove");
    Check(Refused("ftp://h", Verb::Delete), "no path at all");
    Check(Refused("ftp://h/a%2", Verb::Delete), "a broken escape");
    Check(Refused("ftp://h/a", Verb::Rename, ""), "no new name");
    Check(Refused("ftp://h/a", Verb::Rename, "x/y"), "a new name that is a path");
    Check(Refused("ftp://h/a", Verb::Rename, "b\r\nDELE c"),
          "a line break in the new name - a second command");
    Check(Refused("ftp://h/a%0D%0ADELE%20c", Verb::Delete),
          "an encoded line break in the name");
    Check(Refused("ftp://h/a%00b", Verb::Delete), "a NUL in the name");
    Check(Refused("ftp://h/dir/..", Verb::Delete), "'..' is not a name");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
