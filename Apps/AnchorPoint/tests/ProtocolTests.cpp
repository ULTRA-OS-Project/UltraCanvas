// Apps/AnchorPoint/tests/ProtocolTests.cpp
// AnchorPoint - protocol tests: the offered file name is the peer's, so it
// must never place a file outside the save folder; and a transfer stops when
// asked, telling the peer, whether it is between chunks or blocked on the
// network.
// Version: 0.1.0
// Author: AnchorPoint
#include "Protocol.h"
// Relative, so every target that compiles this file finds it (the tests
// build VirtualFS sources into their own executables).
#include "../../../UltraCanvas/include/UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include "UltraCrypt/UltraCryptCore.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
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
    void Interrupt() override {}
    std::string PeerAddress() const override { return "test"; }

    std::vector<uint8_t> out;

protected:
    std::vector<uint8_t> in_;
    size_t pos_ = 0;
};

// Runs out of script the way an interrupted socket does: Stop is pressed while
// the transfer is blocked reading, so the read fails with the flag already set.
class StoppedWhileReading : public ScriptedConnection {
public:
    StoppedWhileReading(std::vector<uint8_t> incoming, std::atomic<bool>& cancel)
        : ScriptedConnection(std::move(incoming)), cancel_(cancel) {}
    bool RecvAll(void* data, size_t len) override {
        if (ScriptedConnection::RecvAll(data, len)) return true;
        cancel_ = true;
        return false;
    }
private:
    std::atomic<bool>& cancel_;
};

// The frame types in a recorded stream, in order.
std::vector<MsgType> FrameTypes(const std::vector<uint8_t>& stream) {
    std::vector<MsgType> types;
    size_t pos = 0;
    while (pos + 5 <= stream.size()) {
        const uint32_t len = (uint32_t(stream[pos + 1]) << 24) | (uint32_t(stream[pos + 2]) << 16) |
                             (uint32_t(stream[pos + 3]) << 8) | stream[pos + 4];
        types.push_back(static_cast<MsgType>(stream[pos]));
        pos += 5 + len;
    }
    return types;
}

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
        return PathToUtf8(saveDir / UltraCanvas::PathFromUtf8(offer.fileName));
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

bool Contains(const std::vector<MsgType>& types, MsgType t) {
    for (MsgType x : types) if (x == t) return true;
    return false;
}

std::vector<uint8_t> AcceptFrom(uint64_t offset) {
    std::vector<uint8_t> stream, p;
    PutU64(p, offset);
    Frame(stream, MsgType::Accept, p);
    return stream;
}

// Stop pressed on the receiver once the file is accepted: no chunk is written,
// the sender is told, and nothing is reported as verified.
void TestReceiverStops(const fs::path& root) {
    std::atomic<bool> cancel{false};
    ScriptedConnection conn(PeerSends("stopped.txt", "payload"));
    auto res = ReceiveFile(conn, [&](const OfferInfo& offer, const std::string&) {
        cancel = true;
        return PathToUtf8(root / UltraCanvas::PathFromUtf8(offer.fileName));
    }, nullptr, &cancel);

    const auto sent = FrameTypes(conn.out);
    Check(!res.ok && res.cancelled, "receiver reports the stop: " + res.error);
    Check(!sent.empty() && sent.back() == MsgType::Error, "receiver tells the sender it stopped");
    Check(!Contains(sent, MsgType::Verified), "receiver does not claim a verified file");
    Check(ReadAll(root / "stopped.txt").empty(), "no chunk written after the stop");
}

// Stop pressed on the sender before the first chunk: no chunk goes out and the
// receiver is told.
void TestSenderStops(const fs::path& root) {
    const fs::path file = root / "outgoing.bin";
    { std::ofstream(file, std::ios::binary) << std::string(3 * kDefaultChunkSize, 'x'); }

    std::atomic<bool> cancel{true};
    ScriptedConnection conn(AcceptFrom(0));
    auto res = SendFile(conn, PathToUtf8(file), "test", nullptr, &cancel);

    const auto sent = FrameTypes(conn.out);
    Check(!res.ok && res.cancelled, "sender reports the stop: " + res.error);
    Check(!Contains(sent, MsgType::Chunk), "sender sends no chunk after the stop");
    Check(!sent.empty() && sent.back() == MsgType::Error, "sender tells the receiver it stopped");
}

// Stop pressed while the sender waits for the receiver's verdict: the read the
// stop broke is reported as the stop, not as a lost connection.
void TestStopWhileBlockedIsReportedAsStop(const fs::path& root) {
    const fs::path file = root / "small.bin";
    { std::ofstream(file, std::ios::binary) << "small"; }

    std::atomic<bool> cancel{false};
    StoppedWhileReading conn(AcceptFrom(0), cancel);
    auto res = SendFile(conn, PathToUtf8(file), "test", nullptr, &cancel);
    Check(!res.ok && res.cancelled && res.error == "cancelled",
          "a read broken by the stop reads as the stop: " + res.error);

    // Without a stop the same failure stays what it is.
    ScriptedConnection plain(AcceptFrom(0));
    auto lost = SendFile(plain, PathToUtf8(file), "test");
    Check(!lost.ok && !lost.cancelled, "a real lost connection is not called a stop");
}

// Interrupt() wakes a socket read blocked on another thread, and a send to a
// peer that has gone fails instead of killing the program with SIGPIPE.
void TestSocketInterruptAndDeadPeer() {
    std::string err;
    auto listener = Listen(0, &err);
    Check(listener != nullptr, "listen on a free loopback port: " + err);
    if (!listener) return;

    std::unique_ptr<IConnection> server;
    std::thread acceptor([&] { server = listener->Accept(); });
    auto client = Connect("127.0.0.1", listener->Port(), &err);
    acceptor.join();
    Check(client && server, "loopback connection: " + err);
    if (!client || !server) return;

    std::atomic<bool> returned{false};
    bool readOk = true;
    std::thread reader([&] {
        uint8_t b;
        readOk = server->RecvAll(&b, 1);   // nothing is ever sent: blocks
        returned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Check(!returned, "the read is blocked before the interrupt");
    server->Interrupt();
    reader.join();
    Check(!readOk, "an interrupted read fails");

    server->Close();
    std::vector<uint8_t> block(64 * 1024, 0);
    bool anyFailed = false;
    for (int i = 0; i < 64 && !anyFailed; ++i) anyFailed = !client->SendAll(block.data(), block.size());
    Check(anyFailed, "sending to a closed peer fails (and the test is still running)");
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
    TestReceiverStops(root);
    TestSenderStops(root);
    TestStopWhileBlockedIsReportedAsStop(root);
    TestSocketInterruptAndDeadPeer();

    std::error_code ec;
    fs::remove_all(root, ec);

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("AnchorPoint protocol tests passed\n");
    return 0;
}
