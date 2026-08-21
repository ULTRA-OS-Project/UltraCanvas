// include/UltraVault/UltraVault.h
// Credential & secret storage for the ULTRA OS / UltraCanvas framework.
//
// UltraVault is the single system-level home for API keys, tokens,
// passphrases and other secrets, so no application or sibling module rolls
// its own storage (UltraAI/Docs/UltraVault.md is the design document and
// the contract UltraAI assumes when resolving apiKeyVaultRef).
//
// v0.1 implements two backends, selected via UltraVaultConfig:
//  - Memory: process-lifetime store, nothing persisted. The CI / test
//    backend, and the Auto fallback when nothing else is configured.
//  - File: a single encrypted vault file. Argon2id-derived key (parameters
//    stored in the header) and XChaCha20-Poly1305 AEAD via UltraCrypt; the
//    file header is authenticated as associated data, so tampering with the
//    stored cost parameters is detected rather than obeyed.
// Platform-native backends (libsecret / Keychain / Credential Manager) are
// planned and slot in behind this same surface.
//
// Design rules:
//  - Nothing throws; every fallible operation returns UltraVault::Result.
//  - No oracle: a wrong passphrase and a tampered vault file report the
//    same AccessDenied with the same message.
//  - Secret bytes live in memory only as long as the store is open;
//    Shutdown() wipes the decrypted state and the derived key.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef ULTRAVAULT_H
#define ULTRAVAULT_H

#include <cstdint>
#include <string>
#include <vector>

// When included after X11 headers, Xlib's `Success` macro (0) collides with
// ResultCode::Success — the same collision UltraAICommon.h neutralizes for
// X11's `None`. Undefine it before our declarations.
#ifdef Success
#undef Success
#endif

namespace UltraVault {

// ============================================================================
// Results
// ============================================================================
enum class ResultCode : uint8_t {
    Success,
    NotFound,            // no secret stored under this key
    AccessDenied,        // wrong passphrase, or the vault file was altered
    Locked,              // Initialize() has not run (or Shutdown() has)
    BackendUnavailable,  // backend cannot operate (e.g. UltraCrypt without
                         // libsodium, unwritable vault path)
    InvalidKey,          // malformed secret key string
    IoError,             // filesystem failure reading/writing the vault
    Unknown
};

struct Result {
    ResultCode code = ResultCode::Unknown;
    std::string message;

    bool IsOk() const { return code == ResultCode::Success; }
    operator bool() const { return IsOk(); }

    static Result Ok() { return Result{ResultCode::Success, {}}; }
    static Result Error(ResultCode code, const std::string& message) {
        return Result{code, message};
    }
};

// ============================================================================
// Values and ACLs
// ============================================================================
struct SecretValue {
    std::vector<uint8_t> bytes;        // arbitrary content
    std::string mimeType = "text/plain";

    std::string AsString() const {
        return std::string(bytes.begin(), bytes.end());
    }
    static SecretValue FromString(const std::string& text,
                                  const std::string& mimeType = "text/plain") {
        SecretValue v;
        v.bytes.assign(text.begin(), text.end());
        v.mimeType = mimeType;
        return v;
    }
};

// Access control is enforced by OS-native backends; the memory and file
// backends store the ACL but cannot enforce caller identity themselves.
struct SecretAcl {
    std::string ownerAppId;            // empty -> current app
    std::vector<std::string> readers;  // additional app ids allowed to read
    bool requiresUserPresence = false; // bio / password prompt on every read
};

// ============================================================================
// Configuration / lifecycle
// ============================================================================
enum class Backend : uint8_t {
    Auto,     // File when a filePath (or ULTRAVAULT_FILE env) is configured,
              // Memory otherwise
    Memory,
    File
    // Libsecret, Keychain, CredMan, Native: planned
};

struct Config {
    Backend backend = Backend::Auto;

    // File backend: path of the encrypted vault file (created on first
    // mutation if absent) and the passphrase its key is derived from.
    // Initialize() wipes `passphrase` in place after adopting it, so the
    // secret does not linger in the caller's Config.
    std::string filePath;
    std::string passphrase;
};

// Open the vault. Idempotent per process: a second call while open returns
// Success without reconfiguring (Shutdown() first to switch backends).
// The no-argument form resolves Backend::Auto from the environment
// (ULTRAVAULT_FILE / ULTRAVAULT_PASSPHRASE) and falls back to Memory.
Result Initialize();
Result Initialize(Config& config);

// Wipe the decrypted store and derived key from memory and close the vault.
void Shutdown();

// True while the vault is open and its backend can operate.
bool IsAvailable();

// Name of the active backend ("memory", "file"); empty when closed.
std::string GetBackendName();

// ============================================================================
// Core operations
// ============================================================================
// Keys use the namespaced convention "<vendor>.<app>.<purpose>", e.g.
// "ai.anthropic.api_key" (UltraAI/Docs/UltraVault.md §4). Enforced loosely:
// non-empty, printable ASCII, no whitespace.
Result Put(const std::string& key, const SecretValue& value,
           const SecretAcl& acl = {});
Result Get(const std::string& key, SecretValue& outValue);
Result Delete(const std::string& key);

// Keys matching the prefix (all keys for ""), sorted. Empty when closed.
std::vector<std::string> List(const std::string& prefix = "");

// ============================================================================
// Bootstrap / migration — not implemented in v0.1
// ============================================================================
Result Import(const std::string& sourcePath);
Result PromptUserForSecret(const std::string& key,
                           const std::string& displayPrompt);

} // namespace UltraVault

#endif // ULTRAVAULT_H
