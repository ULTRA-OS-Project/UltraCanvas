// Apps/UltraAuthenticator/store/ISecretStore.h
// The interface secrets are read and written through.
//
// This exists to be replaced. UltraVault is the designated system-wide
// credential store for ULTRA OS but does not exist yet
// (UltraAI/Docs/UltraVault.md), so the authenticator ships its own encrypted
// file store in the meantime. Defining that behind an interface — and shaping
// the interface like UltraVault's own Put/Get/Delete/List surface — means the
// swap later is a constructor change, not a rewrite of the app.
//
// Every value is an UltraCryptSecureBuffer: implementations must never take or
// hand back a secret in a std::string.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ISECRETSTORE_H
#define ISECRETSTORE_H

#include "UltraCrypt/UltraCryptCore.h"

#include <string>
#include <vector>

namespace UltraCanvas {
namespace Authenticator {

enum class StoreResultCode : uint8_t {
    Success,
    NotOpen,               // no vault is open
    AlreadyExists,         // Create() against an existing file
    NotFound,              // no entry under that key
    AuthenticationFailed,  // wrong password, or the file was altered
    InvalidArgument,
    Corrupt,               // malformed container (distinct from tampering)
    IoError,
    BackendUnavailable,    // built without a crypto backend
    InternalError
};

struct StoreResult {
    StoreResultCode code = StoreResultCode::InternalError;
    bool success = false;
    std::string message;   // developer-facing; never contains secret material

    operator bool() const { return success; }

    static StoreResult Ok();
    static StoreResult Error(StoreResultCode code, const std::string& message);
};

class ISecretStore {
public:
    virtual ~ISecretStore() = default;

    virtual bool IsOpen() const = 0;

    // Inserts or replaces. Implementations persist before returning success:
    // an authenticator that loses a just-added account has failed at its job.
    virtual StoreResult Put(const std::string& key,
                            const UltraCryptSecureBuffer& value) = 0;

    virtual StoreResult Get(const std::string& key,
                            UltraCryptSecureBuffer& outValue) const = 0;

    virtual StoreResult Delete(const std::string& key) = 0;

    // Keys only — never values. Listing must not materialise secrets.
    virtual StoreResult List(std::vector<std::string>& outKeys) const = 0;

    virtual size_t Count() const = 0;
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // ISECRETSTORE_H
