// Apps/UltraDBAdmin/relay/main.cpp
// ultradbadmin-relay - the server half of UltraDBAdmin's browser build
// (Docs/Research/UltraDBAdminDesignProposal.md, 8.1): the page in a browser
// sends every database call here over a WebSocket, and this runs it on a
// connection named in its configuration (RelayConfig.h), with a password from
// its own vault, for the users its tokens name, within its limits, writing
// each statement and who ran it to the audit log.
//
//   ultradbadmin-relay --config relay.ini         serve
//   ultradbadmin-relay --config relay.ini --check read the file and say what it offers
//   ultradbadmin-relay --hash-token               a token from stdin -> token_sha256
//   ultradbadmin-relay --config relay.ini --set-secret crm
//                                                 a password from stdin -> the vault
// Version: 0.2.1 - the no-sign-in message names UltraCrypt's OpenSSL backend
// Version: 0.2.0 - the OpenID Connect sign-in ([signin], RelaySignIn.h)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "RelayConfig.h"
#include "RelayWebSocket.h"
#if defined(ULTRADBADMIN_RELAY_SIGNIN)
#include "RelaySignIn.h"
#endif

#include "UltraCanvasPathUtf8.h"
#include <UltraDatabase/UltraDatabase.h>
#include <UltraVault/UltraVault.h>
#include <UltraVault/UltraVaultDeviceKeyVault.h>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

using namespace UltraDBAdminRelay;

