// UltraCloud/providers/UltraCloudFtp.cpp
// Version: 0.1.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudFtp.h>

// NormalizePath / EncodePath / JoinUrl are generic path helpers that happen to
// be declared with the WebDAV provider, which is where they were first needed.
// Reused rather than written again: they are tested on their own, and two
// copies of percent-encoding is how the two drift apart.
#include <UltraCloud/UltraCloudWebDav.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace UltraCloud {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The scheme of `url` in lower case, "" when it carries none.
std::string SchemeOf(const std::string& url) {
    const std::string::size_type sep = url.find("://");
    if (sep == std::string::npos) return "";
    return Lower(url.substr(0, sep));
}

} // namespace

// ---- Pure helpers -----------------------------------------------------------

FtpTransport FtpTransportFor(const std::string& serverUrl) {
    FtpTransport t;
    const std::string scheme = SchemeOf(serverUrl);
    if (scheme == "ftps") {
        // TLS before anything else is said.
        t.scheme = "ftps";
        t.useTls = true;
        t.implicitTls = true;
    } else if (scheme == "ftpes") {
        // Explicit FTPS travels as an ordinary ftp:// URL; the TLS is asked
        // for with AUTH TLS once the control channel is open.
        t.scheme = "ftp";
        t.useTls = true;
        t.implicitTls = false;
    } else if (scheme == "sftp") {
        t.scheme = "sftp";
    } else {
        t.scheme = "ftp";   // "ftp", and anything unrecognised or absent
    }
    return t;
}

