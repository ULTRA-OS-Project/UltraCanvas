// Plugins/UltraIM/direct/DirectCrypto.cpp
// ChatCube Direct - identity keys, the end-to-end message envelope, signed signalling
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "DirectCrypto.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasTextUtils.h"

#include <algorithm>

namespace UltraIMDirect {

namespace {

constexpr auto kSignature = UltraCryptSignatureAlgorithm::Ed25519;
const char* const kMessageKeyInfo = "chatcube-msg-v1";

std::string AssociatedData(const Envelope& e) {
    return "chatcube-env-v1|" + e.id + "|" + e.from + "|" + e.to + "|" + e.conversation + "|" +
           std::to_string(e.timestampMs);
}

bool CompleteIdentity(Identity& id, std::string& error) {
    if (!UltraCrypt_SignaturePublicKeyToKeyExchange(kSignature, id.signPublic, id.exchangePublic) ||
        !UltraCrypt_SignatureSecretKeyToKeyExchange(kSignature, id.signSecret, id.exchangeSecret)) {
        error = "Could not derive the key-exchange key";
        return false;
    }
    id.userId = UserIdFor(id.signPublic);
    return true;
}

} // namespace

std::string UserIdFor(const std::vector<uint8_t>& signPublic) {
    std::vector<uint8_t> digest;
    if (!UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, signPublic.data(), signPublic.size(), digest) ||
        digest.size() < 16)
        return std::string();
    digest.resize(16);
    return UltraCrypt_ToHex(digest);
}

bool CreateIdentity(Identity& out, std::string& error) {
    Identity id;
    if (!UltraCrypt_GenerateSignatureKeyPair(kSignature, id.signPublic, id.signSecret)) {
        error = "Could not generate a key pair (is UltraCrypt built with libsodium?)";
        return false;
    }
    if (!CompleteIdentity(id, error)) return false;
    out = std::move(id);
    return true;
}

bool IdentityFromSecret(const UltraCryptSecureBuffer& signSecret, Identity& out, std::string& error) {
    Identity id;
    id.signSecret = signSecret.Clone();
    if (!UltraCrypt_GetSignaturePublicKey(kSignature, id.signSecret, id.signPublic)) {
        error = "The stored identity key is damaged";
        return false;
    }
    if (!CompleteIdentity(id, error)) return false;
    out = std::move(id);
    return true;
}

std::string Base64(const std::vector<uint8_t>& bytes) {
    return UltraCanvas::Base64Encode(bytes, false);
}

std::vector<uint8_t> FromBase64(const std::string& text) {
    return UltraCanvas::Base64Decode(text);
}

std::string Sign(const Identity& me, const std::string& message) {
    std::vector<uint8_t> signature;
    if (!UltraCrypt_Sign(kSignature, me.signSecret, message.data(), message.size(), signature)) return std::string();
    return Base64(signature);
}

bool Verify(const std::vector<uint8_t>& signPublic, const std::string& message, const std::string& signatureBase64) {
    const std::vector<uint8_t> signature = FromBase64(signatureBase64);
    return static_cast<bool>(UltraCrypt_Verify(kSignature, signPublic, message.data(), message.size(), signature));
}

std::string RegisterMessage(const std::string& publicKeyBase64, const std::string& displayName) {
    return "chatcube-register-v1\n" + publicKeyBase64 + "\n" + displayName;
}

std::string LoginMessage(const std::string& nonceBase64) {
    return "chatcube-login-v1\n" + nonceBase64;
}

std::string SignalMessage(const std::string& from, const std::string& to, const std::string& session,
                          const std::string& kind, const std::string& data) {
    return "chatcube-signal-v1\n" + from + "\n" + to + "\n" + session + "\n" + kind + "\n" + data;
}

// ============================================================================
// ENVELOPE
// ============================================================================

std::string Envelope::ToJson() const {
    UltraCanvas::JSONValue o = UltraCanvas::JSONValue::MakeObject();
    o.Set("v", UltraCanvas::JSONValue(version));
    o.Set("id", UltraCanvas::JSONValue(id));
    o.Set("from", UltraCanvas::JSONValue(from));
    o.Set("to", UltraCanvas::JSONValue(to));
    o.Set("conv", UltraCanvas::JSONValue(conversation));
    o.Set("ts", UltraCanvas::JSONValue(static_cast<int64_t>(timestampMs)));
    o.Set("nonce", UltraCanvas::JSONValue(nonce));
    o.Set("ct", UltraCanvas::JSONValue(ciphertext));
    return UltraCanvas::JSON::Serialize(o);
}

