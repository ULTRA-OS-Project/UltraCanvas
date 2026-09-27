// Tests/FilerRemoteCacheTest.cpp
// The server-free half of UltraFiler's remote listing cache
// (Apps/UltraFiler/UltraFilerRemoteCache.h): which drives fetch subfolders
// ahead, which subfolders are picked, and the file listings are kept in
// between runs.
//
// The file is read at every start-up of a filer with an FTP drive, and it
// holds names a server chose - so a name with a tab or a newline in it must
// come back as itself, and a damaged file must cost the listing it damaged,
// never the start-up.
// Version: 1.0.0
// Last Modified: 2026-09-27
// Author: UltraCanvas Framework
#include "UltraFilerRemoteCache.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "  ok  " : " FAIL ", what);
    if (!ok) ++failures;
}

RemoteCachedEntry Dir(const std::string& folder, const std::string& name) {
    RemoteCachedEntry e;
    e.name = name;
    e.path = RemoteFilerChild(folder, name);
    e.isDirectory = true;
    return e;
}

RemoteCachedEntry File(const std::string& folder, const std::string& name,
                       uint64_t size, std::time_t modified) {
    RemoteCachedEntry e;
    e.name = name;
    e.path = RemoteFilerChild(folder, name);
    e.size = size;
    e.modifiedTime = modified;
    return e;
}

} // namespace

int main() {
    const std::string root = MakeRemoteFilerPath("nas", "/");
    const std::string pub = MakeRemoteFilerPath("nas", "/pub");

    std::printf("Which drives prefetch\n");
    Check(RemoteProviderPrefetches("ftp"), "ftp prefetches");
    Check(!RemoteProviderPrefetches("nextcloud"), "nextcloud does not");
    Check(!RemoteProviderPrefetches("googledrive"), "a metered cloud does not");
    Check(!RemoteProviderPrefetches(""), "no provider does not");

    std::printf("Picking the subfolders to fetch ahead\n");
    {
        std::vector<RemoteCachedEntry> entries = {
            Dir(root, "docs"), Dir(root, ".ssh"), File(root, "a.txt", 3, 0),
            Dir(root, "pub"), Dir(root, "www"),
        };
        auto t = SelectRemotePrefetchTargets(entries, {});
        Check(t.size() == 3, "folders only, hidden ones left out");
        Check(t.size() == 3 && t[0] == RemoteFilerChild(root, "docs") &&
              t[1] == pub && t[2] == RemoteFilerChild(root, "www"),
              "in listing order");

        t = SelectRemotePrefetchTargets(entries, {pub});
        Check(t.size() == 2 && t[1] == RemoteFilerChild(root, "www"),
              "a folder already known is not fetched again");

        t = SelectRemotePrefetchTargets(entries, {}, 2);
        Check(t.size() == 2, "the limit holds");

        std::vector<RemoteCachedEntry> many;
        for (int i = 0; i < 100; ++i) many.push_back(Dir(root, "d" + std::to_string(i)));
        Check(SelectRemotePrefetchTargets(many, {}).size() == kRemotePrefetchPerFolder,
              "a big folder is not fetched whole");
        Check(SelectRemotePrefetchTargets({}, {}).empty(), "an empty folder picks nothing");
    }

    std::printf("The file, round trip\n");
    {
        std::vector<RemoteCachedListing> in(2);
        in[0].folderPath = root;
        in[0].entries = {Dir(root, "pub"), File(root, "tab\there", 12345, 1700000000),
                         File(root, "line\nbreak\\slash\r", 0, 0)};
        in[1].folderPath = pub;   // an empty folder is a listing too
        std::vector<RemoteCachedListing> out;
        Check(ParseRemoteListings(SerializeRemoteListings(in), out), "parses");
        Check(out.size() == 2, "both listings come back");
        if (out.size() == 2) {
            Check(out[0].folderPath == root && out[1].folderPath == pub, "folder paths");
            Check(out[0].entries.size() == 3, "all entries");
            Check(out[1].entries.empty(), "the empty folder is still there");
            if (out[0].entries.size() == 3) {
                const auto& d = out[0].entries[0];
                const auto& f = out[0].entries[1];
                const auto& g = out[0].entries[2];
                Check(d.isDirectory && d.name == "pub" && d.path == pub, "a folder entry");
                Check(!f.isDirectory && f.name == "tab\there" && f.size == 12345 &&
                      f.modifiedTime == 1700000000, "a tab in a name, size and time");
                Check(g.name == "line\nbreak\\slash\r" && g.path == in[0].entries[2].path,
                      "newline, backslash and CR in a name");
            }
        }
    }

    std::printf("A damaged file\n");
    {
        std::vector<RemoteCachedListing> out;
        Check(!ParseRemoteListings("", out), "empty text is not the file");
        Check(!ParseRemoteListings("UltraFilerRemoteCache 2\n", out),
              "another version is not read");

        std::vector<RemoteCachedListing> in(2);
        in[0].folderPath = root;
        in[0].entries = {Dir(root, "pub")};
        in[1].folderPath = pub;
        in[1].entries = {File(pub, "x", 1, 1)};
        std::string text = SerializeRemoteListings(in);
        // Break the size of the entry in the first listing.
        const auto at = text.find("E\td\t0\t");
        text.replace(at, 6, "E\td\tx\t");
        Check(ParseRemoteListings(text, out), "still read");
        Check(out.size() == 1 && out[0].folderPath == pub,
              "only the damaged listing is dropped");

        Check(ParseRemoteListings(std::string(kRemoteCacheHeader) +
                                  "\nL\tnot-a-drive\nE\tf\t1\t1\tx\tultracloud://nas/x\n", out) &&
              out.empty(), "a listing outside any drive is dropped with its entries");
        Check(ParseRemoteListings(std::string(kRemoteCacheHeader) +
                                  "\nL\tultracloud://nas/\nE\tf\t1\t1\tbad\\q\tultracloud://nas/x\n",
                                  out) && out.empty(), "an unknown escape drops the listing");
        Check(ParseRemoteListings(std::string(kRemoteCacheHeader) +
                                  "\nL\tultracloud://nas/\nE\tf\t99999999999999999999999\t1\tx\tultracloud://nas/x\n",
                                  out) && out.empty(), "an overflowing size drops the listing");
        Check(ParseRemoteListings(std::string(kRemoteCacheHeader) +
                                  "\r\nL\tultracloud://nas/\r\nE\tf\t1\t2\tx\tultracloud://nas/x\r\n",
                                  out) && out.size() == 1 && out[0].entries.size() == 1,
              "CRLF line ends are read");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
