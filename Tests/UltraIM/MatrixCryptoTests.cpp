// Tests/UltraIM/MatrixCryptoTests.cpp
// Matrix provider's end-to-end encryption without a homeserver: canonical
// JSON (the spec's examples), vodozemac through the shim (an Olm handshake
// both ways, Megolm with exported keys, pickles), the encrypted key store,
// room events decrypted with replay and room checks, and room-key export
// files (a file written here from the format's description, imported; an
// export imported elsewhere; a wrong passphrase and a changed byte refused).
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "MatrixCrypto.h"
#include "MatrixOlm.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCrypt/UltraCryptCore.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraIMMatrix;
using UltraCanvas::JSONValue;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

namespace fs = std::filesystem;

JSONValue Parse(const std::string& text) {
    UltraCanvas::JSONParseResult parsed;
    return UltraCanvas::JSON::Parse(text, &parsed);
}

void TestCanonicalJson() {
    std::printf("canonical JSON (the spec's examples)\n");
    struct Case { const char* in; const char* out; };
    const Case cases[] = {
        {"{}", "{}"},
        {"{\"one\": 1, \"two\": \"Two\"}", "{\"one\":1,\"two\":\"Two\"}"},
        {"{\"b\": \"2\", \"a\": \"1\"}", "{\"a\":\"1\",\"b\":\"2\"}"},
        {"{\"auth\": {\"success\": true, \"mxid\": \"@john.doe:example.com\", \"profile\": {\"display_name\": "
         "\"John Doe\", \"three_pids\": [{\"medium\": \"email\", \"address\": \"john.doe@example.org\"}, "
         "{\"medium\": \"msisdn\", \"address\": \"123456789\"}]}}}",
         "{\"auth\":{\"mxid\":\"@john.doe:example.com\",\"profile\":{\"display_name\":\"John Doe\",\"three_pids\":"
         "[{\"address\":\"john.doe@example.org\",\"medium\":\"email\"},{\"address\":\"123456789\",\"medium\":"
         "\"msisdn\"}]},\"success\":true}}"},
        {"{\"a\": \"\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\"}", "{\"a\":\"\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\"}"},
        {"{\"\xE6\x9C\xAC\": 2, \"\xE6\x97\xA5\": 1}", "{\"\xE6\x97\xA5\":1,\"\xE6\x9C\xAC\":2}"},
        {"{\"a\": \"\\u65E5\"}", "{\"a\":\"\xE6\x97\xA5\"}"},
        {"{\"a\": null}", "{\"a\":null}"},
        {"{\"a\": \"line\\nbreak \\\"quoted\\\" \\\\ \\u0001\"}", "{\"a\":\"line\\nbreak \\\"quoted\\\" \\\\ \\u0001\"}"},
    };
    for (const Case& c : cases) {
        const std::string got = CanonicalJson(Parse(c.in));
        if (got != c.out) std::fprintf(stderr, "  canonical: %s\n    -> %s\n", c.in, got.c_str());
        CHECK(got == c.out);
    }
}

