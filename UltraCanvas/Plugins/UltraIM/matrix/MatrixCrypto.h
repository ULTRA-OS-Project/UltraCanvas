// Plugins/UltraIM/matrix/MatrixCrypto.h
// Matrix provider - end-to-end encryption: this device's Olm account and its
// keys on the homeserver, Olm sessions with other devices (to-device
// messages), Megolm sessions per room, other users' device lists trusted on
// first use, and room-key export files. The cryptography itself is
// vodozemac's (MatrixOlm.h); this is the bookkeeping the Matrix spec puts
// around it (m.olm.v1.curve25519-aes-sha2, m.megolm.v1.aes-sha2).
//
// Everything is kept in one internal file, encrypted (XChaCha20-Poly1305)
// with a key the provider keeps in its vault.
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include "MatrixHttp.h"
#include "MatrixOlm.h"

#include "DataFormats/UltraCanvasJSON.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace UltraIMMatrix {

// Matrix's canonical JSON (what is signed): object keys sorted, no
// whitespace, strings escaped only where JSON must be.
std::string CanonicalJson(const UltraCanvas::JSONValue& value);

struct MatrixDeviceInfo {
    std::string userId;
    std::string deviceId;
    std::string curve25519;
    std::string ed25519;
    std::string displayName;
    // Its ed25519 key is not the one first seen for it: no room keys go to
    // it, and what it sends is not believed.
    bool        conflicted = false;
};

enum class MatrixDecryptState {
    Ok,
    MissingKey,     // no session for it yet: the key may still arrive
    Failed          // will not decrypt (see error)
};

struct MatrixDecrypted {
    MatrixDecryptState    state = MatrixDecryptState::Failed;
    // The event as if it had been sent in the clear: the outer event with the
    // payload's type and content (and the outer m.relates_to, which an
    // encrypted event carries outside).
    UltraCanvas::JSONValue event;
    std::string           sessionId;
    std::string           error;
};

// A room's m.room.encryption settings.
struct MatrixRoomEncryption {
    int64_t rotationPeriodMs = 7LL * 24 * 3600 * 1000;
    int     rotationPeriodMessages = 100;
};

// A room's session that has just become known: (room id, session id).
using MatrixSessionRef = std::pair<std::string, std::string>;

class MatrixCrypto {
public:
    MatrixCrypto() = default;
    MatrixCrypto(const MatrixCrypto&) = delete;
    MatrixCrypto& operator=(const MatrixCrypto&) = delete;

    // Opens this device's store (a new account when the file is not there).
    // storeKey: 32 bytes. False when the file exists but does not open with
    // the key, or vodozemac is not built in.
    bool Open(const std::string& storePath, const std::vector<uint8_t>& storeKey, const std::string& userId,
              const std::string& deviceId, std::string& error);
    void Close();
    bool IsOpen() const;
    static bool StoreExists(const std::string& storePath);

    std::string GetUserId() const;
    std::string GetDeviceId() const;
    std::string GetCurve25519() const;
    std::string GetEd25519() const;

    // ---- network steps: they wait, so only on the provider's own threads ----

    // Publishes the device keys (the first time) and tops the one-time keys
    // and fallback key up. serverKeyCount: the server's count of our unused
    // one-time keys (-1 = not known); fallbackUnused: whether our fallback
    // key is still unused (true when not known).
    bool UploadKeys(MatrixHttp& http, int64_t serverKeyCount, bool fallbackUnused, std::string& error);

    // A /sync answer: to-device messages (room keys), device list changes and
    // one-time key counts (uploading more when they run low). Returns the
    // room sessions it brought.
    std::vector<MatrixSessionRef> HandleSync(MatrixHttp& http, const UltraCanvas::JSONValue& sync);

    // Fetches the device lists of users not tracked yet, or changed since.
    bool UpdateDeviceLists(MatrixHttp& http, const std::vector<std::string>& users, std::string& error);

