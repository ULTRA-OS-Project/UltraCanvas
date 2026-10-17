// Tests/FilerRemoteCacheTest.cpp
// The server-free half of UltraFiler's remote listing cache
// (Apps/UltraFiler/UltraFilerRemoteCache.h): which drives fetch subfolders
// ahead, which subfolders are picked, and the file listings are kept in
// between runs.
//
// The file is read at every start-up of a filer with an FTP drive, and it
// holds names a server chose - so a name with a tab or a newline in it must
// come back as itself, and a damaged file must cost the listing it damaged,
// never the start-up. Uploads to FTP are checked here too: which go via a
// part name, putting one in place on servers that do and do not rename onto
// a file, which part an unfinished one left is theirs to remove, and the
// file the notes are kept in.
// Version: 1.1.0 - unfinished uploads
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework
#include "UltraFilerRemoteCache.h"

#include <cstdio>
#include <map>
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

    std::printf("Preview copies\n");
    {
        const std::string photo = MakeRemoteFilerPath("nas", "/pub/photo.jpg");
        const std::string k = RemotePreviewCacheKey(photo, 1000, 1700000000);
        Check(k.size() == 16 && k.find_first_not_of("0123456789abcdef") == std::string::npos,
              "the key is 16 hex digits");
        Check(k == RemotePreviewCacheKey(photo, 1000, 1700000000), "the key is stable");
        Check(k != RemotePreviewCacheKey(photo, 1001, 1700000000), "a new size, a new key");
        Check(k != RemotePreviewCacheKey(photo, 1000, 1700000001), "a new date, a new key");
        Check(k != RemotePreviewCacheKey(MakeRemoteFilerPath("nas2", "/pub/photo.jpg"),
                                         1000, 1700000000), "another drive, another key");
        // Fixed across runs and platforms: the copies of the last run are
        // found again only if this never changes.
        Check(RemotePreviewCacheKey("", 0, 0) == RemotePreviewCacheKey("", 0, 0) &&
              RemotePreviewCacheKey("a", 1, 2) != RemotePreviewCacheKey("a1", 0, 2),
              "fields cannot run into each other");

        Check(RemotePreviewLocalName("photo.jpg") == "photo.jpg", "a plain name is kept");
        Check(RemotePreviewLocalName("a:b?c*.png") == "a_b_c_.png",
              "characters Windows refuses are replaced, the extension kept");
        Check(RemotePreviewLocalName("..") == "preview", "'..' is not a name");
        Check(RemotePreviewLocalName("x/../y.svg") == "x_.._y.svg", "no path gets through");
        Check(RemotePreviewLocalName("name. ") == "name", "trailing dots and blanks go");
        Check(RemotePreviewLocalName("tab\there.stl") == "tab_here.stl",
              "control characters are replaced");
        Check(kRemotePreviewMaxBytes == 64ull * 1024 * 1024, "the preview size limit");
    }

    std::printf("Uploads to FTP: which go via a part name\n");
    {
        Check(RemoteProviderLeavesPartialUploads("ftp"), "an FTP upload leaves its part");
        Check(!RemoteProviderLeavesPartialUploads("webdav") &&
              !RemoteProviderLeavesPartialUploads("nextcloud") &&
              !RemoteProviderLeavesPartialUploads("dropbox"),
              "WebDAV and the cloud services do not");
        Check(RemoteUploadsViaPartName("ftp", "ftp://example.com", "erika"),
              "a signed-in FTP drive uses part names");
        Check(RemoteUploadsViaPartName("ftp", "sftp://example.com", "erika") &&
              RemoteUploadsViaPartName("ftp", "SFTP://example.com", ""),
              "an SFTP drive does, whatever the user name");
        Check(!RemoteUploadsViaPartName("ftp", "ftp://example.com", "") &&
              !RemoteUploadsViaPartName("ftp", "ftps://example.com", "Anonymous") &&
              !RemoteUploadsViaPartName("ftp", "ftp://example.com", "ftp"),
              "an anonymous FTP login goes straight to the name");
        Check(!RemoteUploadsViaPartName("webdav", "https://example.com", "erika"),
              "a WebDAV drive needs none");
        Check(RemoteUploadPartName("report.pdf") == ".report.pdf.part", "the part name");
        Check(RemoteUploadSetAsideName("report.pdf") == ".report.pdf.replaced",
              "the set-aside name");
        Check(RemoteUploadPartName(std::string(249, 'x')).size() == 255 &&
              RemoteUploadPartName(std::string(250, 'x')).empty() &&
              RemoteUploadPartName("").empty(),
              "a part name too long for the server is not made");
    }

    std::printf("Uploads to FTP: putting one in place\n");
    {
        // A server's folder: names to contents. `overwrites`: a rename onto a
        // name that exists replaces it (FTP servers on Unix) or is refused
        // (SFTP, some Windows servers). `renames` / `deletes`: allowed at all.
        struct FakeServer {
            std::map<std::string, std::string> files;
            bool overwrites = true, renames = true, deletes = true;
            int answerFor = 1000;   // requests answered before the line goes dead
            std::vector<std::string> said;
            RemoteInPlaceOps Ops() {
                RemoteInPlaceOps ops;
                ops.rename = [this](const std::string& path, const std::string& to,
                                    bool& unreachable) {
                    if (answerFor-- <= 0) { unreachable = true; return false; }
                    const std::string from = path.substr(path.rfind('/') + 1);
                    if (!renames || !files.count(from)) return false;
                    if (files.count(to) && !overwrites) return false;
                    files[to] = files[from];
                    files.erase(from);
                    return true;
                };
                ops.remove = [this](const std::string& path, bool& unreachable) {
                    if (answerFor-- <= 0) { unreachable = true; return false; }
                    const std::string name = path.substr(path.rfind('/') + 1);
                    if (!deletes || !files.count(name)) return false;
                    files.erase(name);
                    return true;
                };
                ops.say = [this](const std::string& line) { said.push_back(line); };
                return ops;
            }
        };
        using Files = std::map<std::string, std::string>;
        const std::string part = RemoteUploadPartName("a.txt");

        FakeServer overwriting;
        overwriting.files = {{"a.txt", "old"}, {part, "new"}};
        Check(PutRemoteUploadInPlace("/up", "a.txt", overwriting.Ops()) == RemoteInPlace::Done &&
              overwriting.files == Files{{"a.txt", "new"}},
              "a server that renames onto a file replaces it in one step");

        FakeServer sftp;
        sftp.overwrites = false;
        sftp.files = {{"a.txt", "old"}, {part, "new"}, {"b.txt", "b"}};
        Check(PutRemoteUploadInPlace("", "a.txt", sftp.Ops()) == RemoteInPlace::Done &&
              sftp.files == Files{{"a.txt", "new"}, {"b.txt", "b"}} && sftp.said.size() == 1,
              "one that will not has the old file set aside, then removed");

        FakeServer fresh;
        fresh.overwrites = false;
        fresh.files = {{part, "new"}};
        Check(PutRemoteUploadInPlace("", "a.txt", fresh.Ops()) == RemoteInPlace::Done &&
              fresh.files == Files{{"a.txt", "new"}}, "a new file is renamed into place");

        FakeServer noRenames;
        noRenames.renames = false;
        noRenames.files = {{"a.txt", "old"}, {part, "new"}};
        Check(PutRemoteUploadInPlace("", "a.txt", noRenames.Ops()) == RemoteInPlace::Refused &&
              noRenames.files == Files{{"a.txt", "old"}, {part, "new"}},
              "a server that renames nothing is refused, and nothing has moved");

        FakeServer stuckAside;
        stuckAside.overwrites = false;
        stuckAside.deletes = false;
        stuckAside.files = {{"a.txt", "old"}, {part, "new"}};
        Check(PutRemoteUploadInPlace("", "a.txt", stuckAside.Ops()) == RemoteInPlace::Done &&
              stuckAside.files == Files{{"a.txt", "new"}, {RemoteUploadSetAsideName("a.txt"), "old"}} &&
              stuckAside.said.size() == 2,
              "an old file that cannot be removed is left set aside, and said so");

        FakeServer dropped;
        dropped.overwrites = false;
        dropped.answerFor = 2;   // the first rename, the set-aside; then nothing
        dropped.files = {{"a.txt", "old"}, {part, "new"}};
        Check(PutRemoteUploadInPlace("", "a.txt", dropped.Ops()) == RemoteInPlace::Unreachable,
              "a line that goes dead half way is unreachable, not refused");

        FakeServer dead;
        dead.answerFor = 0;
        dead.files = {{part, "new"}};
        Check(PutRemoteUploadInPlace("", "a.txt", dead.Ops()) == RemoteInPlace::Unreachable &&
              dead.files == Files{{part, "new"}}, "no answer at all is unreachable");
    }

    std::printf("Unfinished uploads\n");
    {
        RemoteUnfinishedUpload u;
        u.path = MakeRemoteFilerPath("nas", "/up/big.bin");
        u.server = "ftp://127.0.0.1:2121";
        u.bytesSent = 20u * 1024 * 1024;
        u.fileSize = 40u * 1024 * 1024;
        u.stoppedAt = 1760000000;
        Check(IsLeftByUnfinishedUpload(u, 0), "an empty file there is the part");
        Check(IsLeftByUnfinishedUpload(u, u.bytesSent - 4096), "less than was sent is the part");
        Check(IsLeftByUnfinishedUpload(u, u.bytesSent + 65536),
              "a buffer more than the last report is the part");
        Check(!IsLeftByUnfinishedUpload(u, u.fileSize), "the whole file is kept");
        Check(!IsLeftByUnfinishedUpload(u, u.fileSize + 1), "a larger file is kept");
        Check(!IsLeftByUnfinishedUpload(u, u.bytesSent + 2 * kRemoteUnfinishedUploadSlackBytes),
              "a file longer than was ever sent is kept");
        RemoteUnfinishedUpload whole = u;
        whole.bytesSent = whole.fileSize;
        Check(!IsLeftByUnfinishedUpload(whole, whole.fileSize),
              "at the file's own name a complete file is kept, even if all of it was sent");
        whole.partName = true;
        Check(IsLeftByUnfinishedUpload(whole, whole.fileSize),
              "at a part name a complete copy is UltraFiler's own");
        RemoteUnfinishedUpload early = u;
        early.partName = true;
        early.bytesSent = 0;
        Check(IsLeftByUnfinishedUpload(early, 0) &&
              !IsLeftByUnfinishedUpload(early, 2 * kRemoteUnfinishedUploadSlackBytes),
              "a part stopped before its first byte: empty is it, more is not");

        RemoteUploadState state;
        NoteRemoteUnfinishedUpload(state.unfinished, u);
        RemoteUnfinishedUpload again = u;
        again.bytesSent = 5;
        NoteRemoteUnfinishedUpload(state.unfinished, again);
        Check(state.unfinished.size() == 1 && state.unfinished[0].bytesSent == 5,
              "a second stop of the same file replaces the first note");
        RemoteUnfinishedUpload odd;
        odd.path = MakeRemoteFilerPath("nas", "/up/tab\there\nnewline");
        odd.server = "sftp://host\twith tab";
        odd.partName = true;
        odd.bytesSent = 1;
        odd.fileSize = 2;
        odd.stoppedAt = 1760000100;
        NoteRemoteUnfinishedUpload(state.unfinished, odd);
        state.direct.push_back({"anon\tdrop", "ftp://drop.example.com", 1760000150});

        const std::string text = SerializeRemoteUploadState(state);
        RemoteUploadState back;
        Check(ParseRemoteUploadState(text, 1760000200, back) && back.unfinished.size() == 2 &&
              back.unfinished[0].path == again.path && back.unfinished[0].bytesSent == 5 &&
              !back.unfinished[0].partName &&
              back.unfinished[0].fileSize == again.fileSize &&
              back.unfinished[0].stoppedAt == again.stoppedAt &&
              back.unfinished[0].server == again.server &&
              back.unfinished[1].path == odd.path && back.unfinished[1].server == odd.server &&
              back.unfinished[1].partName,
              "notes come back as they were written, tabs and newlines too");
        Check(back.direct.size() == 1 && back.direct[0].accountId == "anon\tdrop" &&
              back.direct[0].server == "ftp://drop.example.com" &&
              back.direct[0].since == 1760000150,
              "a drive that takes uploads direct comes back too");
        Check(ParseRemoteUploadState(text, 1760000000 + kRemoteUnfinishedUploadMaxAgeSeconds + 50,
                                     back) && back.unfinished.size() == 1 &&
              back.unfinished[0].path == odd.path && back.direct.size() == 1,
              "a note older than its limit is dropped");
        Check(ParseRemoteUploadState(text, 1760000150 + kRemoteUnfinishedUploadMaxAgeSeconds + 1,
                                     back) && back.direct.empty(),
              "a drive's direct uploads are tried via a part name again after the limit");
        Check(!ParseRemoteUploadState("", 0, back) && back.unfinished.empty(),
              "an empty file is not this file");
        Check(!ParseRemoteUploadState(std::string(kRemoteCacheHeader) + "\n", 0, back),
              "the listings' file is not this file");
        Check(ParseRemoteUploadState(std::string(kRemoteUploadStateHeader) + "\n", 0, back) &&
              back.unfinished.empty() && back.direct.empty(), "a file of no records is read");
        Check(ParseRemoteUploadState(std::string(kRemoteUploadStateHeader) +
                                     "\nU\tf\t1\t2\tx\tftp://h\tultracloud://nas/a"
                                     "\nU\tq\t1\t2\t3\tftp://h\tultracloud://nas/q"
                                     "\nU\tf\t1\t2\t3\tftp://h\t/not/remote"
                                     "\nU\tf\t1\t2\t3\tftp://h\tultracloud://nas/"
                                     "\nZ\tf\t1\t2\t3\tftp://h\tultracloud://nas/z"
                                     "\nD\tx\tftp://h\tnas"
                                     "\nD\t3\tftp://h\t"
                                     "\r\nU\tp\t1\t2\t3\tftp://h\tultracloud://nas/b\r\n",
                                     10, back) &&
              back.unfinished.size() == 1 &&
              back.unfinished[0].path == MakeRemoteFilerPath("nas", "/b") &&
              back.unfinished[0].partName && back.direct.empty(),
              "damaged lines are dropped, the good one kept");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
