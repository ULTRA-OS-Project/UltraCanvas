// Plugins/UltraIM/direct/DirectCrypto.h
// ChatCube Direct - identity keys, the end-to-end message envelope, signed signalling
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraCrypt/UltraCryptCore.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace UltraIMDirect {

// One Ed25519 key pair is the whole identity: it signs (login, signalling)
// and, converted to X25519, agrees on message keys.
struct Identity {
    std::vector<uint8_t>   signPublic;     // 32 bytes, Ed25519
    UltraCryptSecureBuffer signSecret;     // 64 bytes, Ed25519
    std::vector<uint8_t>   exchangePublic; // 32 bytes, X25519
    UltraCryptSecureBuffer exchangeSecret; // 32 bytes, X25519
    std::string            userId;         // hex of SHA-256(signPublic)[0..16)

    bool IsValid() const { return signPublic.size() == 32 && signSecret.GetSize() == 64; }
};

// lowercase hex of the first 16 bytes of SHA-256 of an Ed25519 public key.
std::string UserIdFor(const std::vector<uint8_t>& signPublic);

bool CreateIdentity(Identity& out, std::string& error);
// From the 64-byte secret key (what the vault keeps).
bool IdentityFromSecret(const UltraCryptSecureBuffer& signSecret, Identity& out, std::string& error);

std::string Base64(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> FromBase64(const std::string& text);

// Detached Ed25519 signature of `message`, base64.
std::string Sign(const Identity& me, const std::string& message);
bool Verify(const std::vector<uint8_t>& signPublic, const std::string& message, const std::string& signatureBase64);

// What the relay signs for registration and login (Server/ChatCubeRelay/README.md).
std::string RegisterMessage(const std::string& publicKeyBase64, const std::string& displayName);
std::string LoginMessage(const std::string& nonceBase64);
// What a signalling payload is signed over: who, to whom, which link, what.
std::string SignalMessage(const std::string& from, const std::string& to, const std::string& session,
                          const std::string& kind, const std::string& data);
// What each side of a local-network connection signs to prove its identity:
// the other side's fresh nonce, then who signs and for whom.
std::string LanAuthMessage(const std::string& nonceBase64, const std::string& signer, const std::string& verifier);

// ============================================================================
// ENVELOPE
// The same on every path (data channel, local network, relay). Everything
// but `ct` is the header, authenticated as associated data; `ct` is the
// XChaCha20-Poly1305 encryption of the body JSON under a key only the two
// parties can derive.
// ============================================================================

struct Envelope {
    int         version = 1;
    std::string id;             // client message id
    std::string from;
    std::string to;
    std::string conversation;
    int64_t     timestampMs = 0;
    std::string nonce;          // base64
    std::string ciphertext;     // base64

    std::string ToJson() const;
    static bool FromJson(const std::string& json, Envelope& out);
};

// Message keys, cached per peer: HKDF-SHA256 of the X25519 secret, salted
// with both user ids in order, so the key is bound to the pair.
class KeyRing {
public:
    // Seals `body` from `me` to the holder of `theirSignPublic`.
    bool Seal(const Identity& me, const std::vector<uint8_t>& theirSignPublic,
              const std::string& to, const std::string& conversation, const std::string& id,
              int64_t timestampMs, const std::string& body, Envelope& out, std::string& error);
    // Opens an envelope addressed to `me`, sent by the holder of `senderSignPublic`.
    bool Open(const Identity& me, const std::vector<uint8_t>& senderSignPublic,
              const Envelope& envelope, std::string& body, std::string& error);
    void Clear();

private:
    bool KeyFor(const Identity& me, const std::vector<uint8_t>& theirSignPublic,
                const std::string& theirUserId, UltraCryptSecureBuffer& outKey, std::string& error);

    std::mutex mutex_;
    std::map<std::string, UltraCryptSecureBuffer> keys_;   // their user id -> key
};

} // namespace UltraIMDirect
