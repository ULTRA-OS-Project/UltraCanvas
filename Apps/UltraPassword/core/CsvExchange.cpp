// Apps/UltraPassword/core/CsvExchange.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "CsvExchange.h"

#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace UltraPassword {
namespace {

struct MethodKey { SignInMethod method; const char* key; };

const MethodKey kMethodKeys[] = {
    {SignInMethod::Password,                 "password"},
    {SignInMethod::PasswordAuthenticatorApp, "password+totp"},
    {SignInMethod::PasswordSms,              "password+sms"},
    {SignInMethod::PasswordHardwareKey,      "password+securitykey"},
    {SignInMethod::Passkey,                  "passkey"},
    {SignInMethod::PasskeyPasswordFallback,  "passkey+password"},
    {SignInMethod::SingleSignOn,             "sso"},
    {SignInMethod::EmailLink,                "email-link"},
    {SignInMethod::PasswordEmailCode,        "password+email"},
};

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string Quote(const std::string& value) {
    // A cell starting with = + - @ is a formula to a spreadsheet; prefix a
    // quote mark so opening the export in one cannot run anything (CSV
    // injection). Importers strip nothing, so only do it where it matters.
    std::string v = value;
    if (!v.empty() && (v[0] == '=' || v[0] == '+' || v[0] == '@' || v[0] == '\t' || v[0] == '\r'))
        v = "'" + v;
    std::string out = "\"";
    for (char c : v) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    WipeString(v);
    return out;
}

// RFC 4180 parser: rows of cells; quoted cells may hold commas, quotes ("")
// and line breaks. Tolerates CRLF and LF, and a UTF-8 byte-order mark.
std::vector<std::vector<std::string>> ParseCsv(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string cell;
    bool inQuotes = false;
    size_t i = 0;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        i = 3;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') { cell += '"'; ++i; }
                else inQuotes = false;
            } else {
                cell += c;
            }
        } else if (c == '"') {
            inQuotes = true;
        } else if (c == ',') {
            row.push_back(std::move(cell));
            cell.clear();
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            row.push_back(std::move(cell));
            cell.clear();
            rows.push_back(std::move(row));
            row.clear();
        } else {
            cell += c;
        }
    }
    if (!cell.empty() || !row.empty()) {
        row.push_back(std::move(cell));
        rows.push_back(std::move(row));
    }
    return rows;
}

int ColumnOf(const std::vector<std::string>& header, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        for (size_t i = 0; i < header.size(); ++i)
            if (Lower(Trim(header[i])) == name) return static_cast<int>(i);
    }
    return -1;
}

std::string CellAt(const std::vector<std::string>& row, int column) {
    if (column < 0 || static_cast<size_t>(column) >= row.size()) return {};
    return row[static_cast<size_t>(column)];
}

// Finds or creates the group path "A / B / C" under `parentId`.
std::string EnsureGroupPath(PasswordVault& vault, const std::string& parentId, const std::string& path) {
    std::string current = parentId;
    std::stringstream ss(path);
    std::string part;
    // Split on '/' (Bitwarden and KeePass write "A/B"; we write "A / B").
    while (std::getline(ss, part, '/')) {
        part = Trim(part);
        if (part.empty()) continue;
        std::string found;
        for (const PasswordGroup* g : vault.ChildGroups(current))
            if (g->name == part) { found = g->id; break; }
        if (found.empty()) found = vault.AddGroup(part, current);
        if (found.empty()) return current;
        current = found;
    }
    return current;
}

std::string HostOf(const std::string& url) {
    std::string s = url;
    const size_t scheme = s.find("://");
    if (scheme != std::string::npos) s = s.substr(scheme + 3);
    const size_t end = s.find_first_of("/?#:");
    if (end != std::string::npos) s = s.substr(0, end);
    if (s.rfind("www.", 0) == 0) s = s.substr(4);
    return s;
}

} // namespace

std::string SignInMethodKey(SignInMethod method) {
    for (const auto& k : kMethodKeys)
        if (k.method == method) return k.key;
    return "password";
}

bool SignInMethodFromKey(const std::string& key, SignInMethod& out) {
    const std::string k = Lower(Trim(key));
    for (const auto& m : kMethodKeys) {
        if (k == m.key) { out = m.method; return true; }
    }
    return false;
}

