// Apps/AnchorPoint/tests/ProtocolTests.cpp
// AnchorPoint - receiver-side protocol tests: the offered file name is the
// peer's, so it must never place a file outside the save folder.
// Version: 0.1.0
// Author: AnchorPoint
#include "Protocol.h"
// Relative, so every target that compiles this file finds it (the tests
// build VirtualFS sources into their own executables).
#include "../../../UltraCanvas/include/UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include "UltraCrypt/UltraCryptCore.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace AnchorPoint;
namespace fs = std::filesystem;
using UltraCanvas::PathToUtf8;

namespace {

int failures = 0;

void Check(bool condition, const std::string& what) {
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    }
}

// Plays back bytes a peer "sent" and records what the receiver answers.
class ScriptedConnection : public IConnection {
public:
    explicit ScriptedConnection(std::vector<uint8_t> incoming) : in_(std::move(incoming)) {}

    bool SendAll(const void* data, size_t len) override {
        const auto* b = static_cast<const uint8_t*>(data);
        out.insert(out.end(), b, b + len);
        return true;
    }
    bool RecvAll(void* data, size_t len) override {
        if (in_.size() - pos_ < len) return false;
        std::memcpy(data, in_.data() + pos_, len);
        pos_ += len;
        return true;
    }
    void Close() override {}
    std::string PeerAddress() const override { return "test"; }

    std::vector<uint8_t> out;

private:
    std::vector<uint8_t> in_;
    size_t pos_ = 0;
};

void PutU16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v)); }
void PutU32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 3; i >= 0; --i) b.push_back(uint8_t(v >> (i * 8)));
}
void PutU64(std::vector<uint8_t>& b, uint64_t v) {
    for (int i = 7; i >= 0; --i) b.push_back(uint8_t(v >> (i * 8)));
}

void Frame(std::vector<uint8_t>& stream, MsgType type, const std::vector<uint8_t>& payload) {
    stream.push_back(static_cast<uint8_t>(type));
    PutU32(stream, static_cast<uint32_t>(payload.size()));
    stream.insert(stream.end(), payload.begin(), payload.end());
}

// Hello + Offer + one Chunk + Done for `content` under the wire name `name`.
std::vector<uint8_t> PeerSends(const std::string& name, const std::string& content) {
    std::vector<uint8_t> digest;
    UltraCrypt_Hash(UltraCryptHashAlgorithm::SHA256, content.data(), content.size(), digest);

    std::vector<uint8_t> stream, p;
    PutU16(p, kProtocolVersion);
    PutU16(p, 4);
    p.insert(p.end(), {'p', 'e', 'e', 'r'});
    Frame(stream, MsgType::Hello, p);

    p.clear();
    PutU64(p, content.size());
    PutU32(p, kDefaultChunkSize);
    p.insert(p.end(), digest.begin(), digest.end());
    PutU16(p, static_cast<uint16_t>(name.size()));
    p.insert(p.end(), name.begin(), name.end());
    Frame(stream, MsgType::Offer, p);

    p.clear();
    PutU64(p, 0);
    p.insert(p.end(), content.begin(), content.end());
    Frame(stream, MsgType::Chunk, p);

    Frame(stream, MsgType::Done, digest);
    return stream;
}

std::string ReadAll(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

void TestSafeFileName() {
    struct Case { std::string wire; bool ok; std::string name; };
    const std::vector<Case> cases = {
        {"photo.jpg",                true,  "photo.jpg"},
        {".bashrc",                  true,  ".bashrc"},
        {"...",                      true,  "..."},
        {"../../.bashrc",            true,  ".bashrc"},
        {"/etc/passwd",              true,  "passwd"},
        {"..\\..\\evil.dll",         true,  "evil.dll"},
        {"a/b\\c/d.txt",             true,  "d.txt"},
        {"C:\\Windows\\win.ini",     true,  "win.ini"},
        {"",                         false, ""},
        {".",                        false, ""},
        {"..",                       false, ""},
        {"../..",                    false, ""},
        {"dir/",                     false, ""},
        {"dir\\",                    false, ""},
        {"/",                        false, ""},
        {"C:evil.txt",               false, ""},
        {"notes.txt:hidden",         false, ""},
        {std::string("a\0b.txt", 7), false, ""},
    };
    for (const Case& c : cases) {
        std::string out = "unchanged";
        const bool ok = SafeFileName(c.wire, out);
        Check(ok == c.ok, "SafeFileName verdict for \"" + c.wire + "\"");
        Check(out == c.name, "SafeFileName result for \"" + c.wire + "\" was \"" + out + "\"");
    }
}

// A traversal name arrives as its last component, inside the save folder.
void TestTraversalLandsInsideSaveFolder(const fs::path& root) {
    const fs::path saveDir = root / "save";
    fs::create_directories(saveDir);
    const fs::path victim = root / "victim.txt";
    { std::ofstream(victim) << "original"; }

    ScriptedConnection conn(PeerSends("../victim.txt", "payload"));
    std::string offered;
    auto res = ReceiveFile(conn, [&](const OfferInfo& offer, const std::string&) {
        offered = offer.fileName;
        return PathToUtf8(saveDir / offer.fileName);
    });

    Check(res.ok, "traversal name still transfers, into the save folder: " + res.error);
    Check(offered == "victim.txt", "accept callback saw \"" + offered + "\"");
    Check(ReadAll(victim) == "original", "file outside the save folder untouched");
    Check(ReadAll(saveDir / "victim.txt") == "payload", "file written inside the save folder");
}

// A name that cannot be reduced is refused before the caller is asked.
void TestUnsafeNameRefused(const fs::path& root, const std::string& wire) {
    ScriptedConnection conn(PeerSends(wire, "payload"));
    bool asked = false;
    auto res = ReceiveFile(conn, [&](const OfferInfo&, const std::string&) {
        asked = true;
        return PathToUtf8(root / "never");
    });

    Check(!res.ok, "unsafe name \"" + wire + "\" refused");
    Check(!asked, "accept callback not called for \"" + wire + "\"");
    Check(!conn.out.empty() && conn.out[0] == static_cast<uint8_t>(MsgType::Reject),
          "peer told Reject for \"" + wire + "\"");
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() /
        ("anchorpoint-tests-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);

    TestSafeFileName();
    TestTraversalLandsInsideSaveFolder(root);
    TestUnsafeNameRefused(root, "..");
    TestUnsafeNameRefused(root, "C:evil.txt");
    TestUnsafeNameRefused(root, "dir/");

    std::error_code ec;
    fs::remove_all(root, ec);

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("AnchorPoint protocol tests passed\n");
    return 0;
}
