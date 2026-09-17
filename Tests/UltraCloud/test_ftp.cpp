// Tests/UltraCloud/test_ftp.cpp
// FTP helpers: transports, URLs (the trailing slash that decides list from
// retrieve), listing entries, error mapping; and the provider's calls against
// a fake FTP seam. Headless - no server is contacted.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include <UltraCloud/UltraCloudFtp.h>
#include <UltraCloud/UltraCloudWebDav.h>   // the "not a drive" side of the new verbs

#include <memory>
#include <string>
#include <vector>

using namespace UltraCloud;

namespace {

// An account rooted at the server's top level, and one rooted in a subfolder.
Account PlainAccount() {
    Account a;
    a.providerId = "ftp";
    a.serverUrl = "ftp://files.example.org";
    a.username = "erika";
    return a;
}

Credentials PasswordCredentials() {
    Credentials c;
    c.username = "erika";
    c.password = "pw";
    return c;
}

UltraNetFtpEntry MakeEntry(const std::string& name, UltraNetFtpEntryType type,
                           int64_t size = 0, const std::string& modified = "") {
    UltraNetFtpEntry e;
    e.name = name;
    e.type = type;
    e.size = size;
    e.modificationTime = modified;
    return e;
}

UltraNetResult NetOk() {
    UltraNetResult r;
    r.code = UltraNetResultCode::Success;
    r.success = true;
    return r;
}

UltraNetResult NetError(UltraNetResultCode code, const std::string& message = "") {
    UltraNetResult r;
    r.code = code;
    r.success = false;
    r.message = message;
    return r;
}

// What the provider asked the seam to do.
struct Capture {
    std::string listUrl;
    std::string deleteUrl;
    std::string rmdirUrl;
    std::string mkdirUrl;
    std::string renameUrl;
    std::string renameNewName;
    std::string downloadUrl;
    std::string downloadLocal;
    std::string uploadUrl;
    std::string uploadLocal;
    std::string user;
    std::string password;
    bool useTls = false;
    bool implicitTls = false;
    bool createMissingDirs = false;
    std::vector<UltraNetFtpEntry> listing;
};

// A seam that succeeds and records. `listing` is what List() gets back.
FtpOps FakeOps(Capture& cap) {
    FtpOps ops;
    auto note = [&cap](const UltraNetFtpOptions& opt) {
        cap.user = opt.credentials.username;
        cap.password = opt.credentials.password;
        cap.useTls = opt.useTls;
        cap.implicitTls = opt.implicitTls;
        cap.createMissingDirs = opt.createMissingDirs;
    };
    ops.list = [&cap, note](const std::string& url, std::vector<UltraNetFtpEntry>& out,
                            const UltraNetFtpOptions& opt) {
        cap.listUrl = url;
        note(opt);
        out = cap.listing;
        return NetOk();
    };
    ops.deleteFile = [&cap, note](const std::string& url, const UltraNetFtpOptions& opt) {
        cap.deleteUrl = url; note(opt); return NetOk();
    };
    ops.removeDirectory = [&cap, note](const std::string& url, const UltraNetFtpOptions& opt) {
        cap.rmdirUrl = url; note(opt); return NetOk();
    };
    ops.createDirectory = [&cap, note](const std::string& url, const UltraNetFtpOptions& opt) {
        cap.mkdirUrl = url; note(opt); return NetOk();
    };
    ops.rename = [&cap, note](const std::string& url, const std::string& newName,
                              const UltraNetFtpOptions& opt) {
        cap.renameUrl = url; cap.renameNewName = newName; note(opt); return NetOk();
    };
    ops.download = [&cap, note](const std::string& url, const std::string& localPath,
                                const UltraNetFtpOptions& opt) {
        cap.downloadUrl = url; cap.downloadLocal = localPath; note(opt); return NetOk();
    };
    ops.upload = [&cap, note](const std::string& localPath, const std::string& url,
                              const UltraNetFtpOptions& opt) {
        cap.uploadUrl = url; cap.uploadLocal = localPath; note(opt); return NetOk();
    };
    return ops;
}

} // namespace

