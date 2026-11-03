// Plugins/UltraIM/matrix/MatrixOlm.h
// Matrix provider - C++ owners of vodozemac's Olm and Megolm objects (through
// the ultraim_olm Rust shim, olm-c/). Built without the shim (no Rust
// toolchain), OlmAvailable() is false and every object stays empty.
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

struct UimOlmAccount;
struct UimOlmSession;
struct UimMegolmOutbound;
struct UimMegolmInbound;

namespace UltraIMMatrix {

using PickleKey = std::array<uint8_t, 32>;

bool OlmAvailable();
std::string OlmVersion();
// Why the last failing call on this thread failed.
std::string OlmLastError();

// ed25519 signature check (Matrix's unpadded base64 key and signature).
bool OlmVerifyEd25519(const std::string& key, const std::string& message, const std::string& signature);

class OlmSession;

// This device: its identity keys, one-time keys and fallback key. Not
// thread-safe; the crypto machine guards it.
class OlmAccount {
public:
    OlmAccount();       // empty: Create() or Unpickle()
    ~OlmAccount();
    OlmAccount(OlmAccount&&) noexcept;
    OlmAccount& operator=(OlmAccount&&) noexcept;

    bool Create();
    bool Unpickle(const std::string& pickle, const PickleKey& key);
    std::string Pickle(const PickleKey& key) const;
    bool IsValid() const { return account_ != nullptr; }

    std::string Curve25519() const;
    std::string Ed25519() const;
    std::string Sign(const std::string& message) const;

    size_t MaxOneTimeKeys() const;
    void GenerateOneTimeKeys(size_t count);
    std::map<std::string, std::string> OneTimeKeys() const;     // unpublished: id -> key
    void GenerateFallbackKey();
    std::map<std::string, std::string> FallbackKey() const;     // unpublished
    void MarkKeysAsPublished();

    OlmSession OutboundSession(const std::string& theirIdentityKey, const std::string& theirOneTimeKey) const;
    OlmSession InboundSession(const std::string& theirIdentityKey, const std::string& preKeyMessage,
                              std::string& plaintext);

private:
    UimOlmAccount* account_ = nullptr;
};

// A double ratchet with one other device.
class OlmSession {
public:
    OlmSession() = default;
    explicit OlmSession(UimOlmSession* session) : session_(session) {}
    ~OlmSession();
    OlmSession(OlmSession&&) noexcept;
    OlmSession& operator=(OlmSession&&) noexcept;

    bool Unpickle(const std::string& pickle, const PickleKey& key);
    std::string Pickle(const PickleKey& key) const;
    bool IsValid() const { return session_ != nullptr; }

    std::string Id() const;
    bool Matches(const std::string& preKeyMessage) const;
    bool HasReceivedMessage() const;
    // Message type (0 pre-key, 1 normal) and body, or -1.
    int Encrypt(const std::string& plaintext, std::string& body);
    bool Decrypt(int messageType, const std::string& body, std::string& plaintext);

private:
    UimOlmSession* session_ = nullptr;
};

// Our own session for a room: what we encrypt with.
class MegolmOutbound {
public:
    MegolmOutbound() = default;
    ~MegolmOutbound();
    MegolmOutbound(MegolmOutbound&&) noexcept;
    MegolmOutbound& operator=(MegolmOutbound&&) noexcept;

    bool Create();
    bool Unpickle(const std::string& pickle, const PickleKey& key);
    std::string Pickle(const PickleKey& key) const;
    bool IsValid() const { return session_ != nullptr; }

    std::string Id() const;
    std::string SessionKey() const;
    uint32_t MessageIndex() const;
    std::string Encrypt(const std::string& plaintext);

private:
    UimMegolmOutbound* session_ = nullptr;
};

// Someone's session for a room (our own too): what we decrypt with.
class MegolmInbound {
public:
    MegolmInbound() = default;
    ~MegolmInbound();
    MegolmInbound(MegolmInbound&&) noexcept;
    MegolmInbound& operator=(MegolmInbound&&) noexcept;

    bool Create(const std::string& sessionKey);
    bool Import(const std::string& exportedKey);
    bool Unpickle(const std::string& pickle, const PickleKey& key);
    std::string Pickle(const PickleKey& key) const;
    bool IsValid() const { return session_ != nullptr; }

    std::string Id() const;
    uint32_t FirstKnownIndex() const;
    std::string ExportAt(uint32_t index);
    bool Decrypt(const std::string& ciphertext, std::string& plaintext, uint32_t& messageIndex);

private:
    UimMegolmInbound* session_ = nullptr;
};

} // namespace UltraIMMatrix
