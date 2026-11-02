// Apps/UltraDBAdmin/relay/RelayConfig.cpp
// Version: 0.3.0 - [signin], public_url, a user's email or subject; statement_timeout
// Version: 0.2.0 - no ssh_ options: the relay opens no tunnel
// Author: UltraCanvas Framework / ULTRA OS
#include "RelayConfig.h"

#include "UltraCanvasPathUtf8.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace UltraDBAdminRelay {
namespace {

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool Number(const std::string& text, long long& out) {
    if (text.empty()) return false;
    size_t i = text[0] == '-' ? 1 : 0;
    if (i == text.size()) return false;
    long long v = 0;
    for (; i < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
        v = v * 10 + (text[i] - '0');
        if (v > 1000000000000LL) return false;
    }
    out = text[0] == '-' ? -v : v;
    return true;
}

bool Truth(const std::string& text, bool& out) {
    const std::string v = Lower(text);
    if (v == "true" || v == "yes" || v == "on" || v == "1") { out = true; return true; }
    if (v == "false" || v == "no" || v == "off" || v == "0") { out = false; return true; }
    return false;
}

} // namespace

bool IsLoopback(const std::string& host) {
    return host == "localhost" || host == "::1" || host == "[::1]" || host.rfind("127.", 0) == 0;
}

std::string UrlHost(const std::string& url) {
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos) return std::string();
    const size_t start = scheme + 3;
    const size_t end = url.find_first_of("/?#", start);
    std::string authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    const size_t at = authority.rfind('@');
    if (at != std::string::npos) authority = authority.substr(at + 1);
    if (!authority.empty() && authority[0] == '[') {
        const size_t close = authority.find(']');
        return close == std::string::npos ? std::string() : authority.substr(1, close - 1);
    }
    return authority.substr(0, authority.find(':'));
}

namespace {

// https://, or http:// to this computer (a provider or a relay under test).
bool SecureOrLocal(const std::string& url) {
    if (url.rfind("https://", 0) == 0) return !UrlHost(url).empty();
    return url.rfind("http://", 0) == 0 && IsLoopback(UrlHost(url));
}

} // namespace