TEST(ftp_transport_from_scheme) {
    // Plain, and a URL that carries no scheme at all.
    REQUIRE_EQ(FtpTransportFor("ftp://h").scheme, std::string("ftp"));
    REQUIRE(!FtpTransportFor("ftp://h").useTls);
    REQUIRE_EQ(FtpTransportFor("files.example.org").scheme, std::string("ftp"));
    REQUIRE(!FtpTransportFor("files.example.org").useTls);

    // Implicit FTPS keeps the ftps:// scheme on the wire.
    const FtpTransport implicitTls = FtpTransportFor("FTPS://h");
    REQUIRE_EQ(implicitTls.scheme, std::string("ftps"));
    REQUIRE(implicitTls.useTls);
    REQUIRE(implicitTls.implicitTls);

    // Explicit FTPS travels as ftp:// with TLS asked for afterwards.
    const FtpTransport explicitTls = FtpTransportFor("ftpes://h");
    REQUIRE_EQ(explicitTls.scheme, std::string("ftp"));
    REQUIRE(explicitTls.useTls);
    REQUIRE(!explicitTls.implicitTls);

    // SFTP is SSH, not FTP with TLS.
    const FtpTransport sftp = FtpTransportFor("sftp://h");
    REQUIRE_EQ(sftp.scheme, std::string("sftp"));
    REQUIRE(!sftp.useTls);
}

TEST(ftp_host_and_base) {
    REQUIRE_EQ(FtpHostAndBase("ftp://files.example.org"),
               std::string("files.example.org"));
    REQUIRE_EQ(FtpHostAndBase("ftp://files.example.org/"),
               std::string("files.example.org"));
    REQUIRE_EQ(FtpHostAndBase("ftps://files.example.org/srv/pub/"),
               std::string("files.example.org/srv/pub"));
    REQUIRE_EQ(FtpHostAndBase("files.example.org"), std::string("files.example.org"));
    REQUIRE_EQ(FtpHostAndBase(""), std::string(""));
}

TEST(ftp_url_trailing_slash_decides_list_from_retrieve) {
    Account a = PlainAccount();

    // A folder ends in '/' so libcurl lists it; the same path named as an
    // entry must not, or the commands that cut off the last segment to find
    // the parent (DELE, RMD, RNFR, MKD) have nothing left to name.
    REQUIRE_EQ(FtpUrl(a, "/Docs", true), std::string("ftp://files.example.org/Docs/"));
    REQUIRE_EQ(FtpUrl(a, "/Docs", false), std::string("ftp://files.example.org/Docs"));

    // The root lists as the bare host with one slash, and never grows a second.
    REQUIRE_EQ(FtpUrl(a, "/", true), std::string("ftp://files.example.org/"));
    REQUIRE_EQ(FtpUrl(a, "", true), std::string("ftp://files.example.org/"));

    // Paths are percent-encoded segment by segment, keeping the separators.
    REQUIRE_EQ(FtpUrl(a, "/Q3 report/final draft.pdf", false),
               std::string("ftp://files.example.org/Q3%20report/final%20draft.pdf"));

    // An account rooted in a subfolder hangs everything off that base, and
    // the scheme follows the transport (ftpes -> ftp).
    Account sub = PlainAccount();
    sub.serverUrl = "ftpes://files.example.org/srv/pub";
    REQUIRE_EQ(FtpUrl(sub, "/a", true), std::string("ftp://files.example.org/srv/pub/a/"));

    Account sftp = PlainAccount();
    sftp.serverUrl = "sftp://files.example.org";
    REQUIRE_EQ(FtpUrl(sftp, "/a", false), std::string("sftp://files.example.org/a"));

    // No server, no URL.
    Account empty;
    REQUIRE_EQ(FtpUrl(empty, "/a", true), std::string(""));
}

TEST(ftp_entry_mapping_skips_dot_entries) {
    Entry out;
    REQUIRE(!FtpEntryToEntry(MakeEntry(".", UltraNetFtpEntryType::Directory), "/", out));
    REQUIRE(!FtpEntryToEntry(MakeEntry("..", UltraNetFtpEntryType::Directory), "/", out));
    REQUIRE(!FtpEntryToEntry(MakeEntry("", UltraNetFtpEntryType::File), "/", out));

    // A file at the root: one slash, not two.
    REQUIRE(FtpEntryToEntry(
        MakeEntry("a.txt", UltraNetFtpEntryType::File, 12, "20260917100000"), "/", out));
    REQUIRE_EQ(out.path, std::string("/a.txt"));
    REQUIRE_EQ(out.size, (int64_t)12);
    REQUIRE_EQ(out.modified, std::string("20260917100000"));
    REQUIRE(!out.isDirectory);

    // Deeper, and with the folder given unnormalised.
    REQUIRE(FtpEntryToEntry(MakeEntry("b.txt", UltraNetFtpEntryType::File), "/Docs/", out));
    REQUIRE_EQ(out.path, std::string("/Docs/b.txt"));

    // A directory's reported size means nothing and is dropped.
    REQUIRE(FtpEntryToEntry(
        MakeEntry("Archive", UltraNetFtpEntryType::Directory, 4096), "/", out));
    REQUIRE(out.isDirectory);
    REQUIRE_EQ(out.size, (int64_t)0);

    // A symlink is reported as a file rather than probed.
    REQUIRE(FtpEntryToEntry(MakeEntry("link", UltraNetFtpEntryType::Symlink), "/", out));
    REQUIRE(!out.isDirectory);
}