void TestOlm() {
    std::printf("Olm and Megolm through the shim (%s)\n", OlmVersion().c_str());
    OlmAccount alice, bob;
    CHECK(alice.Create() && bob.Create());
    CHECK(alice.Curve25519().size() == 43 && alice.Ed25519().size() == 43);
    CHECK(alice.Curve25519() != bob.Curve25519());

    // Signatures.
    const std::string signature = alice.Sign("a message");
    CHECK(OlmVerifyEd25519(alice.Ed25519(), "a message", signature));
    CHECK(!OlmVerifyEd25519(alice.Ed25519(), "a messagE", signature));
    CHECK(!OlmVerifyEd25519(bob.Ed25519(), "a message", signature));

    // One-time keys: generated unpublished, gone from the list once published.
    CHECK(bob.MaxOneTimeKeys() >= 50);
    bob.GenerateOneTimeKeys(3);
    const auto keys = bob.OneTimeKeys();
    CHECK(keys.size() == 3);
    bob.GenerateFallbackKey();
    CHECK(bob.FallbackKey().size() == 1);
    bob.MarkKeysAsPublished();
    CHECK(bob.OneTimeKeys().empty() && bob.FallbackKey().empty());

    // Alice opens a session with one of Bob's keys; Bob answers on it.
    OlmSession out = alice.OutboundSession(bob.Curve25519(), keys.begin()->second);
    CHECK(out.IsValid());
    std::string body;
    CHECK(out.Encrypt("{\"hello\":\"bob\"}", body) == 0);         // pre-key until Bob answers
    std::string plaintext;
    OlmSession in = bob.InboundSession(alice.Curve25519(), body, plaintext);
    CHECK(in.IsValid() && plaintext == "{\"hello\":\"bob\"}");
    CHECK(in.Matches(body) && in.Id() == out.Id());
    // The one-time key is spent: the same pre-key message opens no second session.
    std::string again;
    CHECK(!bob.InboundSession(alice.Curve25519(), body, again).IsValid());
    std::string reply;
    CHECK(in.Encrypt("hi alice", reply) == 1);
    CHECK(out.Decrypt(1, reply, plaintext) && plaintext == "hi alice");
    CHECK(out.HasReceivedMessage());
    CHECK(!out.Decrypt(1, reply, plaintext));                      // not twice

    // Pickles: back with the key, not with another.
    PickleKey key{}, other{};
    for (size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<uint8_t>(i);
        other[i] = static_cast<uint8_t>(255 - i);
    }
    OlmAccount restored;
    CHECK(restored.Unpickle(alice.Pickle(key), key) && restored.Curve25519() == alice.Curve25519());
    OlmAccount refused;
    CHECK(!refused.Unpickle(alice.Pickle(key), other) && !refused.IsValid());
    OlmSession reopened;
    CHECK(reopened.Unpickle(out.Pickle(key), key) && reopened.Id() == out.Id());
    CHECK(in.Encrypt("after the pickle", reply) == 1);
    CHECK(reopened.Decrypt(1, reply, plaintext) && plaintext == "after the pickle");

    // Megolm: the receiving side from the session key, and from exports.
    MegolmOutbound group;
    CHECK(group.Create() && group.MessageIndex() == 0);
    MegolmInbound member;
    CHECK(member.Create(group.SessionKey()) && member.Id() == group.Id());
    const std::string first = group.Encrypt("first");
    const std::string second = group.Encrypt("second");
    CHECK(group.MessageIndex() == 2);
    uint32_t index = 99;
    CHECK(member.Decrypt(second, plaintext, index) && plaintext == "second" && index == 1);
    CHECK(member.Decrypt(first, plaintext, index) && plaintext == "first" && index == 0);
    MegolmInbound fromStart, fromOne;
    CHECK(fromStart.Import(member.ExportAt(0)) && fromStart.FirstKnownIndex() == 0);
    CHECK(fromStart.Decrypt(first, plaintext, index) && plaintext == "first");
    CHECK(fromOne.Import(member.ExportAt(1)) && fromOne.FirstKnownIndex() == 1);
    CHECK(!fromOne.Decrypt(first, plaintext, index));             // from before the export
    CHECK(fromOne.Decrypt(second, plaintext, index) && plaintext == "second");
    MegolmInbound late;
    CHECK(late.Create(group.SessionKey()) && late.FirstKnownIndex() == 2);
    CHECK(!late.Decrypt(first, plaintext, index));
    MegolmInbound pickled;
    CHECK(pickled.Unpickle(member.Pickle(key), key) && pickled.Decrypt(second, plaintext, index));
    MegolmOutbound groupAgain;
    CHECK(groupAgain.Unpickle(group.Pickle(key), key) && groupAgain.MessageIndex() == 2);
    MegolmInbound garbage;
    CHECK(!garbage.Create("not a key") && !OlmLastError().empty());
}

std::vector<uint8_t> Key(uint8_t fill) {
    return std::vector<uint8_t>(32, fill);
}

// An m.room.encrypted event as a homeserver hands it on.
JSONValue RoomEvent(MegolmOutbound& session, const std::string& roomId, const std::string& eventId,
                    const std::string& sender, const std::string& text) {
    JSONValue clear = JSONValue::MakeObject();
    clear.Set("msgtype", JSONValue("m.text"));
    clear.Set("body", JSONValue(text));
    JSONValue payload = JSONValue::MakeObject();
    payload.Set("type", JSONValue("m.room.message"));
    payload.Set("content", clear);
    payload.Set("room_id", JSONValue(roomId));
    JSONValue content = JSONValue::MakeObject();
    content.Set("algorithm", JSONValue("m.megolm.v1.aes-sha2"));
    content.Set("ciphertext", JSONValue(session.Encrypt(UltraCanvas::JSON::Serialize(payload))));
    content.Set("session_id", JSONValue(session.Id()));
    JSONValue event = JSONValue::MakeObject();
    event.Set("type", JSONValue("m.room.encrypted"));
    event.Set("event_id", JSONValue(eventId));
    event.Set("sender", JSONValue(sender));
    event.Set("content", content);
    return event;
}

