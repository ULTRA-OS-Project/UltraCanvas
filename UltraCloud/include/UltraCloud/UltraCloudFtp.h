// UltraCloud/include/UltraCloud/UltraCloudFtp.h
// The FTP provider (id "ftp"): an FTP, FTPS or SFTP server as an UltraCloud
// account, so an app can carry one the way it carries a Nextcloud - one
// account record, the password in UltraVault, the same add-account dialog -
// instead of keeping its own host, user and password beside everything else.
// The transfers themselves are UltraNet's (UltraNetFtp.h, libcurl); this is
// the account-shaped surface over them.
//
// The one provider that can change what is on the server as well as read it:
// FTP has DELE, RMD and RNFR/RNTO, so `modify` is set and Delete / Rename are
// implemented rather than left Unsupported.
//
// Share links are not FTP's to give - there is no request that mints one -
// so CreateShareLink answers Unsupported.
// Version: 0.1.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudProvider.h"

#include <UltraNet/UltraNetFtp.h>

#include <functional>
#include <string>
#include <vector>

namespace UltraCloud {

// ---- Pure helpers (tested on their own) -------------------------------------

// What a server URL asks to speak. The scheme the user types is not always the
// scheme that goes on the wire: explicit FTPS is an ordinary "ftp://" URL with
// TLS negotiated by AUTH TLS, which is why `scheme` and `useTls` are separate.
//
//   ftp://     plain FTP
//   ftps://    FTPS, TLS from the first byte (implicit, conventionally :990)
//   ftpes://   FTPS, TLS negotiated on the control channel (explicit, :21)
//   sftp://    SFTP over SSH - a different protocol, not FTP with TLS
//
// An URL with no scheme at all is taken for plain FTP.
struct FtpTransport {
    std::string scheme = "ftp";   // what the URL handed to UltraNet carries
    bool useTls = false;
    bool implicitTls = false;
};
FtpTransport FtpTransportFor(const std::string& serverUrl);

// A server URL stripped of its scheme and of any trailing '/': "files.example
// .org", or "files.example.org/srv/pub" when the account is rooted in a
// subdirectory. Everything a path is then hung off.
std::string FtpHostAndBase(const std::string& serverUrl);

// The URL of `path` on `account`, percent-encoded segment by segment.
//
// `directory` decides the trailing slash, and it is not cosmetic: libcurl
// lists a URL that ends in '/' and retrieves one that does not, while the
// commands that work on an entry through its parent (DELE, RMD, RNFR/RNTO,
// MKD) derive that parent by cutting at the last '/' and fail outright on a
// URL that ends in one. So: true to list a folder, false to name an entry.
std::string FtpUrl(const Account& account, const std::string& path, bool directory);

// One listing line as UltraCloud's Entry, with `folderPath` the folder it was
// listed from. Returns false for the entries a caller never wants: "." and
// "..", and anything with no name.
//
// A symlink is reported as a file. Telling a link to a directory from a link
// to a file costs a round trip per entry, and a listing is not the place to
// spend it.
bool FtpEntryToEntry(const UltraNetFtpEntry& in, const std::string& folderPath, Entry& out);

// UltraNet's outcome as a Result. `what` names the operation for the message.
Result FromFtp(const UltraNetResult& net, const std::string& what);

// ---- The FTP seam -----------------------------------------------------------
// The UltraNet_Ftp* free functions by default, fakes in tests - the same seam
// HttpFn is for the HTTP providers, widened to a struct because FTP's verbs
// are separate entry points rather than one request function.
struct FtpOps {
    using ListFn = std::function<UltraNetResult(const std::string& url,
                                                std::vector<UltraNetFtpEntry>& out,
                                                const UltraNetFtpOptions&)>;
    using PathFn = std::function<UltraNetResult(const std::string& url,
                                                const UltraNetFtpOptions&)>;
    using MoveFn = std::function<UltraNetResult(const std::string& url,
                                                const std::string& newName,
                                                const UltraNetFtpOptions&)>;
    using DownloadFn = std::function<UltraNetResult(const std::string& url,
                                                    const std::string& localPath,
                                                    const UltraNetFtpOptions&)>;
    using UploadFn = std::function<UltraNetResult(const std::string& localPath,
                                                  const std::string& url,
                                                  const UltraNetFtpOptions&)>;

    ListFn     list;
    DownloadFn download;
    UploadFn   upload;
    PathFn     createDirectory;
    PathFn     removeDirectory;
    PathFn     deleteFile;
    MoveFn     rename;

    // The real UltraNet entry points.
    static FtpOps Default();
};

class FtpProvider : public ICloudProvider {
public:
    // A default-constructed FtpOps means the real UltraNet functions; tests
    // pass their own.
    explicit FtpProvider(FtpOps ops = FtpOps::Default()) : ops_(std::move(ops)) {}

    std::string Id() const override { return "ftp"; }
    std::string DisplayName() const override { return "FTP / SFTP server"; }
    ProviderCapabilities Capabilities() const override;

    Result Verify(const Account& account, const Credentials& credentials) override;
    Result List(const Account& account, const Credentials& credentials,
                const std::string& path, std::vector<Entry>& out) override;
    Result MakeDirectory(const Account& account, const Credentials& credentials,
                         const std::string& path) override;
    Result Upload(const Account& account, const Credentials& credentials,
                  const std::string& localPath, const std::string& remotePath) override;
    Result Download(const Account& account, const Credentials& credentials,
                    const std::string& remotePath, const std::string& localPath) override;
    Result Delete(const Account& account, const Credentials& credentials,
                  const std::string& path, bool isDirectory) override;
    Result Rename(const Account& account, const Credentials& credentials,
                  const std::string& path, const std::string& newName) override;
    Result CreateShareLink(const Account& account, const Credentials& credentials,
                           const std::string& remotePath,
                           const ShareLinkOptions& options, ShareLink& out) override;

protected:
    // The per-call options: the credentials, and the TLS the server URL asks
    // for.
    UltraNetFtpOptions OptionsFor(const Account& account,
                                  const Credentials& credentials) const;

    FtpOps ops_;
};

} // namespace UltraCloud