TEST(ftp_error_mapping) {
    REQUIRE(FromFtp(NetOk(), "list").IsOk());
    REQUIRE(FromFtp(NetError(UltraNetResultCode::AuthenticationFailed), "x").code ==
            ResultCode::AuthFailed);
    REQUIRE(FromFtp(NetError(UltraNetResultCode::AccessDenied), "x").code ==
            ResultCode::AuthFailed);
    REQUIRE(FromFtp(NetError(UltraNetResultCode::NotFound), "x").code ==
            ResultCode::NotFound);
    REQUIRE(FromFtp(NetError(UltraNetResultCode::HostNotFound), "x").code ==
            ResultCode::Network);
    REQUIRE(FromFtp(NetError(UltraNetResultCode::ConnectionTimeout), "x").code ==
            ResultCode::Network);
    REQUIRE(FromFtp(NetError(UltraNetResultCode::TlsCertificateInvalid), "x").code ==
            ResultCode::Network);
    REQUIRE(FromFtp(NetError(UltraNetResultCode::UnsupportedScheme), "x").code ==
            ResultCode::InvalidArgument);
    REQUIRE(FromFtp(NetError(UltraNetResultCode::InvalidUrl), "x").code ==
            ResultCode::InvalidArgument);
    // Anything the transport could not place: the server answered, and said no.
    REQUIRE(FromFtp(NetError(UltraNetResultCode::HttpError), "x").code ==
            ResultCode::Server);

    // The operation is named in the message, with the server's reason kept.
    const Result r = FromFtp(NetError(UltraNetResultCode::NotFound, "550 no such file"),
                             "download /a.txt");
    REQUIRE(r.message.find("download /a.txt") != std::string::npos);
    REQUIRE(r.message.find("550 no such file") != std::string::npos);
}

TEST(ftp_capabilities_is_a_drive_without_share_links) {
    const ProviderCapabilities c = FtpProvider().Capabilities();
    REQUIRE(c.browse);
    REQUIRE(c.upload);
    REQUIRE(c.modify);            // the point: it can delete and rename
    REQUIRE(!c.shareLinks);
    REQUIRE(c.needsServerUrl);
    REQUIRE(!c.needsOAuth);
    REQUIRE_EQ(FtpProvider().Id(), std::string("ftp"));
}

TEST(ftp_list_maps_and_sorts_folders_first) {
    Capture cap;
    cap.listing = {
        MakeEntry("zebra.txt", UltraNetFtpEntryType::File, 3),
        MakeEntry(".", UltraNetFtpEntryType::Directory),
        MakeEntry("..", UltraNetFtpEntryType::Directory),
        MakeEntry("Archive", UltraNetFtpEntryType::Directory),
        MakeEntry("apple.txt", UltraNetFtpEntryType::File, 1),
        MakeEntry("beta", UltraNetFtpEntryType::Directory),
    };
    FtpProvider ftp(FakeOps(cap));
    std::vector<Entry> out;
    REQUIRE(ftp.List(PlainAccount(), PasswordCredentials(), "/Docs", out));

    // Listed as a folder, so the URL ends in '/'.
    REQUIRE_EQ(cap.listUrl, std::string("ftp://files.example.org/Docs/"));
    // The credentials reached the seam.
    REQUIRE_EQ(cap.user, std::string("erika"));
    REQUIRE_EQ(cap.password, std::string("pw"));

    // "." and ".." dropped; folders first, then by name, case-insensitively.
    REQUIRE_EQ(out.size(), (size_t)4);
    REQUIRE_EQ(out[0].name, std::string("Archive"));
    REQUIRE_EQ(out[1].name, std::string("beta"));
    REQUIRE_EQ(out[2].name, std::string("apple.txt"));
    REQUIRE_EQ(out[3].name, std::string("zebra.txt"));
    REQUIRE_EQ(out[0].path, std::string("/Docs/Archive"));
}

