// SmartHome/tests/MiioCodecTest.cpp
// Xiaomi miIO: packet framing, checksum and encryption (MiioPacket) and the
// JSON-RPC inside (MiioRpc). No device, no socket.
//
// The reference packet below was built outside this code base, with coreutils
// md5sum and the openssl command line, from the description in MiioPacket.h:
//
//   token    00112233445566778899aabbccddeeff
//   key      md5(token)                         6e8311168ee16d6aa1aa48c64145003c
//   iv       md5(key | token)                   6f434fa9acd75da73e5fb999f641cda2
//   body     openssl enc -aes-128-cbc -K key -iv iv   of
//            {"id":1,"method":"miIO.info","params":[]}
//   header   2131 0050 00000000 01234567 00000100
//   checksum md5(header[0..15] | token | body)  ac004b3c85d5befb3d4a09ee22b73df8
//
// so a mistake here cannot pass by agreeing with itself.
//
// Author: UltraCanvas Framework
#include "MiioPacket.h"
#include "MiioRpc.h"

#include <cstdio>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::SmartHome::Miio;

static int failures = 0;

static void Check(bool condition, const char* what) {
    if (condition) { std::printf("ok  %s\n", what); }
    else           { std::printf("FAIL %s\n", what); ++failures; }
}

static std::string Hex(const uint8_t* data, size_t size) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < size; ++i) {
        out.push_back(kDigits[data[i] >> 4]);
        out.push_back(kDigits[data[i] & 0x0F]);
    }
    return out;
}

static Bytes FromHex(const std::string& hex) {
    Bytes out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}

static const char* kReferencePacket =
    "21310050000000000123456700000100ac004b3c85d5befb3d4a09ee22b73df8"
    "a5516ec6151955dc2bb2d43e7c84c18352a39a7f22fd2903d282a5783c6d56ac"
    "8c9c7629130a25fac17d38175c1a7799";

