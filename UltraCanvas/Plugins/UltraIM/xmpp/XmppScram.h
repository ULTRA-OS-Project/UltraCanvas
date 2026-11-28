// Plugins/UltraIM/xmpp/XmppScram.h
// XMPP - the client side of SASL SCRAM-SHA-256 and SCRAM-SHA-1 (RFC 5802,
// RFC 7677), on UltraCrypt's hashes and HMAC.
//
// A sign-in with the password derives SaltedPassword = Hi(password, salt,
// iterations) and from it the ClientKey and the ServerKey. Those two (with
// the salt and the iteration count they belong to) are all a later sign-in
// needs - SCRAM proves the ClientKey, and the ServerKey checks the server -
// so they are what the provider keeps, never the password. They are the
// password's equivalent for this one account on this one server (the server
// stores StoredKey = H(ClientKey), so they never leave the device).
//
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraIMXmpp {

enum class ScramHash { Sha1, Sha256 };

// "SCRAM-SHA-256" / "SCRAM-SHA-1".
const char* ScramMechanismName(ScramHash hash);

struct ScramKeys {
    ScramHash            hash = ScramHash::Sha256;
    std::vector<uint8_t> salt;
    uint32_t             iterations = 0;
    std::vector<uint8_t> clientKey;
    std::vector<uint8_t> serverKey;

    bool Valid() const { return iterations > 0 && !clientKey.empty() && !serverKey.empty(); }
};

class ScramClient {
public:
    // username: the JID's local part (escaped here as SCRAM wants it).
    ScramClient(ScramHash hash, const std::string& username);

    // "n,,n=<user>,r=<nonce>" - the first message, before base64.
    std::string ClientFirst() const;
    // The answer to the server's first message (r=, s=, i=): with the
    // password, or with stored keys when their salt and iteration count are
    // the server's. False with error set when neither works; keysOutdated
    // when stored keys were all there was and the server's salt or count
    // differs (the password was changed: ask for it).
    bool ClientFinal(const std::string& serverFirst, const std::string& password, const ScramKeys* stored,
                     std::string& out, std::string& error, bool& keysOutdated);
    // The server's last message (v=<signature>) proves it knows the password too.
    bool VerifyServerFinal(const std::string& serverFinal) const;
    // The keys the sign-in used (to keep for the next one).
    const ScramKeys& Keys() const { return keys_; }

private:
    ScramHash   hash_;
    std::string user_;
    std::string nonce_;
    std::string authMessage_;
    ScramKeys   keys_;
};

} // namespace UltraIMXmpp
