// include/UltraDatabase/UltraDatabaseRelay.h
// UltraDatabase through a relay: an application that cannot reach a
// database itself - a page in a browser, which opens no TCP connection and
// must hold no password - sends every call of a connection to a relay on
// the database's side, which runs it on a connection configured there and
// answers (Docs/Research/UltraDBAdminDesignProposal.md, 8.1).
//
//   application ── "relay" driver ──transport──► UltraDbRelayServer ──► UltraDb_* ──► the engine
//
// The client is a driver like the others, "relay": its connection's `host`
// is the relay's address, `database` the name of a connection the relay
// offers, options["relay_token"] the user's token. Everything the module
// does - queries, scripts, cursors, cancel, introspection, server
// administration - goes through it unchanged, so an application written
// against UltraDatabase works through a relay as it is. The transport is the
// application's (UltraDb_SetRelayConnector): a WebSocket in the browser, or
// one over UltraNet on a desktop; UltraDatabase opens no socket.
//
// The server answers requests: it knows the connections it offers by name
// (the browser cannot name a host, so it is no proxy into the network), the
// users by the SHA-256 of their tokens with the access each has to each
// connection - read (the driver's read-only mode, not hidden buttons) or
// write - and its limits: rows per page, requests per minute per user. Every
// statement it runs reaches the audit callback with the user who ran it. The
// transport around it - the listener, TLS - is the program's
// (Apps/UltraDBAdmin/relay).
//
// The wire format is JSON text, one request and one answer per message,
// matched by "id"; a session may have several requests running at once (a
// cancel while a query runs).
//
// doc-check: example
//   #include <UltraDatabase/UltraDatabaseRelay.h>
//
//   // On the database's side
//   UltraDbRelayServer relay;
//   UltraDbConnectionConfig crm;
//   crm.name = "crm-db";
//   crm.driver = "postgresql";
//   crm.database = "crm";
//   relay.AddConnection("crm", crm);
//   UltraDbRelayUser anna;
//   anna.name = "anna";
//   anna.tokenSha256 = UltraDb_RelayTokenHash("a long random token");
//   anna.access["crm"] = UltraDbRelayAccess::Read;
//   relay.AddUser(anna);
//   const uint64_t session = relay.OpenSession("203.0.113.7");
//   std::string answer = relay.Handle(session, "{\"id\":1,\"op\":\"hello\",\"token\":\"a long random token\"}");
//   relay.CloseSession(session);
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraDatabaseCore.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ===== CLIENT =====

// How a relay connection reaches its relay: one request sent, its answer
// waited for. Called from any thread, also while another call waits - a
// cancel goes out while a query runs - so a transport matches each answer to
// its request by `id`: the request carries it as "id", and every answer
// begins {"id":<id>, so the number after that prefix is all a transport
// reads of it. A failure to reach the relay is an UltraDbResult,
// ConnectionFailed.
class IUltraDbRelayTransport {
public:
    virtual ~IUltraDbRelayTransport() = default;
    virtual UltraDbResult Call(int64_t id, const std::string& request, std::string& response) = 0;
};

// The id an answer begins with ({"id":<id>,...), or -1.
int64_t UltraDb_RelayAnswerId(const std::string& answer);

// Opens a transport to the relay at `url` (as a connection's `host` names
// it). Each relay connection opens its own.
using UltraDbRelayConnector =
    std::function<std::unique_ptr<IUltraDbRelayTransport>(const std::string& url, UltraDbResult& error)>;

// Install the connector every "relay" connection uses; an empty one removes
// it, and a relay connection then fails with Unsupported.
void UltraDb_SetRelayConnector(UltraDbRelayConnector connector);

// A connection a relay offers this user.
struct UltraDbRelayConnection {
    std::string name;       // what the client's `database` names
    std::string engine;     // "sqlite", "postgresql", "mysql"
    bool readOnly = false;  // this user may only read
};

// What the relay at `url` offers the user of `token` (its "hello").
UltraDbResult UltraDb_RelayConnections(const std::string& url, const std::string& token,
                                       std::vector<UltraDbRelayConnection>& out);

// ===== SERVER =====

enum class UltraDbRelayAccess { Denied, Read, Write };

struct UltraDbRelayUser {
    std::string name;
    std::string tokenSha256;                              // lower-case hex, UltraDb_RelayTokenHash
    std::map<std::string, UltraDbRelayAccess> access;     // by connection name; absent: Denied
};

struct UltraDbRelayLimits {
    size_t rowsPerPage = 1000;          // a cursor's page at most
    size_t rowsPerResult = 100000;      // a query's rows at most; more: use a cursor
    int requestsPerMinute = 1200;       // per user; 0: no limit
    size_t sessionsPerUser = 16;        // open relay sessions, per user
};

// A statement the relay ran, for the audit log.
struct UltraDbRelayAuditEntry {
    std::string user;
    std::string peer;           // the client's address, as OpenSession was given it
    std::string connection;     // the offered name
    std::string sql;
    UltraDbResult result;
};

class UltraDbRelayServer {
public:
    UltraDbRelayServer();
    ~UltraDbRelayServer();      // ends every session, closing its connections
    UltraDbRelayServer(const UltraDbRelayServer&) = delete;
    UltraDbRelayServer& operator=(const UltraDbRelayServer&) = delete;

    // A connection offered as `name`. Its config - credentials from the
    // server's vault, an SSH tunnel - is the server's alone; each session
    // that opens it gets a connection of its own.
    void AddConnection(const std::string& name, const UltraDbConnectionConfig& config);
    void AddUser(const UltraDbRelayUser& user);
    void SetLimits(const UltraDbRelayLimits& limits);
    void SetAudit(std::function<void(const UltraDbRelayAuditEntry&)> audit);

    // A client's session (one WebSocket); `peer` names it in the audit log.
    uint64_t OpenSession(const std::string& peer);
    // Ends it: its cursors and connections closed, a running query stopped.
    void CloseSession(uint64_t session);

    // One request of `session`, and its answer. Thread-safe: requests of one
    // session may run at once.
    std::string Handle(uint64_t session, const std::string& request);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The SHA-256 of `token`, in lower-case hex: what a server keeps of it.
std::string UltraDb_RelayTokenHash(const std::string& token);
