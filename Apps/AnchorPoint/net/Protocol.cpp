// Apps/AnchorPoint/net/Protocol.cpp
// AnchorPoint - file-transfer wire protocol implementation.
// Version: 0.1.0
// Author: AnchorPoint
#include "Protocol.h"
#include "Sha256.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace AnchorPoint {

namespace {

// ---- big-endian encode/decode --------------------------------------------
void PutU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v));
}
void PutU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v >> 24)); b.push_back(uint8_t(v >> 16));
    b.push_back(uint8_t(v >> 8));  b.push_back(uint8_t(v));
}
void PutU64(std::vector<uint8_t>& b, uint64_t v) {
    for (int i = 7; i >= 0; --i) b.push_back(uint8_t(v >> (i * 8)));
}
uint16_t GetU16(const uint8_t* p) { return (uint16_t(p[0]) << 8) | p[1]; }
uint32_t GetU32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | p[3];
}
uint64_t GetU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

// ---- framing --------------------------------------------------------------
bool WriteFrame(IConnection& c, MsgType type, const std::vector<uint8_t>& payload) {
    uint8_t header[5];
    header[0] = static_cast<uint8_t>(type);
    uint32_t n = static_cast<uint32_t>(payload.size());
    header[1] = uint8_t(n >> 24); header[2] = uint8_t(n >> 16);
    header[3] = uint8_t(n >> 8);  header[4] = uint8_t(n);
    if (!c.SendAll(header, sizeof(header))) return false;
    if (!payload.empty() && !c.SendAll(payload.data(), payload.size())) return false;
    return true;
}

bool ReadFrame(IConnection& c, MsgType& type, std::vector<uint8_t>& payload) {
    uint8_t header[5];
    if (!c.RecvAll(header, sizeof(header))) return false;
    type = static_cast<MsgType>(header[0]);
    uint32_t n = GetU32(header + 1);
    if (n > kMaxFrameSize) return false;
    payload.resize(n);
    if (n > 0 && !c.RecvAll(payload.data(), n)) return false;
    return true;
}

bool SendError(IConnection& c, const std::string& msg) {
    std::vector<uint8_t> p(msg.begin(), msg.end());
    return WriteFrame(c, MsgType::Error, p);
}

std::string BaseName(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

} // namespace

// ===========================================================================
// Sender
// ===========================================================================
TransferResult SendFile(IConnection& conn, const std::string& filePath,
                        const std::string& displayName, ProgressFn onProgress) {
    TransferResult r;

    std::ifstream in(filePath, std::ios::binary | std::ios::ate);
    if (!in) { r.error = "cannot open " + filePath; return r; }
    uint64_t fileSize = static_cast<uint64_t>(in.tellg());
    in.seekg(0);

    // Pre-hash the whole file so we can assert integrity at the end.
    OfferInfo offer;
    offer.fileName = BaseName(filePath);
    offer.fileSize = fileSize;
    offer.chunkSize = kDefaultChunkSize;
    {
        Sha256 h;
        std::vector<char> buf(64 * 1024);
        std::ifstream hf(filePath, std::ios::binary);
        while (hf) {
            hf.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            std::streamsize got = hf.gcount();
            if (got > 0) h.Update(buf.data(), static_cast<size_t>(got));
        }
        offer.sha256 = h.Final();
    }

    // Hello
    {
        std::vector<uint8_t> p;
        PutU16(p, kProtocolVersion);
        PutU16(p, static_cast<uint16_t>(displayName.size()));
        p.insert(p.end(), displayName.begin(), displayName.end());
        if (!WriteFrame(conn, MsgType::Hello, p)) { r.error = "send hello failed"; return r; }
    }

    // Offer
    {
        std::vector<uint8_t> p;
        PutU64(p, offer.fileSize);
        PutU32(p, offer.chunkSize);
        p.insert(p.end(), offer.sha256.begin(), offer.sha256.end());
        PutU16(p, static_cast<uint16_t>(offer.fileName.size()));
        p.insert(p.end(), offer.fileName.begin(), offer.fileName.end());
        if (!WriteFrame(conn, MsgType::Offer, p)) { r.error = "send offer failed"; return r; }
    }

    // Await Accept / Reject
    MsgType type;
    std::vector<uint8_t> payload;
    if (!ReadFrame(conn, type, payload)) { r.error = "no response to offer"; return r; }
    if (type == MsgType::Reject) {
        r.error = "peer rejected: " + std::string(payload.begin(), payload.end());
        return r;
    }
    if (type != MsgType::Accept || payload.size() < 8) {
        r.error = "unexpected reply to offer";
        return r;
    }
    uint64_t offset = GetU64(payload.data());
    if (offset > fileSize) offset = 0; // bogus resume point; start over

    // Stream chunks from `offset`.
    in.seekg(static_cast<std::streamoff>(offset));
    std::vector<uint8_t> frame;
    std::vector<char> data(offer.chunkSize);
    uint64_t sent = offset;
    if (onProgress) onProgress(sent, fileSize);

    while (sent < fileSize) {
        uint64_t remaining = fileSize - sent;
        size_t want = static_cast<size_t>(remaining < offer.chunkSize ? remaining : offer.chunkSize);
        in.read(data.data(), static_cast<std::streamsize>(want));
        if (static_cast<size_t>(in.gcount()) != want) { r.error = "read error"; return r; }

        frame.clear();
        PutU64(frame, sent);
        frame.insert(frame.end(), data.begin(), data.begin() + want);
        if (!WriteFrame(conn, MsgType::Chunk, frame)) { r.error = "send chunk failed"; return r; }

        sent += want;
        if (onProgress) onProgress(sent, fileSize);
    }

    // Done + whole-file digest
    {
        std::vector<uint8_t> p(offer.sha256.begin(), offer.sha256.end());
        if (!WriteFrame(conn, MsgType::Done, p)) { r.error = "send done failed"; return r; }
    }

    if (!ReadFrame(conn, type, payload)) { r.error = "no verification reply"; return r; }
    if (type == MsgType::Verified && payload.size() == 1 && payload[0] == 1) {
        r.ok = true;
        r.bytes = fileSize;
    } else if (type == MsgType::Error) {
        r.error = "receiver: " + std::string(payload.begin(), payload.end());
    } else {
        r.error = "integrity check failed at receiver";
    }
    return r;
}