bool Envelope::FromJson(const std::string& json, Envelope& out) {
    UltraCanvas::JSONParseResult parsed;
    UltraCanvas::JSONValue o = UltraCanvas::JSON::Parse(json, &parsed);
    if (!parsed.success || !o.IsObject()) return false;
    Envelope e;
    e.version = static_cast<int>(o["v"].GetInteger(0));
    e.id = o["id"].GetString();
    e.from = o["from"].GetString();
    e.to = o["to"].GetString();
    e.conversation = o["conv"].GetString();
    e.timestampMs = o["ts"].GetInteger(0);
    e.nonce = o["nonce"].GetString();
    e.ciphertext = o["ct"].GetString();
    if (e.version != 1 || e.id.empty() || e.from.empty() || e.to.empty() || e.nonce.empty()) return false;
    out = std::move(e);
    return true;
}

// ============================================================================
// KEY RING
// ============================================================================

bool KeyRing::KeyFor(const Identity& me, const std::vector<uint8_t>& theirSignPublic,
                     const std::string& theirUserId, UltraCryptSecureBuffer& outKey, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = keys_.find(theirUserId);
    if (it != keys_.end()) {
        outKey = it->second.Clone();
        return true;
    }
    std::vector<uint8_t> theirExchange;
    if (!UltraCrypt_SignaturePublicKeyToKeyExchange(kSignature, theirSignPublic, theirExchange)) {
        error = "The contact's key is not a valid identity key";
        return false;
    }
    UltraCryptSecureBuffer shared;
    if (!UltraCrypt_KeyExchange(me.exchangeSecret, theirExchange, shared)) {
        error = "Key agreement with the contact failed";
        return false;
    }
    const std::string first = std::min(me.userId, theirUserId);
    const std::string second = std::max(me.userId, theirUserId);
    const std::string saltText = first + second;
    const std::vector<uint8_t> salt(saltText.begin(), saltText.end());
    const std::string infoText = kMessageKeyInfo;
    const std::vector<uint8_t> info(infoText.begin(), infoText.end());
    UltraCryptSecureBuffer key;
    if (!UltraCrypt_DeriveKeyHkdf(UltraCryptHashAlgorithm::SHA256, shared, salt, info,
                                  UltraCrypt_GetKeySize(UltraCryptAeadAlgorithm::XChaCha20Poly1305), key)) {
        error = "Message key derivation failed";
        return false;
    }
    outKey = key.Clone();
    keys_[theirUserId] = std::move(key);
    return true;
}

bool KeyRing::Seal(const Identity& me, const std::vector<uint8_t>& theirSignPublic,
                   const std::string& to, const std::string& conversation, const std::string& id,
                   int64_t timestampMs, const std::string& body, Envelope& out, std::string& error) {
    if (UserIdFor(theirSignPublic) != to) {
        error = "The key does not belong to the recipient";
        return false;
    }
    UltraCryptSecureBuffer key;
    if (!KeyFor(me, theirSignPublic, to, key, error)) return false;

    Envelope e;
    e.id = id;
    e.from = me.userId;
    e.to = to;
    e.conversation = conversation;
    e.timestampMs = timestampMs;
    const std::string ad = AssociatedData(e);
    UltraCryptAeadParams params;
    params.algorithm = UltraCryptAeadAlgorithm::XChaCha20Poly1305;
    params.associatedData.assign(ad.begin(), ad.end());
    std::vector<uint8_t> ciphertext;
    if (!UltraCrypt_AeadSeal(key, params, body.data(), body.size(), ciphertext)) {
        error = "Encryption failed";
        return false;
    }
    e.nonce = Base64(params.nonce);
    e.ciphertext = Base64(ciphertext);
    out = std::move(e);
    return true;
}

bool KeyRing::Open(const Identity& me, const std::vector<uint8_t>& senderSignPublic,
                   const Envelope& envelope, std::string& body, std::string& error) {
    if (envelope.to != me.userId) {
        error = "The message is not addressed to this identity";
        return false;
    }
    if (UserIdFor(senderSignPublic) != envelope.from) {
        error = "The sender's key does not match the sender";
        return false;
    }
    UltraCryptSecureBuffer key;
    if (!KeyFor(me, senderSignPublic, envelope.from, key, error)) return false;

    const std::string ad = AssociatedData(envelope);
    UltraCryptAeadParams params;
    params.algorithm = UltraCryptAeadAlgorithm::XChaCha20Poly1305;
    params.nonce = FromBase64(envelope.nonce);
    params.associatedData.assign(ad.begin(), ad.end());
    const std::vector<uint8_t> ciphertext = FromBase64(envelope.ciphertext);
    UltraCryptSecureBuffer plain;
    if (!UltraCrypt_AeadOpen(key, params, ciphertext.data(), ciphertext.size(), plain)) {
        error = "The message could not be decrypted or was altered";
        return false;
    }
    body.assign(reinterpret_cast<const char*>(plain.Data()), plain.GetSize());
    return true;
}

void KeyRing::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    keys_.clear();
}

} // namespace UltraIMDirect