// A key export file put together from the format's description (as Element
// writes it), not with the code under test.
std::string HandMadeExport(const std::string& sessionsJson, const std::string& passphrase, uint32_t rounds) {
    std::vector<uint8_t> salt(16), iv(16);
    for (size_t i = 0; i < 16; ++i) {
        salt[i] = static_cast<uint8_t>(i * 7);
        iv[i] = static_cast<uint8_t>(0xa0 + i);
    }
    iv[8] &= 0x7f;
    const UltraCryptSecureBuffer password(passphrase.data(), passphrase.size());
    UltraCryptSecureBuffer derived;
    UltraCrypt_DeriveKeyPbkdf2(UltraCryptHashAlgorithm::SHA512, password, salt, rounds, 64, derived);
    const UltraCryptSecureBuffer aes(derived.Data(), 32), mac(derived.Data() + 32, 32);
    UltraCryptCtrParams ctr;
    ctr.iv = iv;
    std::vector<uint8_t> ciphertext;
    UltraCrypt_AesCtrEncrypt(aes, ctr, sessionsJson.data(), sessionsJson.size(), ciphertext);
    std::vector<uint8_t> data{1};
    data.insert(data.end(), salt.begin(), salt.end());
    data.insert(data.end(), iv.begin(), iv.end());
    for (int shift = 24; shift >= 0; shift -= 8) data.push_back(static_cast<uint8_t>(rounds >> shift));
    data.insert(data.end(), ciphertext.begin(), ciphertext.end());
    std::vector<uint8_t> tag;
    UltraCrypt_Hmac(UltraCryptHashAlgorithm::SHA256, mac, data.data(), data.size(), tag);
    data.insert(data.end(), tag.begin(), tag.end());
    return "-----BEGIN MEGOLM SESSION DATA-----\n" + UltraCanvas::Base64Encode(data, true) +
           "\n-----END MEGOLM SESSION DATA-----\n";
}