// ===========================================================================
// Receiver
// ===========================================================================
TransferResult ReceiveFile(IConnection& conn, const AcceptFn& accept,
                           ProgressFn onProgress) {
    TransferResult r;
    MsgType type;
    std::vector<uint8_t> payload;

    // Hello
    if (!ReadFrame(conn, type, payload) || type != MsgType::Hello || payload.size() < 4) {
        r.error = "bad handshake"; return r;
    }
    uint16_t ver = GetU16(payload.data());
    if (ver != kProtocolVersion) { SendError(conn, "version mismatch"); r.error = "version mismatch"; return r; }
    uint16_t nameLen = GetU16(payload.data() + 2);
    std::string peerName;
    if (payload.size() >= size_t(4) + nameLen)
        peerName.assign(payload.begin() + 4, payload.begin() + 4 + nameLen);

    // Offer
    if (!ReadFrame(conn, type, payload) || type != MsgType::Offer || payload.size() < 8 + 4 + 32 + 2) {
        r.error = "bad offer"; return r;
    }
    OfferInfo offer;
    const uint8_t* p = payload.data();
    offer.fileSize = GetU64(p); p += 8;
    offer.chunkSize = GetU32(p); p += 4;
    std::memcpy(offer.sha256.data(), p, 32); p += 32;
    uint16_t fnLen = GetU16(p); p += 2;
    if (payload.data() + payload.size() < p + fnLen) { r.error = "bad offer name"; return r; }
    offer.fileName.assign(reinterpret_cast<const char*>(p), fnLen);

    // Ask the caller where to put it (or to reject).
    std::string outPath = accept ? accept(offer, peerName) : std::string();
    if (outPath.empty()) {
        SendError(conn, "rejected");
        std::vector<uint8_t> reason{'r','e','j','e','c','t','e','d'};
        WriteFrame(conn, MsgType::Reject, reason);
        r.error = "rejected locally";
        return r;
    }

    // Resume: if a partial file already exists, continue from its length.
    uint64_t haveBytes = 0;
    {
        std::ifstream probe(outPath, std::ios::binary | std::ios::ate);
        if (probe) {
            uint64_t existing = static_cast<uint64_t>(probe.tellg());
            if (existing <= offer.fileSize) haveBytes = existing;
        }
    }

    std::ios::openmode mode = std::ios::binary | std::ios::in | std::ios::out;
    std::fstream out(outPath, mode);
    if (!out) {
        // Create fresh.
        std::ofstream create(outPath, std::ios::binary);
        if (!create) { SendError(conn, "cannot create file"); r.error = "cannot create " + outPath; return r; }
        create.close();
        haveBytes = 0;
        out.open(outPath, mode);
        if (!out) { r.error = "cannot open " + outPath; return r; }
    }

    // Accept with our resume offset.
    {
        std::vector<uint8_t> ap;
        PutU64(ap, haveBytes);
        if (!WriteFrame(conn, MsgType::Accept, ap)) { r.error = "send accept failed"; return r; }
    }

    out.seekp(static_cast<std::streamoff>(haveBytes));
    uint64_t received = haveBytes;
    if (onProgress) onProgress(received, offer.fileSize);

    // Receive chunks until Done.
    while (true) {
        if (!ReadFrame(conn, type, payload)) { r.error = "connection lost"; return r; }
        if (type == MsgType::Chunk) {
            if (payload.size() < 8) { r.error = "short chunk"; return r; }
            uint64_t off = GetU64(payload.data());
            size_t dataLen = payload.size() - 8;
            if (off != received) {
                // Out-of-order writes are supported via seek, but our sender is
                // strictly sequential; tolerate by seeking.
                out.seekp(static_cast<std::streamoff>(off));
            }
            out.write(reinterpret_cast<const char*>(payload.data() + 8),
                      static_cast<std::streamsize>(dataLen));
            if (!out) { r.error = "disk write failed"; return r; }
            received = off + dataLen;
            if (onProgress) onProgress(received, offer.fileSize);
        } else if (type == MsgType::Done) {
            break;
        } else if (type == MsgType::Error) {
            r.error = "sender: " + std::string(payload.begin(), payload.end());
            return r;
        } else {
            r.error = "unexpected frame during transfer";
            return r;
        }
    }
    out.flush();
    out.close();

    // Verify whole-file integrity.
    Sha256 h;
    {
        std::ifstream vf(outPath, std::ios::binary);
        std::vector<char> buf(64 * 1024);
        while (vf) {
            vf.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            std::streamsize got = vf.gcount();
            if (got > 0) h.Update(buf.data(), static_cast<size_t>(got));
        }
    }
    auto digest = h.Final();
    bool ok = (received == offer.fileSize) && (digest == offer.sha256);

    std::vector<uint8_t> vr{ uint8_t(ok ? 1 : 0) };
    WriteFrame(conn, MsgType::Verified, vr);

    if (!ok) {
        r.error = "integrity mismatch (expected " + Sha256::ToHex(offer.sha256) +
                  ", got " + Sha256::ToHex(digest) + ")";
        return r;
    }
    r.ok = true;
    r.bytes = offer.fileSize;
    return r;
}

} // namespace AnchorPoint
