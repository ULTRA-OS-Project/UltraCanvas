// Apps/UltraDBAdmin/relay/RelayConfig.cpp
// Version: 0.1.0
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

bool ParseRelayConfig(const std::string& text, RelayConfig& out, std::string& error) {
    out = RelayConfig();
    std::istringstream in(text);
    std::string line;
    int number = 0;
    enum class Section { Main, Connection, User, Limits } section = Section::Main;
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
            } else {
                return fail("no section [" + kind + "]: connection, user or limits");
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
                    c.options[key] = value;     // a bastion, as UltraDatabaseTunnel.h reads it
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
                else return fail("no limit " + key);
                break;
        }
    }
    // What makes no sense as a whole.
    for (const auto& u : out.users) {
        if (u.tokenSha256.empty()) {
            error = "user " + u.name + " has no token_sha256";
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