bool ParseRelayConfig(const std::string& text, RelayConfig& out, std::string& error) {
    out = RelayConfig();
    std::istringstream in(text);
    std::string line;
    int number = 0;
    enum class Section { Main, Connection, User, Limits, SignIn } section = Section::Main;
    auto fail = [&](const std::string& what) {
        error = "line " + std::to_string(number) + ": " + what;
        return false;
    };
    while (std::getline(in, line)) {
        ++number;
        // A comment from '#' or ';' to the end of the line.
        const size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos) line.resize(comment);
        line = Trim(line);
        if (line.empty()) continue;
        if (line.front() == '[') {
            if (line.back() != ']') return fail("a section is [name] or [kind name]");
            const std::string inside = Trim(line.substr(1, line.size() - 2));
            const size_t space = inside.find(' ');
            const std::string kind = Lower(inside.substr(0, space));
            const std::string name = space == std::string::npos ? std::string() : Trim(inside.substr(space + 1));
            if (kind == "connection") {
                if (name.empty()) return fail("a connection needs a name: [connection crm]");
                for (const auto& c : out.connections)
                    if (c.name == name) return fail("connection " + name + " twice");
                OfferedConnection c;
                c.name = name;
                c.config.driver = "postgresql";
                out.connections.push_back(c);
                section = Section::Connection;
            } else if (kind == "user") {
                if (name.empty()) return fail("a user needs a name: [user anna]");
                UltraDbRelayUser u;
                u.name = name;
                out.users.push_back(u);
                section = Section::User;
            } else if (kind == "limits") {
                section = Section::Limits;
            } else if (kind == "signin") {
                out.signIn.enabled = true;
                section = Section::SignIn;
            } else {
                return fail("no section [" + kind + "]: connection, user, signin or limits");
            }
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos) return fail("a setting is key = value");
        const std::string key = Lower(Trim(line.substr(0, equals)));
        const std::string value = Trim(line.substr(equals + 1));
        long long n = 0;
        bool truth = false;
        switch (section) {
            case Section::Main:
                if (key == "listen") {
                    const size_t colon = value.rfind(':');
                    // Port 0: any free one, said when it listens.
                    if (colon == std::string::npos || !Number(value.substr(colon + 1), n) || n < 0 || n > 65535)
                        return fail("listen is host:port");
                    out.listenHost = value.substr(0, colon);
                    if (out.listenHost.size() > 2 && out.listenHost.front() == '[')
                        out.listenHost = out.listenHost.substr(1, out.listenHost.size() - 2);
                    out.listenPort = static_cast<int>(n);
                } else if (key == "path") {
                    if (value.empty() || value[0] != '/') return fail("path starts with /");
                    out.path = value;
                } else if (key == "vault") {
                    out.vaultDirectory = value;
                } else if (key == "audit") {
                    out.auditFile = value;
                } else if (key == "origins") {
                    std::istringstream list(value);
                    std::string origin;
                    while (std::getline(list, origin, ','))
                        if (!Trim(origin).empty()) out.origins.push_back(Trim(origin));
                } else if (key == "plain_ok") {
                    if (!Truth(value, out.plainOk)) return fail("plain_ok is true or false");
                } else if (key == "public_url") {
                    if (value.rfind("https://", 0) != 0 && value.rfind("http://", 0) != 0)
                        return fail("public_url is the relay's https:// address, as browsers reach it");
                    out.publicUrl = value;
                    while (!out.publicUrl.empty() && out.publicUrl.back() == '/') out.publicUrl.pop_back();
                } else {
                    return fail("no setting " + key);
                }
                break;
            case Section::Connection: {
                UltraDbConnectionConfig& c = out.connections.back().config;
                if (key == "driver") c.driver = Lower(value);
                else if (key == "host") c.host = value;
                else if (key == "port") {
                    if (!Number(value, n) || n <= 0 || n > 65535) return fail("port is 1 to 65535");
                    c.port = static_cast<int>(n);
                } else if (key == "database") c.database = value;
                else if (key == "user") c.user = value;
                else if (key == "password_secret") c.credentials = "vault:relay.connection." + value;
                else if (key == "password") return fail("no password here: store it with --set-secret and name it with password_secret");
                else if (key == "tls") {
                    const std::string v = Lower(value);
                    if (v == "disable") c.tls = UltraDbTls::Disable;
                    else if (v == "prefer") c.tls = UltraDbTls::Prefer;
                    else if (v == "require") c.tls = UltraDbTls::Require;
                    else if (v == "verify-full") c.tls = UltraDbTls::VerifyFull;
                    else return fail("tls is disable, prefer, require or verify-full");
                } else if (key == "read_only") {
                    if (!Truth(value, truth)) return fail("read_only is true or false");
                    c.readOnly = truth;
                } else if (key.rfind("ssh_", 0) == 0) {
                    return fail("no SSH tunnel from the relay: run it where it reaches the database");
                } else {
                    return fail("no connection setting " + key);
                }
                break;
            }
            case Section::User: {
                UltraDbRelayUser& u = out.users.back();
                if (key == "token_sha256") {
                    const std::string v = Lower(value);
                    if (v.size() != 64 || v.find_first_not_of("0123456789abcdef") != std::string::npos)
                        return fail("token_sha256 is 64 hex digits (--hash-token)");
                    u.tokenSha256 = v;
                } else if (key == "email") {
                    if (value.find('@') == std::string::npos) return fail("email is an address");
                    out.identities[u.name].email = Lower(value);
                } else if (key == "subject") {
                    if (value.empty()) return fail("subject is the provider's sub claim");
                    out.identities[u.name].subject = value;
                } else {
                    const std::string v = Lower(value);
                    if (v == "read") u.access[key] = UltraDbRelayAccess::Read;
                    else if (v == "write") u.access[key] = UltraDbRelayAccess::Write;
                    else if (v == "none") u.access[key] = UltraDbRelayAccess::Denied;
                    else return fail(key + " is read, write or none");
                }
                break;
            }
            case Section::Limits:
                if (!Number(value, n) || n < 0) return fail(key + " is a number");
                if (key == "rows_per_page") out.limits.rowsPerPage = static_cast<size_t>(n);
                else if (key == "rows_per_result") out.limits.rowsPerResult = static_cast<size_t>(n);
                else if (key == "requests_per_minute") out.limits.requestsPerMinute = static_cast<int>(n);
                else if (key == "sessions_per_user") out.limits.sessionsPerUser = static_cast<size_t>(n);
                else if (key == "statement_timeout") out.limits.statementTimeoutSeconds = static_cast<int>(n);
                else return fail("no limit " + key);
                break;
            case Section::SignIn:
                if (key == "label") out.signIn.label = value;
                else if (key == "issuer") {
                    if (!SecureOrLocal(value)) return fail("issuer is an https:// address");
                    out.signIn.issuer = value;
                } else if (key == "client_id") out.signIn.clientId = value;
                else if (key == "client_secret_secret") out.signIn.clientSecretName = value;
                else if (key == "client_secret")
                    return fail("no client secret here: store it with --set-secret and name it with client_secret_secret");
                else if (key == "scopes") out.signIn.scopes = value;
                else if (key == "ticket_hours") {
                    if (!Number(value, n) || n < 1 || n > 24 * 30) return fail("ticket_hours is 1 to 720");
                    out.signIn.ticketHours = static_cast<int>(n);
                } else return fail("no sign-in setting " + key);
                break;
        }
    }
    // What makes no sense as a whole.
    for (const auto& u : out.users) {
        const auto identity = out.identities.find(u.name);
        const bool signsIn = identity != out.identities.end();
        if (u.tokenSha256.empty() && !signsIn) {
            error = "user " + u.name + " has no token_sha256, email or subject";
            return false;
        }
        if (signsIn && !out.signIn.enabled) {
            error = "user " + u.name + " signs in by email or subject, but there is no [signin]";
            return false;
        }
        for (const auto& [name, unused] : u.access) {
            bool known = false;
            for (const auto& c : out.connections) known = known || c.name == name;
            if (!known) {
                error = "user " + u.name + " is granted " + name + ", which is no [connection]";
                return false;
            }
        }
    }
    if (out.signIn.enabled) {
        if (out.signIn.issuer.empty() || out.signIn.clientId.empty()) {
            error = "[signin] needs issuer and client_id";
            return false;
        }
        if (out.signIn.scopes.find("openid") == std::string::npos) {
            error = "[signin] scopes must hold openid: the sign-in reads the ID token";
            return false;
        }
        if (out.publicUrl.empty()) {
            error = "[signin] needs public_url: the address the provider sends the browser back to";
            return false;
        }
        if (!SecureOrLocal(out.publicUrl) && !out.plainOk) {
            error = "public_url = " + out.publicUrl + " is not https://: the sign-in's tickets would travel in the clear";
            return false;
        }
    }
    if (!IsLoopback(out.listenHost) && !out.plainOk) {
        error = "listen = " + out.listenHost + " is reachable from the network without TLS: put a TLS proxy in "
                "front and listen on 127.0.0.1, or set plain_ok = true on a network you trust";
        return false;
    }
    return true;
}

bool LoadRelayConfig(const std::string& file, RelayConfig& out, std::string& error) {
    std::ifstream in(UltraCanvas::PathFromUtf8(file), std::ios::binary);
    if (!in) {
        error = "cannot read " + file;
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    if (!ParseRelayConfig(text.str(), out, error)) {
        error = file + ", " + error;
        return false;
    }
    return true;
}

} // namespace UltraDBAdminRelay
