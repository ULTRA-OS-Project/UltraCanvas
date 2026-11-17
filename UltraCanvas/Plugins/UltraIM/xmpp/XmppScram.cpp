// Plugins/UltraIM/xmpp/XmppScram.cpp
// XMPP - SCRAM-SHA-256 and SCRAM-SHA-1, the client's half (RFC 5802)
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework

#include "XmppScram.h"

#include "XmppXml.h"

#include "UltraCrypt/UltraCryptCore.h"

#include <map>

namespace UltraIMXmpp {

namespace {

UltraCryptHashAlgorithm AlgorithmOf(ScramHash hash) {
    return hash == ScramHash::Sha1 ? UltraCryptHashAlgorithm::SHA1 : UltraCryptHashAlgorithm::SHA256;
}

std::vector<uint8_t> Hmac(ScramHash hash, const std::vector<uint8_t>& key, const std::string& data) {
    UltraCryptSecureBuffer k(key.data(), key.size());
    std::vector<uint8_t> out;
    UltraCrypt_Hmac(AlgorithmOf(hash), k, data.data(), data.size(), out);
    return out;
}

std::vector<uint8_t> Hmac(ScramHash hash, const std::vector<uint8_t>& key, const std::vector<uint8_t>& data) {
    return Hmac(hash, key, std::string(data.begin(), data.end()));
}

std::vector<uint8_t> Digest(ScramHash hash, const std::vector<uint8_t>& data) {
    // SCRAM-SHA-1's StoredKey is a plain SHA-1 of a key: a key derivation,
    // which SHA-1's collisions do not reach (UltraCrypt asks for the opt-in).
    UltraCryptHashOptions options;
    options.allowLegacy = hash == ScramHash::Sha1;
    std::vector<uint8_t> out;
    UltraCrypt_Hash(AlgorithmOf(hash), data.data(), data.size(), out, options);
    return out;
}

// Hi() of RFC 5802 2.2: PBKDF2 with the hash's HMAC and one block - written
// out on UltraCrypt_Hmac, since UltraCrypt's PBKDF2 refuses SHA-1.
std::vector<uint8_t> Hi(ScramHash hash, const std::string& password, const std::vector<uint8_t>& salt, uint32_t iterations) {
    const std::vector<uint8_t> key(password.begin(), password.end());
    std::vector<uint8_t> block = salt;
    block.insert(block.end(), {0, 0, 0, 1});
    std::vector<uint8_t> u = Hmac(hash, key, block);
    std::vector<uint8_t> result = u;
    for (uint32_t i = 1; i < iterations; ++i) {
        u = Hmac(hash, key, u);
        for (size_t j = 0; j < result.size() && j < u.size(); ++j) result[j] ^= u[j];
    }
    return result;
}

// "r=...,s=...,i=4096" -> {r: ..., s: ..., i: ...}
std::map<char, std::string> Attributes(const std::string& message) {
    std::map<char, std::string> out;
    size_t start = 0;
    while (start < message.size()) {
        size_t comma = message.find(',', start);
        if (comma == std::string::npos) comma = message.size();
        const std::string part = message.substr(start, comma - start);
        if (part.size() >= 2 && part[1] == '=') out[part[0]] = part.substr(2);
        start = comma + 1;
    }
    return out;
}

// SCRAM's saslname: '=' and ',' escaped.
std::string SaslName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '=') out += "=3D";
        else if (c == ',') out += "=2C";
        else out += c;
    }
    return out;
}

} // namespace

const char* ScramMechanismName(ScramHash hash) {
    return hash == ScramHash::Sha1 ? "SCRAM-SHA-1" : "SCRAM-SHA-256";
}

ScramClient::ScramClient(ScramHash hash, const std::string& username)
    : hash_(hash), user_(SaslName(username)), nonce_(RandomId(24)) {
    keys_.hash = hash;
}

std::string ScramClient::ClientFirst() const {
    return "n,,n=" + user_ + ",r=" + nonce_;
}

bool ScramClient::ClientFinal(const std::string& serverFirst, const std::string& password, const ScramKeys* stored,
                              std::string& out, std::string& error, bool& keysOutdated) {
    keysOutdated = false;
    const auto attrs = Attributes(serverFirst);
    auto r = attrs.find('r'), s = attrs.find('s'), i = attrs.find('i');
    if (attrs.count('m') || r == attrs.end() || s == attrs.end() || i == attrs.end()) {
        error = "The server's SCRAM challenge is not one this client reads";
        return false;
    }
    // The server's nonce goes on from ours: anything else is not an answer to us.
    if (r->second.size() <= nonce_.size() || r->second.compare(0, nonce_.size(), nonce_) != 0) {
        error = "The server's SCRAM nonce does not continue ours";
        return false;
    }
    const std::vector<uint8_t> salt = Unbase64(s->second);
    long iterations = 0;
    try {
        iterations = std::stol(i->second);
    } catch (...) {
        iterations = 0;
    }
    // A count past a million is a server making the client burn time.
    if (salt.empty() || iterations < 1 || iterations > 1000000) {
        error = "The server's SCRAM salt or iteration count is not usable";
        return false;
    }
    keys_.hash = hash_;
    keys_.salt = salt;
    keys_.iterations = static_cast<uint32_t>(iterations);
    if (!password.empty()) {
        const std::vector<uint8_t> salted = Hi(hash_, password, salt, keys_.iterations);
        keys_.clientKey = Hmac(hash_, salted, "Client Key");
        keys_.serverKey = Hmac(hash_, salted, "Server Key");
    } else if (stored && stored->Valid() && stored->hash == hash_ && stored->salt == salt &&
               stored->iterations == keys_.iterations) {
        keys_.clientKey = stored->clientKey;
        keys_.serverKey = stored->serverKey;
    } else {
        keysOutdated = stored != nullptr;
        error = "The server asks for the password again";
        return false;
    }
    const std::string clientFirstBare = "n=" + user_ + ",r=" + nonce_;
    const std::string withoutProof = "c=biws,r=" + r->second;     // biws = base64("n,,")
    authMessage_ = clientFirstBare + "," + serverFirst + "," + withoutProof;
    const std::vector<uint8_t> storedKey = Digest(hash_, keys_.clientKey);
    const std::vector<uint8_t> signature = Hmac(hash_, storedKey, authMessage_);
    std::vector<uint8_t> proof = keys_.clientKey;
    for (size_t j = 0; j < proof.size() && j < signature.size(); ++j) proof[j] ^= signature[j];
    out = withoutProof + ",p=" + Base64(proof);
    return true;
}

bool ScramClient::VerifyServerFinal(const std::string& serverFinal) const {
    const auto attrs = Attributes(serverFinal);
    auto v = attrs.find('v');
    if (v == attrs.end() || authMessage_.empty()) return false;
    const std::vector<uint8_t> expected = Hmac(hash_, keys_.serverKey, authMessage_);
    const std::vector<uint8_t> got = Unbase64(v->second);
    return got.size() == expected.size() &&
           UltraCrypt_ConstantTimeEquals(got.data(), got.size(), expected.data(), expected.size());
}

} // namespace UltraIMXmpp