    // The content of an m.room.encrypted event for (type, content) in a room,
    // after the room's session key went to every device of `members` that
    // does not have it yet. False (and error) when that could not be done.
    bool EncryptRoomEvent(MatrixHttp& http, const std::string& roomId, const std::vector<std::string>& members,
                          const MatrixRoomEncryption& settings, const std::string& type,
                          const UltraCanvas::JSONValue& content, UltraCanvas::JSONValue& encryptedContent,
                          std::string& error);

    // ---- local -------------------------------------------------------------

    // An m.room.encrypted timeline event. No network.
    MatrixDecrypted DecryptRoomEvent(const std::string& roomId, const UltraCanvas::JSONValue& event);
    // Marks every tracked user's list as changed (at sign-in: the changes in
    // between are not known).
    void InvalidateDeviceLists();
    // Writes what decrypting recorded (replay guards) to the store.
    void Flush();
    std::vector<MatrixDeviceInfo> DevicesOf(const std::string& userId) const;
    bool HasSession(const std::string& roomId, const std::string& sessionId) const;

    // Room keys as an export file ("-----BEGIN MEGOLM SESSION DATA-----", the
    // format Element and other clients read and write), protected by a
    // passphrase (PBKDF2-SHA-512, AES-256-CTR, HMAC-SHA-256).
    bool ExportRoomKeys(const std::string& passphrase, uint32_t rounds, std::string& out, std::string& error);
    // Imports the sessions such a file holds; their refs in `imported`.
    bool ImportRoomKeys(const std::string& data, const std::string& passphrase,
                        std::vector<MatrixSessionRef>& imported, std::string& error);

private:
    struct StoredOlm {
        OlmSession session;
        int64_t    lastUsedMs = 0;
    };
    struct Outbound {
        MegolmOutbound session;
        int64_t        createdMs = 0;
        int            messages = 0;
        std::set<std::string> sharedWith;       // "user|device"
    };
    struct Inbound {
        MegolmInbound session;
        std::string   roomId;
        std::string   senderKey;        // curve25519 of the device that sent us the key
        std::string   claimedEd25519;
        std::string   senderUser;       // who the key came from ("" when imported)
    };

    // ---- mutex held -------------------------------------------------------------
    bool LoadLocked(const std::string& text, std::string& error);
    std::string SerializeLocked() const;
    void SaveLocked();
    UltraCanvas::JSONValue SignedLocked(UltraCanvas::JSONValue object) const;
    bool VerifyDeviceKeysLocked(const UltraCanvas::JSONValue& keys, const std::string& userId,
                                const std::string& deviceId, MatrixDeviceInfo& out) const;
    const MatrixDeviceInfo* DeviceByCurveLocked(const std::string& curve) const;
    // Decrypts one olm to-device event; returns the payload (empty on failure).
    UltraCanvas::JSONValue DecryptToDeviceLocked(const UltraCanvas::JSONValue& event, std::string& error);
    bool AddInboundLocked(const std::string& roomId, const std::string& sessionKey, bool exported,
                          const std::string& sessionId, const std::string& senderKey, const std::string& claimedEd,
                          const std::string& senderUser);
    OlmSession* SessionForLocked(const std::string& curve);

    mutable std::mutex mutex_;
    std::mutex         shareMutex_;     // one EncryptRoomEvent at a time
    bool               open_ = false;
    std::string        path_;
    std::vector<uint8_t> storeKey_;
    PickleKey          pickleKey_{};
    std::string        userId_;
    std::string        deviceId_;
    OlmAccount         account_;
    bool               deviceKeysUploaded_ = false;
    std::map<std::string, std::vector<StoredOlm>> olm_;             // their curve25519 -> sessions
    std::map<std::string, Outbound> outbound_;                       // room -> our session
    std::map<std::string, Inbound> inbound_;                         // room "|" session id
    std::map<std::string, std::map<std::string, MatrixDeviceInfo>> devices_;   // user -> device -> keys
    std::map<std::string, bool> tracked_;                            // user -> list outdated
    std::map<std::string, std::string> seen_;                        // session "|" index -> event id
    bool               dirty_ = false;
};

} // namespace UltraIMMatrix