std::string FtpHostAndBase(const std::string& serverUrl) {
    std::string s = serverUrl;
    const std::string::size_type sep = s.find("://");
    if (sep != std::string::npos) s.erase(0, sep + 3);
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

std::string FtpUrl(const Account& account, const std::string& path, bool directory) {
    const FtpTransport transport = FtpTransportFor(account.serverUrl);
    const std::string hostAndBase = FtpHostAndBase(account.serverUrl);
    if (hostAndBase.empty()) return "";

    std::string url = transport.scheme + "://" +
                      JoinUrl(hostAndBase, EncodePath(NormalizePath(path)));
    // JoinUrl already ends the root in '/'; anything deeper needs one added.
    if (directory && !url.empty() && url.back() != '/') url.push_back('/');
    return url;
}

bool FtpEntryToEntry(const UltraNetFtpEntry& in, const std::string& folderPath,
                     Entry& out) {
    if (in.name.empty() || in.name == "." || in.name == "..") return false;

    const std::string folder = NormalizePath(folderPath);
    out.name = in.name;
    out.path = folder == "/" ? "/" + in.name : folder + "/" + in.name;
    out.isDirectory = in.type == UltraNetFtpEntryType::Directory;
    // A directory's size is whatever the server chose to report for the
    // directory entry itself, which means nothing to a caller.
    out.size = out.isDirectory ? 0 : in.size;
    out.modified = in.modificationTime;
    return true;
}

Result FromFtp(const UltraNetResult& net, const std::string& what) {
    if (net.success) return Result::Ok();

    const std::string message = net.message.empty() ? what : what + ": " + net.message;
    switch (net.code) {
        case UltraNetResultCode::AuthenticationRequired:
        case UltraNetResultCode::AuthenticationFailed:
        case UltraNetResultCode::AccessDenied:
            return Result::Error(ResultCode::AuthFailed, message, net.httpStatus);
        case UltraNetResultCode::NotFound:
            return Result::Error(ResultCode::NotFound, message, net.httpStatus);
        case UltraNetResultCode::InvalidUrl:
        case UltraNetResultCode::UnsupportedScheme:
            return Result::Error(ResultCode::InvalidArgument, message, net.httpStatus);
        case UltraNetResultCode::HostNotFound:
        case UltraNetResultCode::ConnectionRefused:
        case UltraNetResultCode::ConnectionReset:
        case UltraNetResultCode::ConnectionTimeout:
        case UltraNetResultCode::Timeout:
        case UltraNetResultCode::SendFailed:
        case UltraNetResultCode::ReceiveFailed:
        case UltraNetResultCode::TlsHandshakeFailed:
        case UltraNetResultCode::TlsCertificateInvalid:
        case UltraNetResultCode::TlsCertificateExpired:
            return Result::Error(ResultCode::Network, message, net.httpStatus);
        default:
            // An FTP reply the transport could not place (a 5xx from the
            // server, a quote command refused): the server spoke, and said no.
            return Result::Error(ResultCode::Server, message, net.httpStatus);
    }
}

// ---- The seam ---------------------------------------------------------------

FtpOps FtpOps::Default() {
    FtpOps ops;
    ops.list = [](const std::string& url, std::vector<UltraNetFtpEntry>& out,
                  const UltraNetFtpOptions& opt) {
        return UltraNet_FtpListDirectory(url, out, opt);
    };
    ops.download = [](const std::string& url, const std::string& localPath,
                      const UltraNetFtpOptions& opt) {
        return UltraNet_FtpDownload(url, localPath, opt);
    };
    ops.upload = [](const std::string& localPath, const std::string& url,
                    const UltraNetFtpOptions& opt) {
        return UltraNet_FtpUpload(localPath, url, opt);
    };
    ops.createDirectory = [](const std::string& url, const UltraNetFtpOptions& opt) {
        return UltraNet_FtpCreateDirectory(url, opt);
    };
    ops.removeDirectory = [](const std::string& url, const UltraNetFtpOptions& opt) {
        return UltraNet_FtpRemoveDirectory(url, opt);
    };
    ops.deleteFile = [](const std::string& url, const UltraNetFtpOptions& opt) {
        return UltraNet_FtpDelete(url, opt);
    };
    ops.rename = [](const std::string& url, const std::string& newName,
                    const UltraNetFtpOptions& opt) {
        return UltraNet_FtpRename(url, newName, opt);
    };
    return ops;
}

// ---- FtpProvider ------------------------------------------------------------

ProviderCapabilities FtpProvider::Capabilities() const {
    ProviderCapabilities c;
    c.browse = true;
    c.upload = true;
    c.modify = true;            // DELE / RMD / RNFR+RNTO
    c.shareLinks = false;       // nothing on an FTP server mints one
    c.needsServerUrl = true;
    c.needsOAuth = false;       // a password, typed into the account dialog
    return c;
}

UltraNetFtpOptions FtpProvider::OptionsFor(const Account& account,
                                           const Credentials& credentials) const {
    const FtpTransport transport = FtpTransportFor(account.serverUrl);
    UltraNetFtpOptions options;
    options.credentials.type = UltraNetAuthType::Basic;
    options.credentials.username =
            credentials.username.empty() ? account.username : credentials.username;
    options.credentials.password = credentials.password;
    options.useTls = transport.useTls;
    options.implicitTls = transport.implicitTls;
    return options;
}

Result FtpProvider::Verify(const Account& account, const Credentials& credentials) {
    if (account.serverUrl.empty())
        return Result::Error(ResultCode::InvalidArgument, "no server URL configured");
    if (!ops_.list)
        return Result::Error(ResultCode::Unsupported, "no FTP listing function");

    // Listing the account's root is the cheapest thing that proves both the
    // connection and the credentials. What comes back is thrown away.
    std::vector<UltraNetFtpEntry> entries;
    const UltraNetResult net =
            ops_.list(FtpUrl(account, "/", true), entries, OptionsFor(account, credentials));
    return FromFtp(net, "sign in");
}

Result FtpProvider::List(const Account& account, const Credentials& credentials,
                         const std::string& path, std::vector<Entry>& out) {
    if (account.serverUrl.empty())
        return Result::Error(ResultCode::InvalidArgument, "no server URL configured");
    if (!ops_.list)
        return Result::Error(ResultCode::Unsupported, "no FTP listing function");

    std::vector<UltraNetFtpEntry> entries;
    const UltraNetResult net = ops_.list(FtpUrl(account, path, true), entries,
                                         OptionsFor(account, credentials));
    if (!net.success) return FromFtp(net, "list " + NormalizePath(path));

    out.clear();
    out.reserve(entries.size());
    for (const UltraNetFtpEntry& in : entries) {
        Entry e;
        if (FtpEntryToEntry(in, path, e)) out.push_back(std::move(e));
    }
    // Folders first, then by name - the order the WebDAV provider returns, so
    // a caller showing two accounts side by side gets one ordering.
    std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        return Lower(a.name) < Lower(b.name);
    });
    return Result::Ok();
}