namespace {

RelayListener* g_listener = nullptr;

void OnSignal(int) {
    if (g_listener) g_listener->Stop();
}

void Usage() {
    std::printf(
        "ultradbadmin-relay - UltraDBAdmin's relay for the browser build\n"
        "\n"
        "  --config FILE        the configuration (see Apps/UltraDBAdmin/relay/RelayConfig.h)\n"
        "  --check              read the configuration, say what it offers, and stop\n"
        "  --hash-token         read a token from stdin, print its token_sha256\n"
        "  --set-secret NAME    read a password from stdin into the relay's vault, as\n"
        "                       password_secret = NAME names it\n"
        "  --help               this\n");
}

std::string ReadLine() {
    std::string line;
    std::getline(std::cin, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    return line;
}

std::string Now() {
    const std::time_t t = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&t, &utc);
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}

// One line of the audit log: tabs between the fields, the statement's own
// tabs and line breaks escaped.
std::string AuditLine(const UltraDbRelayAuditEntry& e) {
    std::string sql;
    for (char c : e.sql) {
        if (c == '\t') sql += "\\t";
        else if (c == '\n') sql += "\\n";
        else if (c == '\r') sql += "\\r";
        else sql += c;
    }
    return Now() + "\t" + e.user + "\t" + e.peer + "\t" + e.connection + "\t" +
           (e.result.success ? "ok" : "failed") + "\t" + sql + "\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string configFile, secretName;
    bool check = false, hash = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) configFile = argv[++i];
        else if (arg == "--check") check = true;
        else if (arg == "--hash-token") hash = true;
        else if (arg == "--set-secret" && i + 1 < argc) secretName = argv[++i];
        else if (arg == "--help" || arg == "-h") { Usage(); return 0; }
        else { std::fprintf(stderr, "unknown argument %s\n", arg.c_str()); Usage(); return 2; }
    }
    if (hash) {
        const std::string token = ReadLine();
        if (token.size() < 24) {
            std::fprintf(stderr, "a token of at least 24 characters, please: it is the user's only key\n");
            return 1;
        }
        std::printf("%s\n", UltraDb_RelayTokenHash(token).c_str());
        return 0;
    }
    if (configFile.empty()) { Usage(); return 2; }
    RelayConfig config;
    std::string error;
    if (!LoadRelayConfig(configFile, config, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    // The vault: the database passwords, never the configuration file.
    std::unique_ptr<UltraVault::DeviceKeyVault> vault;
    if (!config.vaultDirectory.empty()) {
        vault = std::make_unique<UltraVault::DeviceKeyVault>(
            config.vaultDirectory, UltraVault::DeviceKeyVaultProfile{ "relay.vault", "relay.connection." });
        if (!vault->TryAutoUnlock()) {
            std::fprintf(stderr, "cannot open the vault in %s: %s\n", config.vaultDirectory.c_str(),
                         UltraVault::DeviceKeyVault::DescribeUnlockStatus(vault->GetLastUnlockStatus()).c_str());
            return 1;
        }
    }
    if (!secretName.empty()) {
        if (!vault) {
            std::fprintf(stderr, "the configuration names no vault = directory\n");
            return 1;
        }
        std::string secret = ReadLine();
        const bool stored = !secret.empty() && vault->Store(secretName, secret);
        for (char& c : secret) c = '\0';
        if (!stored) {
            std::fprintf(stderr, "not stored\n");
            return 1;
        }
        std::printf("stored as %s - name it with password_secret = %s\n", secretName.c_str(), secretName.c_str());
        return 0;
    }

    UltraDbRelayServer server;
    for (const OfferedConnection& c : config.connections) server.AddConnection(c.name, c.config);
    for (const UltraDbRelayUser& u : config.users) server.AddUser(u);
    server.SetLimits(config.limits);
    std::mutex auditMutex;
    std::ofstream audit;
    if (!config.auditFile.empty()) {
        audit.open(UltraCanvas::PathFromUtf8(config.auditFile), std::ios::app);
        if (!audit) {
            std::fprintf(stderr, "cannot write the audit log %s\n", config.auditFile.c_str());
            return 1;
        }
        server.SetAudit([&audit, &auditMutex](const UltraDbRelayAuditEntry& e) {
            std::lock_guard<std::mutex> lock(auditMutex);
            audit << AuditLine(e);
            audit.flush();
        });
    }

    // The sign-in: the provider read now, so a mistake shows at start-up.
#if defined(ULTRADBADMIN_RELAY_SIGNIN)
    std::unique_ptr<RelaySignIn> signIn;
    if (config.signIn.enabled) {
        std::string secret;
        if (!config.signIn.clientSecretName.empty() &&
            (!vault || !vault->Retrieve(config.signIn.clientSecretName, secret))) {
            std::fprintf(stderr, "the client secret %s is not in the relay's vault (--set-secret %s)\n",
                         config.signIn.clientSecretName.c_str(), config.signIn.clientSecretName.c_str());
            return 1;
        }
        signIn = std::make_unique<RelaySignIn>(config, secret, [&audit, &auditMutex](const std::string& peer,
                                                                                    const std::string& line, bool ok) {
            if (!audit.is_open()) return;
            UltraDbRelayAuditEntry entry;
            entry.user = "-";
            entry.peer = peer;
            entry.connection = "-";
            entry.sql = line;
            if (!ok) entry.result = UltraDbResult::Error(UltraDbResultCode::CredentialsUnavailable, line, "relay");
            std::lock_guard<std::mutex> lock(auditMutex);
            audit << AuditLine(entry);
            audit.flush();
        });
        for (char& c : secret) c = '\0';
        if (!signIn->Start(error)) {
            std::fprintf(stderr, "[signin] %s: %s\n", config.signIn.issuer.c_str(), error.c_str());
            return 1;
        }
        RelaySignIn* tickets = signIn.get();
        server.SetSignIn(config.signIn.label, [&config, tickets](const std::string& token, UltraDbRelayUser& user) {
            std::string name;
            if (!tickets->Ticket(token, name)) return false;
            for (const UltraDbRelayUser& u : config.users)
                if (u.name == name) {
                    user = u;
                    return true;
                }
            return false;
        });
    }
#else
    if (config.signIn.enabled) {
        std::fprintf(stderr, "this relay was built without UltraNet or UltraCrypt's OpenSSL 3 backend, so it has no [signin]\n");
        return 1;
    }
#endif

    if (check) {
        std::printf("listen %s:%d%s\n", config.listenHost.c_str(), config.listenPort, config.path.c_str());
#if defined(ULTRADBADMIN_RELAY_SIGNIN)
        if (signIn)
            std::printf("signin %s: %s - signs in at %s, returns to %s/callback\n", config.signIn.label.c_str(),
                        config.signIn.issuer.c_str(), signIn->AuthorizationEndpoint().c_str(), config.publicUrl.c_str());
#endif
        if (config.limits.statementTimeoutSeconds > 0)
            std::printf("statements stopped after %d s\n", config.limits.statementTimeoutSeconds);
        for (const OfferedConnection& c : config.connections)
            std::printf("connection %s: %s %s%s\n", c.name.c_str(), c.config.driver.c_str(), c.config.database.c_str(),
                        c.config.readOnly ? " (read only)" : "");
        for (const UltraDbRelayUser& u : config.users) {
            std::printf("user %s:", u.name.c_str());
            const auto identity = config.identities.find(u.name);
            if (identity != config.identities.end())
                std::printf(" (signs in as %s)", identity->second.subject.empty() ? identity->second.email.c_str()
                                                                                  : identity->second.subject.c_str());
            for (const auto& [name, access] : u.access)
                std::printf(" %s=%s", name.c_str(),
                            access == UltraDbRelayAccess::Write ? "write" : access == UltraDbRelayAccess::Read ? "read" : "none");
            std::printf("\n");
        }
        return 0;
    }

    ListenOptions options;
    options.host = config.listenHost;
    options.port = config.listenPort;
    options.path = config.path;
    options.origins = config.origins;
#if defined(ULTRADBADMIN_RELAY_SIGNIN)
    if (signIn) options.http = [&signIn](const HttpRequest& request, HttpReply& reply) { return signIn->Handle(request, reply); };
#endif
    RelayListener listener(server, options);
    if (!listener.Start(error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    g_listener = &listener;
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    std::printf("ultradbadmin-relay listening on %s:%d%s - %zu connections, %zu users\n", config.listenHost.c_str(),
                listener.Port(), config.path.c_str(), config.connections.size(), config.users.size());
    std::fflush(stdout);
    listener.Run();
    g_listener = nullptr;
    return 0;
}
