// Tests/UltraCloud/test_uploads.cpp
// Uploads to a server that writes them as they arrive (UltraCloudUploads.h):
// the part-name rules, putting a finished upload in place on servers that do
// and do not rename onto a file, the pending-upload records in the account
// store, and CloudService doing all of it through the FTP provider against
// a fake FTP server - one that can refuse renames, deletes or dot names, hide
// dot names from a listing, break an upload part way, stop answering once an
// upload is in, or not answer at all. Headless - no server is contacted.
// Version: 0.2.0 - a complete upload whose rename the connection cut off is
//                  given its name, replacing or beside the file there
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraCloud/UltraCloudFtp.h>
#include <UltraCloud/UltraCloudLog.h>
#include <UltraCloud/UltraCloudSecrets.h>
#include <UltraCloud/UltraCloudService.h>
#include <UltraCloud/UltraCloudUploads.h>

#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCloud;

namespace {

// ---- A fake FTP server -------------------------------------------------------

struct FakeFtpServer {
    std::map<std::string, std::string> files;   // server path -> contents
    bool overwritesOnRename = true;   // a rename onto a name that exists replaces it
    bool renames = true;
    bool deletes = true;
    bool refusesDotNames = false;     // STOR of ".x" answers 553
    bool hidesDotNames = false;       // LIST leaves ".x" out (vsftpd's default)
    bool down = false;                // nothing answers
    bool downAfterUpload = false;     // ... from the moment an upload is in
    bool callsRenamesOff = false;     // the caller's abort check stops a rename
    long breakUploadAfter = -1;       // bytes in, then the connection resets
    std::vector<std::string> commands;
    int lists = 0;                    // listings asked for, answered or not
};

std::string PathOfUrl(const std::string& url) {
    const std::string::size_type scheme = url.find("://");
    const std::string::size_type slash = url.find('/', scheme + 3);
    std::string path = slash == std::string::npos ? "/" : url.substr(slash);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

std::string ParentOf(const std::string& path) {
    const std::string::size_type slash = path.find_last_of('/');
    return slash == 0 ? "/" : path.substr(0, slash);
}

std::string NameOf(const std::string& path) {
    return path.substr(path.find_last_of('/') + 1);
}

UltraNetResult Ok() {
    UltraNetResult r;
    r.code = UltraNetResultCode::Ok;
    r.success = true;
    return r;
}

UltraNetResult Fail(UltraNetResultCode code, const std::string& message) {
    UltraNetResult r;
    r.code = code;
    r.success = false;
    r.message = message;
    return r;
}

void Say(const UltraNetFtpOptions& opt, UltraNetFtpLogKind kind, const std::string& text,
         int code = 0) {
    if (!opt.onLog) return;
    UltraNetFtpLogLine line;
    line.kind = kind;
    line.text = text;
    line.replyCode = code;
    opt.onLog(line);
}

FtpOps FakeOps(FakeFtpServer& srv) {
    FtpOps ops;
    ops.list = [&srv](const std::string& url, std::vector<UltraNetFtpEntry>& out,
                      const UltraNetFtpOptions&) {
        out.clear();
        ++srv.lists;
        if (srv.down) return Fail(UltraNetResultCode::ConnectionRefused, "connection refused");
        const std::string folder = PathOfUrl(url);
        for (const auto& [path, contents] : srv.files) {
            if (ParentOf(path) != folder) continue;
            const std::string name = NameOf(path);
            if (srv.hidesDotNames && name[0] == '.') continue;
            UltraNetFtpEntry e;
            e.name = name;
            e.type = UltraNetFtpEntryType::File;
            e.size = static_cast<int64_t>(contents.size());
            out.push_back(e);
        }
        return Ok();
    };
    ops.upload = [&srv](const std::string& localPath, const std::string& url,
                        const UltraNetFtpOptions& opt) {
        if (srv.down) return Fail(UltraNetResultCode::ConnectionRefused, "connection refused");
        const std::string path = PathOfUrl(url);
        const bool sftp = url.rfind("sftp://", 0) == 0;
        srv.commands.push_back("STOR " + path);
        if (!sftp) Say(opt, UltraNetFtpLogKind::Command, "STOR " + NameOf(path));
        if (srv.refusesDotNames && NameOf(path)[0] == '.') {
            if (!sftp) Say(opt, UltraNetFtpLogKind::Response, "553 Could not create file.", 553);
            return Fail(UltraNetResultCode::Unknown, "553 Could not create file.");
        }
        if (!sftp) Say(opt, UltraNetFtpLogKind::Response, "150 Ok to send data.", 150);
        std::ifstream in(UltraCanvas::PathFromUtf8(localPath), std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (srv.breakUploadAfter >= 0 && static_cast<size_t>(srv.breakUploadAfter) < data.size()) {
            srv.files[path] = data.substr(0, static_cast<size_t>(srv.breakUploadAfter));
            return Fail(UltraNetResultCode::ConnectionReset, "connection reset");
        }
        srv.files[path] = data;
        if (!sftp) Say(opt, UltraNetFtpLogKind::Response, "226 Transfer complete.", 226);
        if (srv.downAfterUpload) srv.down = true;
        return Ok();
    };
    ops.rename = [&srv](const std::string& url, const std::string& newName,
                        const UltraNetFtpOptions&) {
        if (srv.down) return Fail(UltraNetResultCode::ConnectionRefused, "connection refused");
        const std::string from = PathOfUrl(url);
        const std::string parent = ParentOf(from);
        const std::string to = (parent == "/" ? "" : parent) + "/" + newName;
        if (srv.callsRenamesOff) return Fail(UltraNetResultCode::Cancelled, "called off");
        srv.commands.push_back("RNFR " + from + " RNTO " + to);
        if (!srv.renames) return Fail(UltraNetResultCode::Unknown, "550 Permission denied.");
        if (!srv.files.count(from)) return Fail(UltraNetResultCode::Unknown, "550 No such file.");
        if (srv.files.count(to) && !srv.overwritesOnRename)
            return Fail(UltraNetResultCode::Unknown, "553 File exists.");
        srv.files[to] = srv.files[from];
        srv.files.erase(from);
        return Ok();
    };
    ops.deleteFile = [&srv](const std::string& url, const UltraNetFtpOptions&) {
        if (srv.down) return Fail(UltraNetResultCode::ConnectionRefused, "connection refused");
        const std::string path = PathOfUrl(url);
        srv.commands.push_back("DELE " + path);
        if (!srv.deletes) return Fail(UltraNetResultCode::Unknown, "550 Permission denied.");
        if (!srv.files.erase(path)) return Fail(UltraNetResultCode::Unknown, "550 No such file.");
        return Ok();
    };
    ops.createDirectory = [](const std::string&, const UltraNetFtpOptions&) { return Ok(); };
    ops.removeDirectory = [](const std::string&, const UltraNetFtpOptions&) { return Ok(); };
    ops.download = [](const std::string&, const std::string&, const UltraNetFtpOptions&) {
        return Ok();
    };
    return ops;
}

bool Sent(const FakeFtpServer& srv, const std::string& command) {
    for (const std::string& c : srv.commands)
        if (c == command) return true;
    return false;
}

// ---- The setting each test runs in ---------------------------------------------

// A local file to upload, removed with the test.
struct LocalFile {
    std::filesystem::path path;
    explicit LocalFile(const std::string& contents, const std::string& name = "big.bin") {
        path = std::filesystem::temp_directory_path() /
               ("uctest-uploads-" + std::to_string(std::time(nullptr)) + "-" + name);
        std::ofstream out(path, std::ios::binary);
        out << contents;
    }
    ~LocalFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    std::string Utf8() const { return UltraCanvas::PathToUtf8(path); }
};

// A service whose "ftp" provider talks to `srv`. The built-in provider is put
// back when it goes, so the next test does not find a fake bound to a server
// that is gone.
struct Setting {
    AccountStore accounts;
    MemorySecretStore secrets;
    std::unique_ptr<CloudService> service;
    Account account;
    std::vector<std::string> log;
    LogCallback previousLog;

    Setting(FakeFtpServer& srv, const std::string& connection,
            const std::string& username = "erika",
            const std::string& serverUrl = "ftp://files.example.org") {
        RegisterProvider(std::make_shared<FtpProvider>(FakeOps(srv)));
        accounts.Open(connection, ":memory:");
        service = std::make_unique<CloudService>(accounts, secrets);
        account.providerId = "ftp";
        account.serverUrl = serverUrl;
        account.username = username;
        Credentials c;
        c.password = "pw";
        service->AddAccount(account, c, /*verify=*/false);
        previousLog = SetThreadLog([this](const LogLine& line) { log.push_back(line.text); });
    }
    ~Setting() {
        SetThreadLog(std::move(previousLog));
        RegisterProvider(std::make_shared<FtpProvider>());
    }
    std::vector<PendingUpload> Pending() {
        std::vector<PendingUpload> out;
        accounts.ListPendingUploads(account.accountId, static_cast<int64_t>(std::time(nullptr)), out);
        return out;
    }
    bool Said(const std::string& part) const {
        for (const std::string& l : log)
            if (l.find(part) != std::string::npos) return true;
        return false;
    }
};

const std::string kNew(4096, 'n');

int64_t Now() { return static_cast<int64_t>(std::time(nullptr)); }

// A folder: names to contents, behind the two operations PutUploadInPlace and
// PlaceCompletePart use. Answers `answerFor` steps, then nothing.
struct Folder {
    std::map<std::string, std::string> files;
    bool overwrites = true, renames = true, deletes = true;
    int answerFor = 1000;
    int said = 0;
    InPlaceOps Ops() {
        InPlaceOps ops;
        ops.rename = [this](const std::string& path, const std::string& to, bool& unreachable) {
            if (answerFor-- <= 0) { unreachable = true; return false; }
            const std::string from = NameOf(path);
            if (!renames || !files.count(from)) return false;
            if (files.count(to) && !overwrites) return false;
            files[to] = files[from];
            files.erase(from);
            return true;
        };
        ops.remove = [this](const std::string& path, bool& unreachable) {
            if (answerFor-- <= 0) { unreachable = true; return false; }
            return deletes && files.erase(NameOf(path)) > 0;
        };
        ops.say = [this](const std::string&) { ++said; };
        return ops;
    }
};
using Files = std::map<std::string, std::string>;

} // namespace

// ---- The pure parts -----------------------------------------------------------

TEST(uploads_part_names_and_anonymous_logins) {
    REQUIRE_EQ(UploadPartName("report.pdf"), std::string(".report.pdf.part"));
    REQUIRE_EQ(UploadNameOfPart(".report.pdf.part"), std::string("report.pdf"));
    REQUIRE(UploadNameOfPart("report.pdf").empty());
    REQUIRE(UploadNameOfPart(".part").empty());
    REQUIRE_EQ(UploadSetAsideName("report.pdf"), std::string(".report.pdf.replaced"));
    REQUIRE_EQ(UploadPartName(std::string(249, 'x')).size(), (size_t)255);
    REQUIRE(UploadPartName(std::string(250, 'x')).empty());
    REQUIRE(UploadPartName("").empty());

    REQUIRE(IsAnonymousFtpLogin("ftp://example.com", ""));
    REQUIRE(IsAnonymousFtpLogin("ftps://example.com", "Anonymous"));
    REQUIRE(IsAnonymousFtpLogin("ftp://example.com", "ftp"));
    REQUIRE(!IsAnonymousFtpLogin("ftp://example.com", "erika"));
    REQUIRE(!IsAnonymousFtpLogin("sftp://example.com", ""));
    REQUIRE(!IsAnonymousFtpLogin("SFTP://example.com", "anonymous"));

    PendingUpload atName;
    atName.fileSize = 100;
    REQUIRE(IsLeftByPendingUpload(atName, 0));
    REQUIRE(IsLeftByPendingUpload(atName, 99));
    REQUIRE(!IsLeftByPendingUpload(atName, 100));
    REQUIRE(!IsLeftByPendingUpload(atName, 101));
    PendingUpload atPart = atName;
    atPart.partName = true;
    REQUIRE(IsLeftByPendingUpload(atPart, 100));   // complete, never renamed into place
}

TEST(uploads_put_in_place_on_servers_that_do_and_do_not_rename_onto_a_file) {
    const std::string part = UploadPartName("a.txt");
    const std::string aside = UploadSetAsideName("a.txt");

    Folder overwriting;
    overwriting.files = {{"a.txt", "old"}, {part, "new"}};
    REQUIRE(PutUploadInPlace("/up", "a.txt", overwriting.Ops()) == InPlace::Done);
    REQUIRE(overwriting.files == (Files{{"a.txt", "new"}}));

    Folder sftp;
    sftp.overwrites = false;
    sftp.files = {{"a.txt", "old"}, {part, "new"}, {"b.txt", "b"}};
    REQUIRE(PutUploadInPlace("", "a.txt", sftp.Ops()) == InPlace::Done);
    REQUIRE(sftp.files == (Files{{"a.txt", "new"}, {"b.txt", "b"}}));
    REQUIRE_EQ(sftp.said, 1);

    Folder fresh;
    fresh.overwrites = false;
    fresh.files = {{part, "new"}};
    REQUIRE(PutUploadInPlace("", "a.txt", fresh.Ops()) == InPlace::Done);
    REQUIRE(fresh.files == (Files{{"a.txt", "new"}}));

    Folder noRenames;
    noRenames.renames = false;
    noRenames.files = {{"a.txt", "old"}, {part, "new"}};
    REQUIRE(PutUploadInPlace("", "a.txt", noRenames.Ops()) == InPlace::Refused);
    REQUIRE(noRenames.files == (Files{{"a.txt", "old"}, {part, "new"}}));

    Folder stuck;
    stuck.overwrites = false;
    stuck.deletes = false;
    stuck.files = {{"a.txt", "old"}, {part, "new"}};
    REQUIRE(PutUploadInPlace("", "a.txt", stuck.Ops()) == InPlace::Done);
    REQUIRE(stuck.files == (Files{{"a.txt", "new"}, {aside, "old"}}));
    REQUIRE_EQ(stuck.said, 2);

    Folder dropped;
    dropped.overwrites = false;
    dropped.answerFor = 2;   // the first rename and the set-aside; then nothing
    dropped.files = {{"a.txt", "old"}, {part, "new"}};
    REQUIRE(PutUploadInPlace("", "a.txt", dropped.Ops()) == InPlace::Unreachable);

    Folder dead;
    dead.answerFor = 0;
    dead.files = {{part, "new"}};
    REQUIRE(PutUploadInPlace("", "a.txt", dead.Ops()) == InPlace::Unreachable);
    REQUIRE(dead.files == (Files{{part, "new"}}));
}

TEST(uploads_a_complete_part_is_told_apart_and_given_its_place) {
    const int64_t now = 1760000000;
    PendingUpload u{"nas", "/up/.a.txt.part", true, 100, now - 50};
    REQUIRE(!IsCompletePart(u, true, 99));
    REQUIRE(IsCompletePart(u, true, 100));
    REQUIRE(!IsCompletePart(u, false, 0));          // hidden: not known
    u.completedAt = now - 10;
    REQUIRE(IsCompletePart(u, false, 0));           // ... unless the upload said so
    PendingUpload atName = u;
    atName.partName = false;
    REQUIRE(!IsCompletePart(atName, true, 100));

    REQUIRE(CompletePartReplaces(u, now));
    REQUIRE(!CompletePartReplaces(u, now - 10 + kCompleteUploadReplacesSeconds));
    u.completedAt = 0;                               // the start stands in for the end
    REQUIRE(CompletePartReplaces(u, now));
    REQUIRE(!CompletePartReplaces(u, now - 50 + kCompleteUploadReplacesSeconds));

    auto none = [](const std::string&) { return false; };
    REQUIRE_EQ(UploadKeptName("a.txt", none), std::string("a (2).txt"));
    REQUIRE_EQ(UploadKeptName(".bashrc", none), std::string(".bashrc (2)"));
    REQUIRE_EQ(UploadKeptName("a.txt", [](const std::string& n) { return n == "a (2).txt"; }),
               std::string("a (3).txt"));

    const std::string part = UploadPartName("a.txt");
    const std::string aside = UploadSetAsideName("a.txt");

    Folder free;
    free.files = {{part, "new"}, {"b", "b"}};
    REQUIRE(PlaceCompletePart("", "a.txt", AtName::Nothing, false, false, "a (2).txt",
                              free.Ops()) == CompletePart::Named);
    REQUIRE(free.files == (Files{{"a.txt", "new"}, {"b", "b"}}));

    // Cut off part way through replacing: the old file was set aside.
    Folder halfway;
    halfway.overwrites = false;
    halfway.files = {{part, "new"}, {aside, "old"}};
    REQUIRE(PlaceCompletePart("", "a.txt", AtName::Nothing, true, false, "a (2).txt",
                              halfway.Ops()) == CompletePart::Named);
    REQUIRE(halfway.files == (Files{{"a.txt", "new"}}));

    Folder recent;
    recent.overwrites = false;
    recent.files = {{part, "new"}, {"a.txt", "old"}};
    REQUIRE(PlaceCompletePart("", "a.txt", AtName::File, false, true, "a (2).txt",
                              recent.Ops()) == CompletePart::Replaced);
    REQUIRE(recent.files == (Files{{"a.txt", "new"}}));

    Folder later;
    later.files = {{part, "new"}, {"a.txt", "written since"}};
    REQUIRE(PlaceCompletePart("", "a.txt", AtName::File, false, false, "a (2).txt",
                              later.Ops()) == CompletePart::KeptBeside);
    REQUIRE(later.files == (Files{{"a.txt", "written since"}, {"a (2).txt", "new"}}));

    Folder folder;   // a folder took the name: never replaced
    folder.files = {{part, "new"}};
    REQUIRE(PlaceCompletePart("", "a.txt", AtName::Folder, false, true, "a (2).txt",
                              folder.Ops()) == CompletePart::KeptBeside);
    REQUIRE(folder.files == (Files{{"a (2).txt", "new"}}));

    Folder noRenames;
    noRenames.renames = false;
    noRenames.files = {{part, "new"}, {aside, "old"}};
    REQUIRE(PlaceCompletePart("", "a.txt", AtName::Nothing, true, false, "a (2).txt",
                              noRenames.Ops()) == CompletePart::Refused);
    REQUIRE(noRenames.files == (Files{{part, "new"}, {aside, "old"}}));

    Folder dead;
    dead.answerFor = 0;
    dead.files = {{part, "new"}, {"a.txt", "old"}};
    REQUIRE(PlaceCompletePart("", "a.txt", AtName::File, false, false, "a (2).txt",
                              dead.Ops()) == CompletePart::Unreachable);
    REQUIRE(dead.files == (Files{{part, "new"}, {"a.txt", "old"}}));
}

TEST(uploads_the_account_store_keeps_pending_uploads_and_direct_accounts) {
    AccountStore accounts;
    REQUIRE(accounts.Open("uctest-uploads-store", ":memory:"));
    const int64_t now = 1760000000;
    REQUIRE(accounts.AddPendingUpload({"nas", "/up/.a.part", true, 10, now - 5}));
    REQUIRE(accounts.AddPendingUpload({"nas", "/up/b", false, 20, now}));
    REQUIRE(accounts.AddPendingUpload({"nas", "/up/b", false, 30, now}));   // replaces
    REQUIRE(accounts.AddPendingUpload({"other", "/x", false, 1, now}));
    REQUIRE(accounts.AddPendingUpload({"nas", "/old", false, 1,
                                       now - kPendingUploadMaxAgeSeconds - 1}));
    std::vector<PendingUpload> out;
    REQUIRE(accounts.ListPendingUploads("nas", now, out));
    REQUIRE_EQ(out.size(), (size_t)2);   // the old one is gone
    REQUIRE_EQ(out[0].remotePath, std::string("/up/.a.part"));
    REQUIRE(out[0].partName);
    REQUIRE_EQ(out[0].fileSize, (uint64_t)10);
    REQUIRE_EQ(out[1].fileSize, (uint64_t)30);
    REQUIRE(!out[1].partName);
    REQUIRE_EQ(out[0].completedAt, (int64_t)0);
    REQUIRE(accounts.AddPendingUpload({"nas", "/up/.a.part", true, 10, now - 5, now - 1}));
    REQUIRE(accounts.ListPendingUploads("nas", now, out));
    REQUIRE_EQ(out[0].completedAt, now - 1);
    REQUIRE(accounts.RemovePendingUpload("nas", "/up/b"));
    REQUIRE(accounts.ListPendingUploads("nas", now, out));
    REQUIRE_EQ(out.size(), (size_t)1);

    REQUIRE(!accounts.UploadsDirect("nas", "ftp://h", now));
    REQUIRE(accounts.SetUploadsDirect("nas", "ftp://h", now));
    REQUIRE(accounts.UploadsDirect("nas", "ftp://h", now + 10));
    REQUIRE(!accounts.UploadsDirect("nas", "ftp://other", now + 10));   // another server
    REQUIRE(!accounts.UploadsDirect("nas", "ftp://h", now + 10));       // ... and forgotten
    REQUIRE(accounts.SetUploadsDirect("nas", "ftp://h", now));
    REQUIRE(!accounts.UploadsDirect("nas", "ftp://h", now + kPendingUploadMaxAgeSeconds + 1));
}

// ---- Through the service --------------------------------------------------------

TEST(uploads_go_by_part_name_and_leave_the_old_file_whole_until_complete) {
    FakeFtpServer srv;
    srv.files["/up/big.bin"] = "the old version";
    Setting s(srv, "uctest-uploads-part");
    LocalFile local(kNew);
    REQUIRE(s.service->Upload(s.account.accountId, local.Utf8(), "/up/big.bin"));
    REQUIRE(Sent(srv, "STOR /up/.big.bin.part"));
    REQUIRE(Sent(srv, "RNFR /up/.big.bin.part RNTO /up/big.bin"));
    REQUIRE_EQ(srv.files.size(), (size_t)1);
    REQUIRE(srv.files["/up/big.bin"] == kNew);
    REQUIRE(s.Pending().empty());
    REQUIRE(s.Said("Uploading as \".big.bin.part\""));
}

TEST(uploads_onto_a_server_that_will_not_rename_over_a_file_set_the_old_one_aside) {
    FakeFtpServer srv;
    srv.overwritesOnRename = false;   // SFTP
    srv.files["/up/big.bin"] = "the old version";
    Setting s(srv, "uctest-uploads-aside", "erika", "sftp://files.example.org");
    LocalFile local(kNew);
    REQUIRE(s.service->Upload(s.account.accountId, local.Utf8(), "/up/big.bin"));
    REQUIRE(Sent(srv, "RNFR /up/big.bin RNTO /up/.big.bin.replaced"));
    REQUIRE_EQ(srv.files.size(), (size_t)1);
    REQUIRE(srv.files["/up/big.bin"] == kNew);
    REQUIRE(s.Pending().empty());
}

TEST(uploads_with_an_anonymous_login_go_straight_to_the_name) {
    FakeFtpServer srv;
    Setting s(srv, "uctest-uploads-anon", "anonymous");
    LocalFile local(kNew);
    REQUIRE(s.service->Upload(s.account.accountId, local.Utf8(), "/up/big.bin"));
    REQUIRE_EQ(srv.commands.size(), (size_t)1);
    REQUIRE(Sent(srv, "STOR /up/big.bin"));
    REQUIRE(s.Pending().empty());
}

TEST(uploads_a_server_that_refuses_renames_gets_the_file_again_and_later_ones_direct) {
    FakeFtpServer srv;
    srv.renames = false;
    Setting s(srv, "uctest-uploads-norename");
    LocalFile local(kNew);
    REQUIRE(s.service->Upload(s.account.accountId, local.Utf8(), "/up/big.bin"));
    REQUIRE(Sent(srv, "DELE /up/.big.bin.part"));
    REQUIRE(Sent(srv, "STOR /up/big.bin"));
    REQUIRE_EQ(srv.files.size(), (size_t)1);
    REQUIRE(srv.files["/up/big.bin"] == kNew);
    REQUIRE(s.accounts.UploadsDirect(s.account.accountId, s.account.serverUrl,
                                     static_cast<int64_t>(std::time(nullptr))));
    srv.commands.clear();
    REQUIRE(s.service->Upload(s.account.accountId, local.Utf8(), "/up/second.bin"));
    REQUIRE_EQ(srv.commands.size(), (size_t)1);
    REQUIRE(Sent(srv, "STOR /up/second.bin"));
}

TEST(uploads_a_copy_that_cannot_be_renamed_or_removed_is_kept_as_pending) {
    FakeFtpServer srv;
    srv.renames = false;
    srv.deletes = false;
    Setting s(srv, "uctest-uploads-stuck");
    LocalFile local(kNew);
    REQUIRE(s.service->Upload(s.account.accountId, local.Utf8(), "/up/big.bin"));
    REQUIRE(srv.files.count("/up/.big.bin.part"));
    const std::vector<PendingUpload> pending = s.Pending();
    REQUIRE_EQ(pending.size(), (size_t)1);
    REQUIRE_EQ(pending[0].remotePath, std::string("/up/.big.bin.part"));
    // The next call asks again, is refused again, and leaves it for the
    // next run rather than asking before every call.
    std::vector<Entry> entries;
    srv.commands.clear();
    REQUIRE(s.service->List(s.account.accountId, "/up", entries));
    REQUIRE(Sent(srv, "DELE /up/.big.bin.part"));
    srv.commands.clear();
    REQUIRE(s.service->List(s.account.accountId, "/up", entries));
    REQUIRE(!Sent(srv, "DELE /up/.big.bin.part"));
    REQUIRE_EQ(s.Pending().size(), (size_t)1);
    // ... and the next run, a server that has changed its mind, removes it.
    srv.deletes = true;
    CloudService nextRun(s.accounts, s.secrets);
    REQUIRE(nextRun.List(s.account.accountId, "/up", entries));
    REQUIRE(!srv.files.count("/up/.big.bin.part"));
    REQUIRE(s.Pending().empty());
}

TEST(uploads_a_part_left_by_a_broken_upload_is_removed_before_the_next_call) {
    FakeFtpServer srv;
    srv.files["/up/big.bin"] = "the old version";
    srv.breakUploadAfter = 1000;
    Setting s(srv, "uctest-uploads-broken");
    LocalFile local(kNew);
    REQUIRE(!s.service->Upload(s.account.accountId, local.Utf8(), "/up/big.bin"));
    REQUIRE(srv.files["/up/big.bin"] == "the old version");
    REQUIRE_EQ(srv.files["/up/.big.bin.part"].size(), (size_t)1000);
    REQUIRE_EQ(s.Pending().size(), (size_t)1);
    // The next run - a process killed during the upload leaves the same.
    CloudService nextRun(s.accounts, s.secrets);
    std::vector<Entry> entries;
    REQUIRE(nextRun.List(s.account.accountId, "/up", entries));
    REQUIRE(!srv.files.count("/up/.big.bin.part"));
    REQUIRE(srv.files["/up/big.bin"] == "the old version");
    REQUIRE_EQ(entries.size(), (size_t)1);
    REQUIRE_EQ(entries[0].name, std::string("big.bin"));
    REQUIRE(s.Pending().empty());
}

TEST(uploads_a_part_the_listing_hides_is_removed_by_name) {
    FakeFtpServer srv;
    srv.hidesDotNames = true;
    srv.breakUploadAfter = 10;
    Setting s(srv, "uctest-uploads-hidden");
    LocalFile local(kNew);
    REQUIRE(!s.service->Upload(s.account.accountId, local.Utf8(), "/up/big.bin"));
    std::vector<Entry> entries;
    REQUIRE(s.service->List(s.account.accountId, "/", entries));
    REQUIRE(!srv.files.count("/up/.big.bin.part"));
    REQUIRE(s.Said("removing it by name"));
    REQUIRE(s.Pending().empty());
}

TEST(uploads_a_hidden_part_the_server_will_not_delete_stays_noted) {
    // vsftpd's default: dot names left out of the listing, and an anonymous-
    // style account that may upload but not delete or rename. The copy left
    // behind is noted, asked about once a run, and kept noted.
    FakeFtpServer srv;
    srv.hidesDotNames = true;
    srv.renames = false;
    srv.deletes = false;
    Setting s(srv, "uctest-uploads-hidden-stuck");
    LocalFile local(kNew);
    REQUIRE(s.service->Upload(s.account.accountId, local.Utf8(), "/up/big.bin"));
    REQUIRE(srv.files.count("/up/.big.bin.part"));
    std::vector<Entry> entries;
    REQUIRE(s.service->List(s.account.accountId, "/up", entries));
    REQUIRE_EQ(s.Pending().size(), (size_t)1);
    CloudService nextRun(s.accounts, s.secrets);
    REQUIRE(nextRun.List(s.account.accountId, "/up", entries));
    REQUIRE_EQ(s.Pending().size(), (size_t)1);
    srv.deletes = true;
    CloudService thirdRun(s.accounts, s.secrets);
    REQUIRE(thirdRun.List(s.account.accountId, "/up", entries));
    REQUIRE(!srv.files.count("/up/.big.bin.part"));
    REQUIRE(s.Pending().empty());
}

TEST(uploads_a_part_the_server_never_created_is_not_noted) {
    FakeFtpServer srv;
    srv.down = true;   // nothing answers, so no STOR is ever accepted
    Setting s(srv, "uctest-uploads-never");
    LocalFile local(kNew);
    REQUIRE(!s.service->Upload(s.account.accountId, local.Utf8(), "/up/big.bin"));
    REQUIRE(s.Pending().empty());
}

TEST(uploads_a_part_name_the_server_refuses_falls_back_to_the_name) {
    FakeFtpServer srv;
    srv.refusesDotNames = true;
    Setting s(srv, "uctest-uploads-nodot");
    LocalFile local(kNew);
    REQUIRE(s.service->Upload(s.account.accountId, local.Utf8(), "/up/big.bin"));
    REQUIRE(Sent(srv, "STOR /up/.big.bin.part"));
    REQUIRE(Sent(srv, "STOR /up/big.bin"));
    REQUIRE(srv.files["/up/big.bin"] == kNew);
    REQUIRE(s.Pending().empty());
}

TEST(uploads_straight_to_the_name_are_pending_once_the_server_created_the_file) {
    FakeFtpServer srv;
    srv.files["/up/small.txt"] = "a small file of the user's";
    Setting s(srv, "uctest-uploads-straight", "anonymous");
    LocalFile local(kNew);
    // Refused before the server got as far as the file: nothing noted, and
    // the small file at that name is never touched.
    srv.down = true;
    REQUIRE(!s.service->Upload(s.account.accountId, local.Utf8(), "/up/small.txt"));
    REQUIRE(s.Pending().empty());
    srv.down = false;
    // Broken after the server created it: noted, and the part removed next
    // time - shorter than the whole file.
    srv.breakUploadAfter = 100;
    REQUIRE(!s.service->Upload(s.account.accountId, local.Utf8(), "/up/new.bin"));
    REQUIRE_EQ(s.Pending().size(), (size_t)1);
    srv.breakUploadAfter = -1;
    std::vector<Entry> entries;
    CloudService nextRun(s.accounts, s.secrets);
    REQUIRE(nextRun.List(s.account.accountId, "/up", entries));
    REQUIRE(!srv.files.count("/up/new.bin"));
    REQUIRE(srv.files.count("/up/small.txt"));
    // A file of the full size at that name by then is kept.
    s.accounts.AddPendingUpload({s.account.accountId, "/up/full.bin", false,
                                 kNew.size(), static_cast<int64_t>(std::time(nullptr))});
    srv.files["/up/full.bin"] = kNew;
    CloudService thirdRun(s.accounts, s.secrets);
    REQUIRE(thirdRun.List(s.account.accountId, "/up", entries));
    REQUIRE(srv.files.count("/up/full.bin"));
    REQUIRE(s.Pending().empty());
}

TEST(uploads_straight_to_a_free_name_over_sftp_are_pending_from_the_start) {
    // SFTP shows no STOR, so a name that is free before the upload counts as
    // the upload's from its first write: one stopped before any byte leaves
    // an empty file, which is removed.
    FakeFtpServer srv;
    srv.files["/up/kept.txt"] = "kept";
    Setting s(srv, "uctest-uploads-sftp", "erika", "sftp://files.example.org");
    s.accounts.SetUploadsDirect(s.account.accountId, s.account.serverUrl,
                                static_cast<int64_t>(std::time(nullptr)));
    LocalFile local(kNew);
    srv.breakUploadAfter = 0;
    REQUIRE(!s.service->Upload(s.account.accountId, local.Utf8(), "/up/new.bin"));
    REQUIRE(srv.files.count("/up/new.bin"));
    REQUIRE_EQ(s.Pending().size(), (size_t)1);
    // A name already taken is not noted: the upload may never have opened it.
    REQUIRE(!s.service->Upload(s.account.accountId, local.Utf8(), "/up/kept.txt"));
    std::vector<Entry> entries;
    CloudService nextRun(s.accounts, s.secrets);
    REQUIRE(nextRun.List(s.account.accountId, "/up", entries));
    REQUIRE(!srv.files.count("/up/new.bin"));
    REQUIRE(srv.files.count("/up/kept.txt"));
    REQUIRE(s.Pending().empty());
}

TEST(uploads_a_cleanup_the_server_did_not_answer_runs_once_a_call_gets_through) {
    FakeFtpServer srv;
    srv.breakUploadAfter = 10;
    Setting s(srv, "uctest-uploads-down");
    LocalFile local(kNew);
    REQUIRE(!s.service->Upload(s.account.accountId, local.Utf8(), "/up/big.bin"));
    srv.breakUploadAfter = -1;
    CloudService nextRun(s.accounts, s.secrets);
    std::vector<Entry> entries;
    srv.down = true;
    srv.lists = 0;
    REQUIRE(!nextRun.List(s.account.accountId, "/up", entries));
    REQUIRE_EQ(srv.lists, 2);        // the cleanup's look, then the call's own
    srv.lists = 0;
    REQUIRE(!nextRun.List(s.account.accountId, "/up", entries));
    REQUIRE_EQ(srv.lists, 1);        // not asked before every call while it waits
    srv.down = false;
    REQUIRE(nextRun.List(s.account.accountId, "/up", entries));
    REQUIRE(!srv.files.count("/up/.big.bin.part"));   // straight after that call
    REQUIRE(s.Pending().empty());
}

TEST(uploads_the_old_file_set_aside_when_the_line_went_is_put_back) {
    FakeFtpServer srv;
    srv.files["/up/.big.bin.replaced"] = "the old version";
    srv.files["/up/.big.bin.part"] = kNew.substr(0, 100);
    Setting s(srv, "uctest-uploads-putback");
    s.accounts.AddPendingUpload({s.account.accountId, "/up/.big.bin.part", true, kNew.size(),
                                 Now()});
    CloudService nextRun(s.accounts, s.secrets);
    std::vector<Entry> entries;
    REQUIRE(nextRun.List(s.account.accountId, "/up", entries));
    REQUIRE(srv.files["/up/big.bin"] == "the old version");
    REQUIRE_EQ(srv.files.size(), (size_t)1);
}

// ---- A complete upload whose rename the connection cut off ----------------------

TEST(uploads_a_complete_upload_cut_off_before_its_rename_is_given_its_name) {
    FakeFtpServer srv;
    srv.downAfterUpload = true;
    Setting s(srv, "uctest-uploads-complete");
    LocalFile local(kNew);
    const Result r = s.service->Upload(s.account.accountId, local.Utf8(), "/up/big.bin");
    REQUIRE(!r);
    REQUIRE(r.code == ResultCode::Network);
    REQUIRE(r.message.find("given that name") != std::string::npos);
    REQUIRE(srv.files["/up/.big.bin.part"] == kNew);
    std::vector<PendingUpload> pending = s.Pending();
    REQUIRE_EQ(pending.size(), (size_t)1);
    REQUIRE(pending[0].completedAt != 0);
    // Still down: it waits, and is not removed.
    std::vector<Entry> entries;
    REQUIRE(!s.service->List(s.account.accountId, "/up", entries));
    REQUIRE(srv.files.count("/up/.big.bin.part"));
    srv.down = false;
    srv.downAfterUpload = false;
    REQUIRE(s.service->List(s.account.accountId, "/up", entries));
    REQUIRE_EQ(srv.files.size(), (size_t)1);
    REQUIRE(srv.files["/up/big.bin"] == kNew);
    REQUIRE(s.Pending().empty());
    REQUIRE(s.Said("\"big.bin\" is in place"));
}

TEST(uploads_a_rename_called_off_is_not_taken_for_a_server_that_refuses_renames) {
    // The application quitting just as the upload is in: the rename is
    // called off. Nothing was refused - the account keeps its part names,
    // nothing is sent again, and the upload is put in place next time.
    FakeFtpServer srv;
    srv.files["/up/big.bin"] = "the old version";
    srv.callsRenamesOff = true;
    Setting s(srv, "uctest-uploads-complete-calledoff");
    LocalFile local(kNew);
    const Result r = s.service->Upload(s.account.accountId, local.Utf8(), "/up/big.bin");
    REQUIRE(r.code == ResultCode::Cancelled);
    REQUIRE(r.message.find("was called off") != std::string::npos);
    REQUIRE(!s.accounts.UploadsDirect(s.account.accountId, s.account.serverUrl, Now()));
    REQUIRE(!Sent(srv, "STOR /up/big.bin"));
    REQUIRE(!Sent(srv, "DELE /up/.big.bin.part"));
    REQUIRE(srv.files["/up/big.bin"] == "the old version");
    REQUIRE_EQ(s.Pending().size(), (size_t)1);
    REQUIRE(s.Pending()[0].completedAt != 0);
    srv.callsRenamesOff = false;
    CloudService nextRun(s.accounts, s.secrets);
    std::vector<Entry> entries;
    REQUIRE(nextRun.List(s.account.accountId, "/up", entries));
    REQUIRE_EQ(srv.files.size(), (size_t)1);
    REQUIRE(srv.files["/up/big.bin"] == kNew);
    REQUIRE(s.Pending().empty());
}

TEST(uploads_a_complete_upload_cut_off_while_replacing_replaces_the_file_soon_after) {
    // vsftpd: dot names hidden, so the part is found by the record alone.
    FakeFtpServer srv;
    srv.hidesDotNames = true;
    srv.files["/up/big.bin"] = "the old version";
    srv.downAfterUpload = true;
    Setting s(srv, "uctest-uploads-complete-replace");
    LocalFile local(kNew);
    REQUIRE(!s.service->Upload(s.account.accountId, local.Utf8(), "/up/big.bin"));
    REQUIRE(srv.files["/up/big.bin"] == "the old version");
    srv.down = false;
    srv.downAfterUpload = false;
    CloudService nextRun(s.accounts, s.secrets);   // a record from a run that ended
    std::vector<Entry> entries;
    REQUIRE(nextRun.List(s.account.accountId, "/up", entries));
    REQUIRE_EQ(srv.files.size(), (size_t)1);
    REQUIRE(srv.files["/up/big.bin"] == kNew);
    REQUIRE_EQ(entries.size(), (size_t)1);
    REQUIRE(s.Pending().empty());
}

TEST(uploads_a_complete_upload_found_later_is_kept_beside_the_file_there) {
    FakeFtpServer srv;
    srv.files["/up/big.bin"] = "written since";
    srv.files["/up/big (2).bin"] = "another";
    srv.files["/up/.big.bin.part"] = kNew;
    Setting s(srv, "uctest-uploads-complete-beside");
    const int64_t now = Now();
    s.accounts.AddPendingUpload({s.account.accountId, "/up/.big.bin.part", true, kNew.size(),
                                 now - 3 * 3600, now - 2 * 3600});
    CloudService nextRun(s.accounts, s.secrets);
    std::vector<Entry> entries;
    REQUIRE(nextRun.List(s.account.accountId, "/up", entries));
    REQUIRE(srv.files["/up/big.bin"] == "written since");
    REQUIRE(srv.files["/up/big (3).bin"] == kNew);
    REQUIRE(!srv.files.count("/up/.big.bin.part"));
    REQUIRE(s.Pending().empty());
    REQUIRE(s.Said("kept beside it as \"big (3).bin\""));
}

TEST(uploads_a_whole_part_a_run_left_without_a_word_is_given_its_name) {
    // The process ended between the last byte and the rename: the record
    // does not say complete, the listing's size does.
    FakeFtpServer srv;
    srv.files["/up/.big.bin.part"] = kNew;
    Setting s(srv, "uctest-uploads-complete-killed");
    s.accounts.AddPendingUpload({s.account.accountId, "/up/.big.bin.part", true, kNew.size(),
                                 Now()});
    CloudService nextRun(s.accounts, s.secrets);
    std::vector<Entry> entries;
    REQUIRE(nextRun.List(s.account.accountId, "/up", entries));
    REQUIRE_EQ(srv.files.size(), (size_t)1);
    REQUIRE(srv.files["/up/big.bin"] == kNew);
    REQUIRE(s.Said("It is all there"));
}

TEST(uploads_a_complete_part_the_server_will_not_rename_is_removed) {
    FakeFtpServer srv;
    srv.files["/up/.big.bin.part"] = kNew;
    srv.renames = false;
    Setting s(srv, "uctest-uploads-complete-refused");
    s.accounts.AddPendingUpload({s.account.accountId, "/up/.big.bin.part", true, kNew.size(),
                                 Now(), Now()});
    CloudService nextRun(s.accounts, s.secrets);
    std::vector<Entry> entries;
    REQUIRE(nextRun.List(s.account.accountId, "/up", entries));
    REQUIRE(srv.files.empty());
    REQUIRE(s.Pending().empty());
    REQUIRE(s.Said("has to be sent again"));
}
