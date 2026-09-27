// core/UltraNet/UltraNetFtpQuote.h
// The commands UltraNet sends to change something on an FTP or SFTP server:
// delete a file, remove / create a folder, rename. They travel as libcurl
// "quote" commands after the connection is set up, and this is where the
// command text is made from the URL of the thing acted on.
//
// Two things have to be right here that the URL does not give for free:
//
//   - The name. A URL carries its path percent-encoded ("My%20Photo.jpg"),
//     and libcurl decodes it for the transfers it runs itself (RETR, STOR,
//     LIST). A quote command is sent verbatim, so the name has to be decoded
//     here: "RNFR My%20Photo.jpg" asks the server for a file that does not
//     exist, and every name with a space, a bracket, a '+' or a letter
//     outside ASCII failed to rename or delete with a 550.
//
//   - The folder. libcurl sends quote commands straight after the login,
//     BEFORE it changes into the folder the URL names - so a bare name was
//     looked up in the login folder, whatever folder the entry was in. A
//     rename in a subfolder failed with a 550, a new folder appeared at the
//     top of the server instead of where it was made, and a delete in a
//     subfolder took a same-named file at the top instead. The commands now
//     carry the entry's path from the login folder, which is how libcurl
//     itself reads the path of an ftp:// URL (RFC 1738: relative to where the
//     login lands, "%2F" first for an absolute one).
//
//   - The dialect. RNFR / RNTO / DELE / RMD / MKD are FTP (and FTPS) commands.
//     An SFTP server speaks none of them; libcurl's SFTP backend takes its
//     own quote commands - "rename", "rm", "rmdir", "mkdir" - with full,
//     double-quoted paths. Sent the FTP words, every SFTP change failed.
//
// A name that holds a line break is refused rather than sent: on FTP it
// would end the command and start another one of the server's choosing.
//
// Header-only and free of libcurl, so Tests/UltraNetFtpQuoteTest.cpp can
// check it on any build.
// Version: 1.0.0
// Last Modified: 2026-09-27
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cctype>
#include <string>
#include <vector>

namespace ultranet_internal::ftpquote {

enum class Verb { Delete, RemoveDirectory, MakeDirectory, Rename };

// What to send: the URL of the folder the command runs in, and the commands.
struct Plan {
    std::string parentUrl;
    std::vector<std::string> commands;
};

// Percent-decoding of one URL path. False for a '%' not followed by two hex
// digits, which is a URL this module did not build.
inline bool PercentDecode(const std::string& in, std::string& out) {
    out.clear();
    out.reserve(in.size());
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '%') { out.push_back(in[i]); continue; }
        if (i + 2 >= in.size()) return false;
        const int hi = hex(in[i + 1]), lo = hex(in[i + 2]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
    }
    return true;
}

// A character that would end a command line - or, NUL, cut it short.
inline bool HasLineBreak(const std::string& s) {
    return s.find_first_of(std::string("\r\n\0", 3)) != std::string::npos;
}

inline bool IsSftpUrl(const std::string& url) {
    const std::string::size_type sep = url.find("://");
    if (sep == std::string::npos) return false;
    std::string scheme = url.substr(0, sep);
    for (char& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return scheme == "sftp";
}

// A path as libcurl's SFTP quote parser reads one: in double quotes, with a
// backslash before any '"' or '\' inside.
inline std::string SftpQuotePath(const std::string& path) {
    std::string out = "\"";
    for (char c : path) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

// Splits `url` into the folder it is in (still encoded - libcurl decodes a
// URL it connects to) and the path of the entry on the server (decoded).
// One trailing '/' is allowed, the way a folder's URL is often spelled.
inline bool SplitUrl(const std::string& url, std::string& parentUrl,
                     std::string& serverPath, std::string& error) {
    const std::string::size_type sep = url.find("://");
    if (sep == std::string::npos) { error = "not a URL"; return false; }
    const std::string::size_type pathStart = url.find('/', sep + 3);
    if (pathStart == std::string::npos) { error = "no path in the URL"; return false; }
    std::string trimmed = url;
    if (trimmed.size() > pathStart + 1 && trimmed.back() == '/') trimmed.pop_back();
    const std::string::size_type lastSlash = trimmed.find_last_of('/');
    if (lastSlash < pathStart || lastSlash + 1 >= trimmed.size()) {
        error = "the URL names the server's root, not something on it";
        return false;
    }
    parentUrl = trimmed.substr(0, lastSlash + 1);
    if (!PercentDecode(trimmed.substr(pathStart), serverPath)) {
        error = "the URL's path is not validly encoded";
        return false;
    }
    return true;
}

// Builds the commands for `verb` on the entry `url` names. `newName` is the
// Rename target: a bare name in the same folder, never a path.
inline bool Build(const std::string& url, Verb verb, const std::string& newName,
                  Plan& out, std::string& error) {
    out = Plan{};
    std::string serverPath;
    if (!SplitUrl(url, out.parentUrl, serverPath, error)) return false;

    const std::string::size_type cut = serverPath.find_last_of('/');
    const std::string name = serverPath.substr(cut + 1);
    const std::string folder = serverPath.substr(0, cut + 1);   // ends in '/'
    if (name.empty() || name == "." || name == "..") {
        error = "no name to act on";
        return false;
    }
    if (HasLineBreak(serverPath)) {
        error = "the name holds a line break";
        return false;
    }
    if (verb == Verb::Rename) {
        if (newName.empty() || newName == "." || newName == "..") {
            error = "no new name given";
            return false;
        }
        if (newName.find('/') != std::string::npos) {
            error = "a new name cannot contain '/'";
            return false;
        }
        if (HasLineBreak(newName)) {
            error = "the new name holds a line break";
            return false;
        }
    }

    if (IsSftpUrl(url)) {
        // Full paths. "/~/" is libcurl's spelling of the home folder in an
        // SFTP URL; the quote commands take a path relative to it instead,
        // which is where an SFTP session starts.
        std::string path = serverPath;
        std::string dir = folder;
        if (path.rfind("/~/", 0) == 0) { path.erase(0, 3); dir.erase(0, 3); }
        switch (verb) {
            case Verb::Delete:
                out.commands.push_back("rm " + SftpQuotePath(path)); break;
            case Verb::RemoveDirectory:
                out.commands.push_back("rmdir " + SftpQuotePath(path)); break;
            case Verb::MakeDirectory:
                out.commands.push_back("mkdir " + SftpQuotePath(path)); break;
            case Verb::Rename:
                out.commands.push_back("rename " + SftpQuotePath(path) + " " +
                                       SftpQuotePath(dir + newName));
                break;
        }
        return true;
    }

    // FTP: the commands run in the login folder (see the top of this file),
    // so the entry is named by its path from there - the URL's path without
    // its leading '/'. A URL whose path began "%2F" named an absolute path,
    // and decodes to "//..."; dropping one '/' leaves it absolute.
    const std::string path = serverPath.substr(1);
    const std::string dir = folder.substr(1);
    switch (verb) {
        case Verb::Delete:          out.commands.push_back("DELE " + path); break;
        case Verb::RemoveDirectory: out.commands.push_back("RMD " + path); break;
        case Verb::MakeDirectory:   out.commands.push_back("MKD " + path); break;
        case Verb::Rename:
            out.commands.push_back("RNFR " + path);
            out.commands.push_back("RNTO " + dir + newName);
            break;
    }
    return true;
}

} // namespace ultranet_internal::ftpquote