TEST(ftp_list_replaces_what_the_caller_passed_in) {
    Capture cap;
    cap.listing = { MakeEntry("a.txt", UltraNetFtpEntryType::File) };
    FtpProvider ftp(FakeOps(cap));
    std::vector<Entry> out;
    out.resize(3);   // stale contents must not survive a successful listing
    REQUIRE(ftp.List(PlainAccount(), PasswordCredentials(), "/", out));
    REQUIRE_EQ(out.size(), (size_t)1);
    REQUIRE_EQ(out[0].name, std::string("a.txt"));
}

TEST(ftp_verify_lists_the_root) {
    Capture cap;
    FtpProvider ftp(FakeOps(cap));
    REQUIRE(ftp.Verify(PlainAccount(), PasswordCredentials()));
    REQUIRE_EQ(cap.listUrl, std::string("ftp://files.example.org/"));
}

TEST(ftp_verify_reports_a_rejected_password_as_auth_failed) {
    FtpOps ops;
    ops.list = [](const std::string&, std::vector<UltraNetFtpEntry>&,
                  const UltraNetFtpOptions&) {
        return NetError(UltraNetResultCode::AuthenticationFailed, "530 Login incorrect");
    };
    FtpProvider ftp(std::move(ops));
    const Result r = ftp.Verify(PlainAccount(), PasswordCredentials());
    REQUIRE(r.code == ResultCode::AuthFailed);
}

TEST(ftp_delete_picks_rmd_for_a_folder_and_dele_for_a_file) {
    Capture cap;
    FtpProvider ftp(FakeOps(cap));

    REQUIRE(ftp.Delete(PlainAccount(), PasswordCredentials(), "/Docs/a.txt", false));
    REQUIRE_EQ(cap.deleteUrl, std::string("ftp://files.example.org/Docs/a.txt"));
    REQUIRE_EQ(cap.rmdirUrl, std::string(""));

    REQUIRE(ftp.Delete(PlainAccount(), PasswordCredentials(), "/Docs/Archive", true));
    REQUIRE_EQ(cap.rmdirUrl, std::string("ftp://files.example.org/Docs/Archive"));
}

TEST(ftp_delete_refuses_the_root) {
    Capture cap;
    FtpProvider ftp(FakeOps(cap));
    REQUIRE(ftp.Delete(PlainAccount(), PasswordCredentials(), "/", true).code ==
            ResultCode::InvalidArgument);
    REQUIRE(ftp.Delete(PlainAccount(), PasswordCredentials(), "", true).code ==
            ResultCode::InvalidArgument);
    // Nothing was sent.
    REQUIRE_EQ(cap.rmdirUrl, std::string(""));
    REQUIRE_EQ(cap.deleteUrl, std::string(""));
}

TEST(ftp_rename_is_in_place_only) {
    Capture cap;
    FtpProvider ftp(FakeOps(cap));

    REQUIRE(ftp.Rename(PlainAccount(), PasswordCredentials(), "/Docs/a.txt", "b.txt"));
    REQUIRE_EQ(cap.renameUrl, std::string("ftp://files.example.org/Docs/a.txt"));
    REQUIRE_EQ(cap.renameNewName, std::string("b.txt"));

    // A name with a separator would be a move, which this does not do.
    cap.renameNewName.clear();
    REQUIRE(ftp.Rename(PlainAccount(), PasswordCredentials(), "/a.txt", "sub/b.txt").code ==
            ResultCode::InvalidArgument);
    REQUIRE(ftp.Rename(PlainAccount(), PasswordCredentials(), "/a.txt", "").code ==
            ResultCode::InvalidArgument);
    REQUIRE(ftp.Rename(PlainAccount(), PasswordCredentials(), "/", "x").code ==
            ResultCode::InvalidArgument);
    REQUIRE_EQ(cap.renameNewName, std::string(""));
}

