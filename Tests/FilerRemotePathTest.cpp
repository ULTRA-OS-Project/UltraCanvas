// Tests/FilerRemotePathTest.cpp
// The path scheme of UltraFiler's remote drives
// (Apps/UltraFiler/UltraFilerRemotePath.h): "ultracloud://<accountId><path>".
//
// These paths travel through everything that moves a folder path around - the
// folder display, the tree, the breadcrumb, the history, the prefetch cache -
// so the parsing has to be exact in both directions. Two spellings of one
// folder look like two folders to a cache keyed by path, and a parent that
// climbs past a drive's root walks out of the drive into a string nothing can
// resolve.
// Version: 1.1.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework
#include "UltraFilerRemotePath.h"

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

void CheckEq(const std::string& got, const std::string& want, const char* what) {
    const bool ok = got == want;
    std::printf("%s  %s", ok ? "  ok  " : " FAIL ", what);
    if (!ok) std::printf("  (got \"%s\", want \"%s\")", got.c_str(), want.c_str());
    std::printf("\n");
    if (!ok) ++failures;
}

} // namespace

int main() {
    const std::string acc = "ftp-files-example-org";

    // ===== RECOGNISING ONE =====
    {
        Check(IsRemoteFilerPath("ultracloud://a/"), "a drive root is a remote path");
        Check(IsRemoteFilerPath("ultracloud://a/Docs"), "a path inside one is too");
        Check(!IsRemoteFilerPath("/home/erika"), "a local path is not");
        Check(!IsRemoteFilerPath("C:\\Users\\erika"), "nor a Windows one");
        Check(!IsRemoteFilerPath("ftp://files.example.org/x"),
              "nor an ftp:// URL - the drive scheme is ours, not the server's");
        Check(!IsRemoteFilerPath(""), "nor the empty path");
        // The bare scheme names no drive, so it is not a path to anything.
        Check(!IsRemoteFilerPath("ultracloud://"), "the bare scheme is not a path");
        Check(!IsRemoteFilerPath("ultracloud:/"), "a near-miss scheme is not one");
    }

    // ===== BUILDING ONE =====
    {
        CheckEq(MakeRemoteFilerPath(acc, "/"),
                "ultracloud://" + acc + "/", "the root keeps its slash");
        CheckEq(MakeRemoteFilerPath(acc, ""),
                "ultracloud://" + acc + "/", "an empty path is the root");
        CheckEq(MakeRemoteFilerPath(acc, "/Docs"),
                "ultracloud://" + acc + "/Docs", "a folder");
        // One spelling per folder: a trailing slash and a missing leading one
        // both normalise, or a cache keyed by path holds the same folder twice.
        CheckEq(MakeRemoteFilerPath(acc, "/Docs/"),
                "ultracloud://" + acc + "/Docs", "a trailing slash is dropped");
        CheckEq(MakeRemoteFilerPath(acc, "Docs"),
                "ultracloud://" + acc + "/Docs", "a missing leading slash is added");
        CheckEq(MakeRemoteFilerPath(acc, "/a/b/c"),
                "ultracloud://" + acc + "/a/b/c", "a deep path");
        CheckEq(MakeRemoteFilerPath("", "/Docs"), "",
                "no account, no path");
    }

    // ===== SPLITTING ONE =====
    {
        std::string id, remote;
        Check(SplitRemoteFilerPath("ultracloud://" + acc + "/", id, remote),
              "the root splits");
        CheckEq(id, acc, "  its account id");
        CheckEq(remote, "/", "  its path is the root");

        Check(SplitRemoteFilerPath("ultracloud://" + acc + "/a/b", id, remote),
              "a deep path splits");
        CheckEq(id, acc, "  its account id");
        CheckEq(remote, "/a/b", "  its path");

        // No slash at all after the id: still that drive's root.
        Check(SplitRemoteFilerPath("ultracloud://" + acc, id, remote),
              "an id with no trailing slash splits");
        CheckEq(remote, "/", "  and means the root");

        // A trailing slash on a folder is tolerated on the way in and
        // normalised away, so it matches what MakeRemoteFilerPath produces.
        Check(SplitRemoteFilerPath("ultracloud://" + acc + "/a/", id, remote),
              "a trailing slash is tolerated");
        CheckEq(remote, "/a", "  and normalised off");

        // Failures leave the outputs alone.
        id = "untouched"; remote = "untouched";
        Check(!SplitRemoteFilerPath("/home/erika", id, remote),
              "a local path does not split");
        Check(!SplitRemoteFilerPath("ultracloud://", id, remote),
              "the bare scheme does not split");
        Check(!SplitRemoteFilerPath("ultracloud:///a", id, remote),
              "an empty account id does not split");
        CheckEq(id, "untouched", "  a failed split leaves the outputs alone");
        CheckEq(remote, "untouched", "  both of them");
    }

    // ===== ROUND TRIP =====
    {
        const char* paths[] = { "/", "/Docs", "/a/b/c", "/Q3 report/final.pdf" };
        for (const char* p : paths) {
            std::string id, remote;
            const std::string built = MakeRemoteFilerPath(acc, p);
            const bool ok = SplitRemoteFilerPath(built, id, remote) &&
                            id == acc && MakeRemoteFilerPath(id, remote) == built;
            Check(ok, (std::string("round trip: ") + p).c_str());
        }
    }

    // ===== ACCOUNT ID, ROOT, NAME =====
    {
        CheckEq(RemoteFilerAccountId("ultracloud://" + acc + "/a"), acc,
                "the account id of a path");
        CheckEq(RemoteFilerAccountId("/home/erika"), "",
                "a local path has no account id");

        Check(IsRemoteFilerRoot("ultracloud://" + acc + "/"),
              "the root is recognised");
        Check(IsRemoteFilerRoot("ultracloud://" + acc),
              "with or without the slash");
        Check(!IsRemoteFilerRoot("ultracloud://" + acc + "/Docs"),
              "a folder inside is not the root");
        Check(!IsRemoteFilerRoot("/home/erika"), "nor is a local path");

        CheckEq(RemoteFilerName("ultracloud://" + acc + "/a/b.txt"), "b.txt",
                "the name of an entry");
        CheckEq(RemoteFilerName("ultracloud://" + acc + "/b.txt"), "b.txt",
                "at the top level too");
        CheckEq(RemoteFilerName("ultracloud://" + acc + "/"), "",
                "the root has no name of its own - the drive's label stands in");
    }

    // ===== GOING UP =====
    {
        CheckEq(RemoteFilerParent("ultracloud://" + acc + "/a/b/c"),
                "ultracloud://" + acc + "/a/b", "up from a deep path");
        CheckEq(RemoteFilerParent("ultracloud://" + acc + "/a"),
                "ultracloud://" + acc + "/", "up from the top level is the root");
        // The one that matters: up from a drive root must not climb out into
        // something unresolvable. It answers empty, and the caller decides.
        CheckEq(RemoteFilerParent("ultracloud://" + acc + "/"), "",
                "up from the root leaves the drive, it does not climb past it");
        CheckEq(RemoteFilerParent("/home/erika"), "",
                "a local path has no remote parent");

        // The folder tree climbs with this in a loop, to find the deepest row
        // it already shows and expand from there down to the folder that was
        // navigated to (UltraFilerWindow::SyncTreeSelection). Two properties
        // it depends on: the climb ends, and the last path it yields is the
        // drive root spelled the way the tree keys that row - WITH the
        // trailing slash, since MakeRemoteFilerPath(acc, "/") is the node id
        // AddTreeRemoteDriveNode used. A chain that stopped one short of it,
        // or at "ultracloud://acc" without the slash, would never match the
        // drive's row and the tree would not follow the display into a
        // remote subfolder.
        {
            std::vector<std::string> chain;
            for (std::string p = "ultracloud://" + acc + "/a/b/c";
                 !p.empty(); p = RemoteFilerParent(p)) {
                chain.push_back(p);
                if (chain.size() > 8) break;   // a climb that will not end
            }
            Check(chain.size() == 4, "the climb ends at the drive root");
            if (chain.size() == 4) {
                CheckEq(chain[0], "ultracloud://" + acc + "/a/b/c", "it starts where it was asked");
                CheckEq(chain[1], "ultracloud://" + acc + "/a/b", "then the parent");
                CheckEq(chain[2], "ultracloud://" + acc + "/a", "then its parent");
                CheckEq(chain[3], "ultracloud://" + acc + "/",
                        "and last the drive root, as the tree spells it");
            }
        }
    }

    // ===== APPENDING A CHILD =====
    {
        CheckEq(RemoteFilerChild("ultracloud://" + acc + "/", "Docs"),
                "ultracloud://" + acc + "/Docs", "a child of the root");
        CheckEq(RemoteFilerChild("ultracloud://" + acc + "/a", "b.txt"),
                "ultracloud://" + acc + "/a/b.txt", "a child of a folder");
        // A name with a space is ordinary on a server and must survive as-is:
        // this is a path, not a URL, so nothing is escaped here.
        CheckEq(RemoteFilerChild("ultracloud://" + acc + "/", "Q3 report"),
                "ultracloud://" + acc + "/Q3 report",
                "a space in a name is kept verbatim");
        CheckEq(RemoteFilerChild("ultracloud://" + acc + "/", ""), "",
                "no name, no path");
        CheckEq(RemoteFilerChild("/home/erika", "x"), "",
                "a local folder takes no remote child");
    }

    // ===== WHAT COUNTS AS HIDDEN ON A SERVER =====
    {
        Check(IsHiddenRemoteFilerName(".ssh"), "a dot name is hidden");
        Check(IsHiddenRemoteFilerName(".htaccess"), "so is a dot file");
        Check(!IsHiddenRemoteFilerName("Videos"), "an ordinary name is not");
        Check(!IsHiddenRemoteFilerName("report.2026.txt"),
              "a dot inside a name does not hide it");
        Check(!IsHiddenRemoteFilerName(""), "and there is no empty name to hide");
    }

    // ===== THE TIME A LISTING REPORTS =====
    {
        // Both wire formats are UTC, and both must land on the same instant -
        // read as local time they would move by the viewer's offset, which is
        // how a remote file ends up dated a day out.
        const std::time_t want = RemoteFilerTimeFromUtcParts(2026, 9, 17, 10, 0, 0);
        Check(want > 0, "a UTC instant is built");
        Check(ParseRemoteFilerTime("20260917100000") == want,
              "FTP's MLSD timestamp is read as UTC");
        Check(ParseRemoteFilerTime("Thu, 17 Sep 2026 10:00:00 GMT") == want,
              "WebDAV's RFC 1123 date is read as UTC");
        Check(ParseRemoteFilerTime("20260917100000.250") == want,
              "a fractional second on an MLSD timestamp is ignored");
        // A date with no weekday still parses: the weekday is decoration.
        Check(ParseRemoteFilerTime("17 Sep 2026 10:00:00 GMT") == want,
              "an RFC 1123 date without its weekday still parses");

        // Nothing reported, and things that are not dates, answer 0 - which
        // the display already draws as a blank rather than as 1970.
        Check(ParseRemoteFilerTime("") == 0, "no timestamp answers 0");
        Check(ParseRemoteFilerTime("not a date") == 0, "nor does junk parse");
        Check(ParseRemoteFilerTime("2026") == 0, "a bare year is not a timestamp");
        Check(ParseRemoteFilerTime("Wed, 03 Foo 2026 10:00:00 GMT") == 0,
              "an unknown month name is refused");
        Check(ParseRemoteFilerTime("20261317100000") == 0,
              "month 13 is refused rather than wrapped");
        Check(ParseRemoteFilerTime("20260917250000") == 0,
              "hour 25 is refused rather than wrapped");
        // Before the epoch there is nothing a file display can show.
        Check(ParseRemoteFilerTime("19600917100000") == 0,
              "a pre-epoch year is refused");

        // Ordering is what the date column sorts by, so later must compare
        // greater across both formats.
        Check(ParseRemoteFilerTime("20260917100001") >
              ParseRemoteFilerTime("20260917100000"),
              "one second later compares greater");
        Check(ParseRemoteFilerTime("Thu, 17 Sep 2026 11:00:00 GMT") >
              ParseRemoteFilerTime("20260917100000"),
              "an hour later compares greater across the two formats");
    }

    if (failures) {
        std::printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("\nall checks passed\n");
    return 0;
}