int main() {
    // ---- the token ----
    Token token;
    Check(Token::FromHex("00112233445566778899AABBCCDDEEFF", token), "a 32-digit token reads, either case");
    Check(token.ToHex() == "00112233445566778899aabbccddeeff", "and writes back in lower case");
    Token untouched = token;
    Check(!Token::FromHex("0011223344556677", untouched) && !Token::FromHex(std::string(32, 'g'), untouched) &&
              untouched.ToHex() == token.ToHex(),
          "a short or non-hex token is refused and changes nothing");

    // ---- keys ----
    Block16 key, iv;
    std::string error;
    Check(DeriveKeys(token, key, iv, error), "keys derive");
    Check(Hex(key.data(), 16) == "6e8311168ee16d6aa1aa48c64145003c", "key = md5(token)");
    Check(Hex(iv.data(), 16) == "6f434fa9acd75da73e5fb999f641cda2", "iv = md5(key | token)");

    // ---- the reference packet, both ways ----
    const std::string request = BuildRequest(1, "miIO.info", JSONValue::MakeArray());
    Check(request == "{\"id\":1,\"method\":\"miIO.info\",\"params\":[]}", "the request JSON");
    Bytes packet;
    Check(Encode(token, 0x01234567, 0x100, request, packet, error), "encodes");
    Check(Hex(packet.data(), packet.size()) == kReferencePacket,
          "byte for byte the packet openssl and md5sum built");

    const Bytes reference = FromHex(kReferencePacket);
    Header header;
    std::string payload;
    Check(Decode(token, reference.data(), reference.size(), header, payload, error), "the reference packet decodes");
    Check(payload == request, "to the request");
    Check(header.length == 80 && header.deviceId == 0x01234567 && header.stamp == 0x100 && header.unknown == 0,
          "header fields: length 80, device 0x01234567, stamp 0x100");

    // ---- what must be refused ----
    {
        Bytes damaged = reference;
        damaged[40] ^= 0x01;
        Check(!Decode(token, damaged.data(), damaged.size(), header, payload, error) &&
                  error.find("checksum") != std::string::npos,
              "a changed body byte fails the checksum, before any decryption");
        Token wrong;
        Token::FromHex("ffeeddccbbaa99887766554433221100", wrong);
        Check(!Decode(wrong, reference.data(), reference.size(), header, payload, error),
              "the wrong token fails");
        Bytes badMagic = reference;
        badMagic[0] = 0x21;
        badMagic[1] = 0x32;
        Check(!ParseHeader(badMagic.data(), badMagic.size(), header, error), "bad magic");
        Check(!ParseHeader(reference.data(), reference.size() - 16, header, error),
              "a length field that does not match the datagram");
        Check(!ParseHeader(reference.data(), 20, header, error), "shorter than a header");
        Bytes ragged = reference;
        ragged.push_back(0);
        ragged[3] = static_cast<uint8_t>(ragged.size());
        Check(!Decode(token, ragged.data(), ragged.size(), header, payload, error),
              "a body that is not whole AES blocks");
    }

    // ---- hello ----
    const Bytes hello = HelloRequest();
    Check(Hex(hello.data(), hello.size()) == "21310020" + std::string(56, 'f'), "hello: magic, length 32, then 0xFF");
    {
        // A device's answer: a bare header with its id and clock.
        const Bytes reply = FromHex("2131002000000000047a1b2c5f5e100000112233445566778899aabbccddeeff");
        Check(Decode(token, reply.data(), reply.size(), header, payload, error) && payload.empty(),
              "a bare header decodes with no payload");
        Check(header.deviceId == 0x047a1b2c && header.stamp == 0x5f5e1000, "the hello reply's device id and clock");
    }

    // ---- trailing NULs some firmware appends ----
    {
        Bytes padded;
        Encode(token, 1, 2, std::string("{\"id\":7,\"result\":[\"ok\"]}") + std::string(3, '\0'), padded, error);
        Check(Decode(token, padded.data(), padded.size(), header, payload, error) &&
                  payload == "{\"id\":7,\"result\":[\"ok\"]}",
              "trailing NUL bytes are dropped");
    }

    // ---- replies ----
    Reply reply;
    Check(ParseReply("{\"result\":[\"ok\"],\"id\":7}", reply, error) && reply.ok && reply.id == 7 &&
              reply.result[0].GetString() == "ok",
          "a result reply");
    Check(ParseReply("{\"id\":8,\"error\":{\"code\":-5001,\"message\":\"invalid arg\"}}", reply, error) &&
              !reply.ok && reply.id == 8 && reply.errorCode == -5001 && reply.errorMessage == "invalid arg",
          "an error reply");
    Check(ParseReply("{\"id\":9,\"error\":\"unknown_method\"}", reply, error) && !reply.ok &&
              reply.errorMessage == "unknown_method",
          "an error given as a bare string");
    Check(!ParseReply("{\"result\":[\"ok\"]}", reply, error), "no id: refused");
    Check(!ParseReply("{\"id\":3}", reply, error), "neither result nor error: refused");
    Check(!ParseReply("not json", reply, error), "not JSON: refused");

    // ---- MIoT spec ----
    const JSONValue get = GetPropertiesParams("vacuum", {{2, 1}, {3, 1}});
    Check(JSON::Serialize(get) ==
              "[{\"did\":\"vacuum\",\"siid\":2,\"piid\":1},{\"did\":\"vacuum\",\"siid\":3,\"piid\":1}]",
          "get_properties params");
    const JSONValue set = SetPropertiesParams("vacuum", {{{2, 6}, JSONValue(2)}});
    Check(JSON::Serialize(set) == "[{\"did\":\"vacuum\",\"siid\":2,\"piid\":6,\"value\":2}]", "set_properties params");
    Check(JSON::Serialize(ActionParams("vacuum", 2, 1)) == "{\"did\":\"vacuum\",\"siid\":2,\"aiid\":1,\"in\":[]}",
          "action params");

    std::vector<PropertyResult> results;
    Check(ParseReply("{\"id\":4,\"result\":[{\"did\":\"vacuum\",\"siid\":2,\"piid\":1,\"code\":0,\"value\":5},"
                     "{\"did\":\"vacuum\",\"siid\":3,\"piid\":1,\"code\":-4001}]}",
                     reply, error) &&
              ParsePropertyResults(reply.result, results, error),
          "get_properties results parse");
    Check(results.size() == 2 && results[0].siid == 2 && results[0].value.GetInteger() == 5 && results[0].code == 0,
          "a value read");
    Check(results[1].code == -4001 && results[1].value.IsNull(), "a property the device could not read keeps its code");
    Check(!ParsePropertyResults(JSON::Parse("[{\"value\":1}]"), results, error), "a result without ids is refused");

    int64_t code = -1;
    JSONValue out;
    Check(ParseActionResult(JSON::Parse("{\"code\":0,\"out\":[]}"), code, out, error) && code == 0 && out.IsArray(),
          "an action result");

    if (failures) { std::printf("%d check(s) FAILED\n", failures); return 1; }
    std::printf("PASS - miIO packets and RPC\n");
    return 0;
}