TEST(ftp_mkdir_upload_and_download_name_the_entry) {
    Capture cap;
    FtpProvider ftp(FakeOps(cap));
    const Account a = PlainAccount();
    const Credentials c = PasswordCredentials();

    // MKD is issued at the parent, so the URL must not end in '/'.
    REQUIRE(ftp.MakeDirectory(a, c, "/Docs/New"));
    REQUIRE_EQ(cap.mkdirUrl, std::string("ftp://files.example.org/Docs/New"));

    REQUIRE(ftp.Download(a, c, "/Docs/a.txt", "/tmp/a.txt"));
    REQUIRE_EQ(cap.downloadUrl, std::string("ftp://files.example.org/Docs/a.txt"));
    REQUIRE_EQ(cap.downloadLocal, std::string("/tmp/a.txt"));

    REQUIRE(ftp.Upload(a, c, "/tmp/a.txt", "/Docs/a.txt"));
    REQUIRE_EQ(cap.uploadUrl, std::string("ftp://files.example.org/Docs/a.txt"));
    REQUIRE_EQ(cap.uploadLocal, std::string("/tmp/a.txt"));
    // Uploading into a folder that is not there yet is the common case.
    REQUIRE(cap.createMissingDirs);
}

TEST(ftp_tls_flags_reach_the_seam) {
    Capture cap;
    FtpProvider ftp(FakeOps(cap));
    Account a = PlainAccount();
    a.serverUrl = "ftps://files.example.org";
    REQUIRE(ftp.Verify(a, PasswordCredentials()));
    REQUIRE(cap.useTls);
    REQUIRE(cap.implicitTls);

    a.serverUrl = "ftpes://files.example.org";
    REQUIRE(ftp.Verify(a, PasswordCredentials()));
    REQUIRE(cap.useTls);
    REQUIRE(!cap.implicitTls);
}

TEST(ftp_falls_back_to_the_account_username) {
    Capture cap;
    FtpProvider ftp(FakeOps(cap));
    Credentials c;               // no username of its own, only a password
    c.password = "pw";
    REQUIRE(ftp.Verify(PlainAccount(), c));
    REQUIRE_EQ(cap.user, std::string("erika"));
}

TEST(ftp_without_a_server_url_is_an_argument_error) {
    Capture cap;
    FtpProvider ftp(FakeOps(cap));
    Account a;                   // no serverUrl
    const Credentials c = PasswordCredentials();
    std::vector<Entry> entries;
    REQUIRE(ftp.Verify(a, c).code == ResultCode::InvalidArgument);
    REQUIRE(ftp.List(a, c, "/", entries).code == ResultCode::InvalidArgument);
    REQUIRE(ftp.MakeDirectory(a, c, "/x").code == ResultCode::InvalidArgument);
    REQUIRE(ftp.Delete(a, c, "/x", false).code == ResultCode::InvalidArgument);
    REQUIRE(ftp.Rename(a, c, "/x", "y").code == ResultCode::InvalidArgument);
    REQUIRE(ftp.Upload(a, c, "/tmp/x", "/x").code == ResultCode::InvalidArgument);
    REQUIRE(ftp.Download(a, c, "/x", "/tmp/x").code == ResultCode::InvalidArgument);
    // Nothing was attempted.
    REQUIRE_EQ(cap.listUrl, std::string(""));
}

TEST(ftp_has_no_share_links) {
    Capture cap;
    FtpProvider ftp(FakeOps(cap));
    ShareLink link;
    REQUIRE(ftp.CreateShareLink(PlainAccount(), PasswordCredentials(), "/a.txt", {},
                                link).code == ResultCode::Unsupported);
}

TEST(ftp_is_registered_as_a_built_in_provider) {
    RegisterBuiltInProviders();
    const std::shared_ptr<ICloudProvider> p = GetProvider("ftp");
    REQUIRE(p != nullptr);
    REQUIRE_EQ(p->Id(), std::string("ftp"));
    REQUIRE(p->Capabilities().modify);
}

TEST(providers_that_are_not_drives_report_unsupported) {
    // The two new verbs default to Unsupported, so the providers that cannot
    // change what is on the server say so rather than appearing to succeed.
    WebDavProvider dav;
    Account a; a.serverUrl = "https://dav.example.org";
    Credentials c; c.password = "pw";
    REQUIRE(dav.Delete(a, c, "/x", false).code == ResultCode::Unsupported);
    REQUIRE(dav.Rename(a, c, "/x", "y").code == ResultCode::Unsupported);
    REQUIRE(!dav.Capabilities().modify);
}