void TestStoreAndRooms() {
    std::printf("key store, room events, key export files\n");
    const fs::path dir = fs::temp_directory_path() / ("ultraim-matrix-crypto-" + std::to_string(::rand()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string path = UltraCanvas::PathToUtf8(dir / "a.store");
    std::string error;

    // A new store, then the same account from it; not with another key or
    // for another device.
    std::string curve;
    {
        MatrixCrypto crypto;
        CHECK(!MatrixCrypto::StoreExists(path));
        CHECK(crypto.Open(path, Key(1), "@a:hs", "DEVA", error));
        curve = crypto.GetCurve25519();
        CHECK(curve.size() == 43 && MatrixCrypto::StoreExists(path));
        crypto.Close();
    }
    {
        MatrixCrypto crypto;
        CHECK(crypto.Open(path, Key(1), "@a:hs", "DEVA", error) && crypto.GetCurve25519() == curve);
    }
    {
        MatrixCrypto crypto;
        CHECK(!crypto.Open(path, Key(2), "@a:hs", "DEVA", error) && !crypto.IsOpen());
        CHECK(!crypto.Open(path, Key(1), "@a:hs", "DEVB", error));
        CHECK(!crypto.Open(path, std::vector<uint8_t>(16, 1), "@a:hs", "DEVA", error));
    }

    // Someone's room session, imported from a hand-made export file.
    MegolmOutbound bobs;
    CHECK(bobs.Create());
    MegolmInbound copy;
    CHECK(copy.Create(bobs.SessionKey()));
    const std::string room = "!room:hs";
    JSONValue session = JSONValue::MakeObject();
    session.Set("algorithm", JSONValue("m.megolm.v1.aes-sha2"));
    session.Set("room_id", JSONValue(room));
    session.Set("sender_key", JSONValue("bobcurve"));
    session.Set("session_id", JSONValue(bobs.Id()));
    session.Set("session_key", JSONValue(copy.ExportAt(0)));
    JSONValue claimed = JSONValue::MakeObject();
    claimed.Set("ed25519", JSONValue("bobed"));
    session.Set("sender_claimed_keys", claimed);
    session.Set("forwarding_curve25519_key_chain", JSONValue::MakeArray());
    JSONValue list = JSONValue::MakeArray();
    list.Append(session);
    const std::string file = HandMadeExport(UltraCanvas::JSON::Serialize(list), "correct horse", 1000);

    MatrixCrypto alice;
    CHECK(alice.Open(path, Key(1), "@a:hs", "DEVA", error));
    std::vector<MatrixSessionRef> imported;
    CHECK(!alice.ImportRoomKeys(file, "wrong horse", imported, error) && imported.empty());
    std::string altered = file;
    altered[60] = altered[60] == 'A' ? 'B' : 'A';
    CHECK(!alice.ImportRoomKeys(altered, "correct horse", imported, error));
    CHECK(alice.ImportRoomKeys(file, "correct horse", imported, error));
    CHECK(imported.size() == 1 && imported[0] == MatrixSessionRef(room, bobs.Id()));
    CHECK(alice.HasSession(room, bobs.Id()));
    CHECK(alice.ImportRoomKeys(file, "correct horse", imported, error) && imported.empty());   // known already

    // Bob's messages in the room decrypt; not in another room, not replayed
    // under another id, and not as someone else's.
    const JSONValue first = RoomEvent(bobs, room, "$1", "@bob:hs", "hello alice");
    MatrixDecrypted d = alice.DecryptRoomEvent(room, first);
    CHECK(d.state == MatrixDecryptState::Ok);
    CHECK(d.event["type"].GetString() == "m.room.message" && d.event["content"]["body"].GetString() == "hello alice");
    CHECK(d.event["event_id"].GetString() == "$1" && d.event["sender"].GetString() == "@bob:hs");
    CHECK(alice.DecryptRoomEvent(room, first).state == MatrixDecryptState::Ok);       // the same event again
    JSONValue replay = first;
    replay.Set("event_id", JSONValue("$2"));
    d = alice.DecryptRoomEvent(room, replay);
    CHECK(d.state == MatrixDecryptState::Failed && d.error.find("replay") != std::string::npos);
    CHECK(alice.DecryptRoomEvent("!other:hs", first).state == MatrixDecryptState::MissingKey);
    JSONValue elsewhere = RoomEvent(bobs, "!other:hs", "$3", "@bob:hs", "wrong room");
    d = alice.DecryptRoomEvent(room, elsewhere);
    CHECK(d.state == MatrixDecryptState::Failed);
    MegolmOutbound unknown;
    unknown.Create();
    CHECK(alice.DecryptRoomEvent(room, RoomEvent(unknown, room, "$4", "@bob:hs", "?")).state ==
          MatrixDecryptState::MissingKey);
    // From before the session's first known index: its key may still come.
    {
        MegolmOutbound carols;
        carols.Create();
        const JSONValue early = RoomEvent(carols, room, "$6", "@carol:hs", "before the key");
        MegolmInbound later;
        later.Create(carols.SessionKey());       // from index 1 on
        JSONValue s2 = JSONValue::MakeObject();
        s2.Set("algorithm", JSONValue("m.megolm.v1.aes-sha2"));
        s2.Set("room_id", JSONValue(room));
        s2.Set("sender_key", JSONValue("carolcurve"));
        s2.Set("session_id", JSONValue(carols.Id()));
        s2.Set("session_key", JSONValue(later.ExportAt(1)));
        JSONValue l2 = JSONValue::MakeArray();
        l2.Append(s2);
        CHECK(alice.ImportRoomKeys(HandMadeExport(UltraCanvas::JSON::Serialize(l2), "p", 10), "p", imported, error));
        CHECK(alice.DecryptRoomEvent(room, early).state == MatrixDecryptState::MissingKey);
    }
    alice.Flush();

    // An export of ours goes into another store, and its messages open there.
    std::string exported;
    CHECK(!alice.ExportRoomKeys("", 1000, exported, error));
    CHECK(alice.ExportRoomKeys("pass phrase", 1000, exported, error));
    CHECK(exported.rfind("-----BEGIN MEGOLM SESSION DATA-----\n", 0) == 0);
    CHECK(exported.find("-----END MEGOLM SESSION DATA-----") != std::string::npos);
    bool lines = true;
    size_t at = exported.find('\n') + 1;
    while (at < exported.size()) {
        const size_t nl = exported.find('\n', at);
        if (exported.compare(at, 5, "-----") != 0) lines = lines && nl - at <= 96;
        at = nl + 1;
    }
    CHECK(lines);       // 96 characters a line, as Element writes it
    MatrixCrypto other;
    CHECK(other.Open(UltraCanvas::PathToUtf8(dir / "b.store"), Key(3), "@a:hs", "DEVB", error));
    CHECK(other.ImportRoomKeys(exported, "pass phrase", imported, error) && imported.size() == 2);
    const JSONValue later = RoomEvent(bobs, room, "$5", "@bob:hs", "after the export");
    CHECK(other.DecryptRoomEvent(room, later).event["content"]["body"].GetString() == "after the export");
    CHECK(other.DecryptRoomEvent(room, first).state == MatrixDecryptState::Ok);    // from the first index on

    // The replay guard and the sessions are in the store after a restart.
    alice.Close();
    MatrixCrypto reopened;
    CHECK(reopened.Open(path, Key(1), "@a:hs", "DEVA", error));
    CHECK(reopened.HasSession(room, bobs.Id()));
    CHECK(reopened.DecryptRoomEvent(room, replay).state == MatrixDecryptState::Failed);
    reopened.Close();
    other.Close();
    fs::remove_all(dir, ec);
}

} // namespace

int main() {
    if (!OlmAvailable()) {
        std::printf("MatrixCryptoTests: SKIP (built without vodozemac)\n");
        return 0;
    }
    TestCanonicalJson();
    TestOlm();
    TestStoreAndRooms();
    std::printf("MatrixCryptoTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