Result FtpProvider::MakeDirectory(const Account& account, const Credentials& credentials,
                                  const std::string& path) {
    if (account.serverUrl.empty())
        return Result::Error(ResultCode::InvalidArgument, "no server URL configured");
    if (!ops_.createDirectory)
        return Result::Error(ResultCode::Unsupported, "no FTP mkdir function");

    // Named, not listed: MKD is issued at the parent, which is derived by
    // cutting the URL at its last '/'.
    const UltraNetResult net = ops_.createDirectory(FtpUrl(account, path, false),
                                                    OptionsFor(account, credentials));
    return FromFtp(net, "create " + NormalizePath(path));
}

Result FtpProvider::Upload(const Account& account, const Credentials& credentials,
                           const std::string& localPath, const std::string& remotePath) {
    if (account.serverUrl.empty())
        return Result::Error(ResultCode::InvalidArgument, "no server URL configured");
    if (localPath.empty())
        return Result::Error(ResultCode::InvalidArgument, "no local file given");
    if (!ops_.upload)
        return Result::Error(ResultCode::Unsupported, "no FTP upload function");

    UltraNetFtpOptions options = OptionsFor(account, credentials);
    // The folders of a path that is not there yet: uploading into a new
    // subfolder is the common case, and one request beats mkdir per level.
    options.createMissingDirs = true;
    const UltraNetResult net = ops_.upload(localPath, FtpUrl(account, remotePath, false),
                                           options);
    return FromFtp(net, "upload " + NormalizePath(remotePath));
}

Result FtpProvider::Download(const Account& account, const Credentials& credentials,
                             const std::string& remotePath, const std::string& localPath) {
    if (account.serverUrl.empty())
        return Result::Error(ResultCode::InvalidArgument, "no server URL configured");
    if (localPath.empty())
        return Result::Error(ResultCode::InvalidArgument, "no local file given");
    if (!ops_.download)
        return Result::Error(ResultCode::Unsupported, "no FTP download function");

    const UltraNetResult net = ops_.download(FtpUrl(account, remotePath, false), localPath,
                                             OptionsFor(account, credentials));
    return FromFtp(net, "download " + NormalizePath(remotePath));
}

Result FtpProvider::Delete(const Account& account, const Credentials& credentials,
                           const std::string& path, bool isDirectory) {
    if (account.serverUrl.empty())
        return Result::Error(ResultCode::InvalidArgument, "no server URL configured");
    const std::string normalized = NormalizePath(path);
    if (normalized == "/")
        return Result::Error(ResultCode::InvalidArgument, "refusing to delete the root");

    // A file is DELE and a folder is RMD; the server rejects the wrong one,
    // so the caller's `isDirectory` is what picks.
    const FtpOps::PathFn& fn = isDirectory ? ops_.removeDirectory : ops_.deleteFile;
    if (!fn) return Result::Error(ResultCode::Unsupported, "no FTP delete function");

    const UltraNetResult net = fn(FtpUrl(account, path, false),
                                  OptionsFor(account, credentials));
    return FromFtp(net, "delete " + normalized);
}

Result FtpProvider::Rename(const Account& account, const Credentials& credentials,
                           const std::string& path, const std::string& newName) {
    if (account.serverUrl.empty())
        return Result::Error(ResultCode::InvalidArgument, "no server URL configured");
    if (newName.empty())
        return Result::Error(ResultCode::InvalidArgument, "no new name given");
    // RNTO takes a name, and a caller that passes a path means a move - which
    // this does not do, and must not do by accident.
    if (newName.find('/') != std::string::npos)
        return Result::Error(ResultCode::InvalidArgument, "a new name cannot contain '/'");
    if (NormalizePath(path) == "/")
        return Result::Error(ResultCode::InvalidArgument, "refusing to rename the root");
    if (!ops_.rename)
        return Result::Error(ResultCode::Unsupported, "no FTP rename function");

    const UltraNetResult net = ops_.rename(FtpUrl(account, path, false), newName,
                                           OptionsFor(account, credentials));
    return FromFtp(net, "rename " + NormalizePath(path));
}

Result FtpProvider::CreateShareLink(const Account& account, const Credentials& credentials,
                                    const std::string& remotePath,
                                    const ShareLinkOptions& options, ShareLink& out) {
    (void)account; (void)credentials; (void)remotePath; (void)options; (void)out;
    return Result::Error(ResultCode::Unsupported,
                         "an FTP server cannot create share links");
}

} // namespace UltraCloud