std::string ExportCsv(const PasswordVault& vault) {
    std::string out = "name,url,username,password,note,group,signin_method,sso_provider\r\n";
    // Group order, then title order, so the file reads like the tree.
    std::vector<std::string> order = {kRootGroupId};
    for (size_t i = 0; i < order.size(); ++i)
        for (const PasswordGroup* g : vault.ChildGroups(order[i])) order.push_back(g->id);
    for (const std::string& groupId : order) {
        const std::string path = vault.GroupPath(groupId);
        for (const PasswordEntry* e : vault.EntriesIn(groupId)) {
            std::string password = Quote(e->password);
            out += Quote(e->title) + "," + Quote(e->url) + "," + Quote(e->username) + "," +
                   password + "," + Quote(e->notes) + "," + Quote(path) + "," +
                   Quote(SignInMethodKey(e->method)) + "," + Quote(e->ssoProvider) + "\r\n";
            WipeString(password);
        }
    }
    return out;
}

int ImportCsv(const std::string& csv, PasswordVault& vault,
              const std::string& intoGroupId, std::string& error) {
    auto rows = ParseCsv(csv);
    auto wipeRows = [&rows]() {
        for (auto& r : rows) for (auto& c : r) WipeString(c);
    };
    if (rows.empty()) { error = "The file is empty."; return -1; }

    const auto& header = rows.front();
    const int cTitle = ColumnOf(header, {"name", "title", "account"});
    const int cUrl   = ColumnOf(header, {"url", "login_uri", "website", "web site", "origin"});
    const int cUser  = ColumnOf(header, {"username", "login_username", "user", "login", "email"});
    const int cPass  = ColumnOf(header, {"password", "login_password"});
    const int cNote  = ColumnOf(header, {"note", "notes", "extra", "comments"});
    const int cGroup = ColumnOf(header, {"group", "folder", "grouping"});
    const int cMeth  = ColumnOf(header, {"signin_method"});
    const int cSso   = ColumnOf(header, {"sso_provider"});
    if (cPass < 0 && cUser < 0) {
        wipeRows();
        error = "No \"password\" or \"username\" column was found in the first row, "
                "so this does not look like a password export.";
        return -1;
    }
    if (!vault.GroupExists(intoGroupId)) {
        wipeRows();
        error = "The target group no longer exists.";
        return -1;
    }

    int added = 0;
    for (size_t r = 1; r < rows.size(); ++r) {
        const auto& row = rows[r];
        if (row.size() == 1 && Trim(row[0]).empty()) continue;   // blank line
        PasswordEntry e;
        e.title    = CellAt(row, cTitle);
        e.url      = CellAt(row, cUrl);
        e.username = CellAt(row, cUser);
        e.password = CellAt(row, cPass);
        e.notes    = CellAt(row, cNote);
        e.ssoProvider = CellAt(row, cSso);
        SignInMethod m;
        if (SignInMethodFromKey(CellAt(row, cMeth), m)) e.method = m;
        if (e.title.empty()) e.title = HostOf(e.url);
        if (e.title.empty()) e.title = e.username.empty() ? "Imported entry" : e.username;
        e.groupId = EnsureGroupPath(vault, intoGroupId, CellAt(row, cGroup));
        if (!vault.AddEntry(std::move(e)).empty()) ++added;
    }
    wipeRows();
    return added;
}

bool WriteCsvFile(const std::string& path, const PasswordVault& vault, std::string& error) {
    namespace fs = std::filesystem;
    std::string text = ExportCsv(vault);
    const fs::path target = UltraCanvas::PathFromUtf8(path);
    fs::path temporary = target;
    temporary += ".tmp";
    bool ok = false;
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (file) {
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            file.flush();
            ok = static_cast<bool>(file);
        }
    }
    WipeString(text);
    std::error_code ec;
    if (ok) {
        fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace, ec);
        ec.clear();
        fs::rename(temporary, target, ec);
        ok = !ec;
    }
    if (!ok) {
        fs::remove(temporary, ec);
        error = "The CSV file could not be written.";
    }
    return ok;
}

int ReadCsvFile(const std::string& path, PasswordVault& vault,
                const std::string& intoGroupId, std::string& error) {
    std::ifstream file(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    if (!file) { error = "The file could not be read."; return -1; }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const int added = ImportCsv(text, vault, intoGroupId, error);
    WipeString(text);
    return added;
}

} // namespace UltraPassword
