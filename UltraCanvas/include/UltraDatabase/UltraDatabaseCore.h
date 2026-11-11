// include/UltraDatabase/UltraDatabaseCore.h
// Stage 1 subset of the UltraDatabase module: handles, result type,
// connection configuration, and the version/driver-registry entrypoints.
// Full surface specified in Docs/Modules/UltraDatabase/README.md.
// Version: 0.3.0 - UltraDbConnectionConfig::encryptionKey (UltraDatabaseEncryption.h)
// Version: 0.2.0 - HostKeyUnknown, HostKeyChanged, UltraDbResult::hostKeyFingerprint (SSH tunnels)
// Version: 0.1.0 (Stage 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class UltraCryptSecureBuffer;   // UltraCrypt/UltraCryptCore.h

// Opaque handle for prepared statements, transactions and (Stage 2) cursors
// and async queries. Zero is always invalid.
using UltraDbHandle = uint64_t;
constexpr UltraDbHandle UltraDbInvalidHandle = 0;

// Result code for every blocking operation. Drivers map their native error
// codes onto this enum.
enum class UltraDbResultCode {
    Ok = 0,
    InvalidArgument,
    ConnectionNotFound,
    DriverNotFound,
    ConnectionFailed,
    CredentialsUnavailable,   // the password could not be obtained here (vault
                              // closed, key missing); nothing was sent anywhere
    QueryFailed,
    ConstraintViolation,
    TypeMismatch,
    NotFound,
    Busy,
    Misuse,
    AlreadyExists,
    Unsupported,
    Internal,
    Unknown,
    Cancelled,                // stopped by UltraDb_CancelQuery
    // An SSH tunnel's bastion (UltraDatabaseTunnel.h) whose host key nobody
    // trusted yet - UltraDbResult::hostKeyFingerprint is the key, for the user
    // to accept - or one that differs from the key trusted, never accepted.
    HostKeyUnknown,
    HostKeyChanged
};

// Returned by every blocking UltraDb_* call. Operator bool() reports success
// for `if (UltraDb_Exec(...))` style checks; the structured fields carry
// diagnostics.
struct UltraDbResult {
    UltraDbResultCode code = UltraDbResultCode::Ok;
    bool              success = true;
    std::string       message;          // human-readable diagnostic
    std::string       driver;           // driver id that produced the result
    std::string       sqlState;         // SQLSTATE where the driver provides it
    int64_t           affectedRows = 0; // rows this INSERT/UPDATE/DELETE changed (not its triggers'); 0 for any other statement
    int64_t           lastInsertId = 0; // the row id this INSERT gave, if any; 0 when the statement inserted nothing
    double            processingTimeMs = 0.0;
    std::string       hostKeyFingerprint; // HostKeyUnknown / HostKeyChanged: the bastion's key, "SHA256:..."

    explicit operator bool() const { return success; }

    static UltraDbResult Ok() { return UltraDbResult{}; }
    static UltraDbResult Error(UltraDbResultCode c, const std::string& msg,
                               const std::string& drv = "") {
        UltraDbResult r;
        r.code = c;
        r.success = false;
        r.message = msg;
        r.driver = drv;
        return r;
    }
};

// TLS posture for networked engines (ignored by the embedded SQLite driver).
enum class UltraDbTls {
    Disable = 0,   // never use TLS
    Prefer,        // use TLS if the server offers it, else plaintext
    Require,       // require TLS, but do not verify the certificate
    VerifyFull     // require TLS and verify certificate + hostname (default)
};

// Describes one named connection. For the SQLite driver, `database` is a file
// path (leading "~" is expanded) or ":memory:"; host/port/user/credentials
// are unused.
struct UltraDbConnectionConfig {
    std::string name;                     // referenced everywhere by this
    std::string driver = "sqlite";        // driver id: "sqlite", "postgresql", ...
    std::string database;                 // file path / db name
    std::string host;
    int         port = 0;
    std::string user;
    std::string credentials;              // UltraVault key ("vault:...") or empty
    UltraDbTls  tls = UltraDbTls::VerifyFull;
    bool        readOnly = false;
    int         poolSize = 1;             // Stage 1 opens a single connection
    std::map<std::string, std::string> options;  // driver-specific extras
    // SQLite: the file is encrypted at rest with this 32-byte key
    // (UltraDatabaseEncryption.h); empty, it is an ordinary SQLite file.
    std::shared_ptr<const UltraCryptSecureBuffer> encryptionKey;
};

// Module version string, e.g. "0.1.0".
std::string UltraDatabase_GetVersion();

// Driver ids currently registered (always includes the built-in "sqlite").
std::vector<std::string> UltraDatabase_GetSupportedDrivers();
