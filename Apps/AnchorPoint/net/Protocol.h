// Apps/AnchorPoint/net/Protocol.h
// AnchorPoint - file-transfer wire protocol (framing, offer/accept, chunking,
// integrity verification, resume).
// Version: 0.1.0
// Author: AnchorPoint
#pragma once

#include "Transport.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace AnchorPoint {

// Wire frame: [1 byte type][4 byte big-endian length N][N bytes payload].
enum class MsgType : uint8_t {
    Hello    = 0x01, // [2B protoVersion][2B nameLen][name]   handshake
    Offer    = 0x02, // file metadata (see OfferInfo)
    Accept   = 0x03, // [8B resumeOffset]
    Reject   = 0x04, // [reason string]
    Chunk    = 0x05, // [8B offset][bytes...]
    Done     = 0x06, // [32B sha256]  sender asserts whole-file digest
    Verified = 0x07, // [1B ok]
    Error    = 0x08, // [message string]
};

constexpr uint16_t kProtocolVersion = 1;
constexpr uint32_t kDefaultChunkSize = 256 * 1024;   // 256 KiB
constexpr uint32_t kMaxFrameSize     = 16 * 1024 * 1024; // safety cap

struct OfferInfo {
    std::string fileName;                 // base name only (no path)
    uint64_t fileSize = 0;
    uint32_t chunkSize = kDefaultChunkSize;
    std::array<uint8_t, 32> sha256{};     // whole-file digest
};

// Progress callback: (bytesTransferred, totalBytes). Optional.
using ProgressFn = std::function<void(uint64_t, uint64_t)>;

struct TransferResult {
    bool ok = false;
    std::string error;
    uint64_t bytes = 0;
};

// ---- Sender side ----------------------------------------------------------
// Reads `filePath` from disk, offers it to the peer, streams it, and waits for
// the receiver's integrity verification. `displayName` is sent in the Hello.
TransferResult SendFile(IConnection& conn,
                        const std::string& filePath,
                        const std::string& displayName,
                        ProgressFn onProgress = nullptr);

// ---- Receiver side --------------------------------------------------------
// Decides whether to accept an incoming offer. Return the absolute output path
// to accept (existing partial file enables resume), or empty string to reject.
using AcceptFn = std::function<std::string(const OfferInfo&, const std::string& peerName)>;

// Receives one offered file. `accept` chooses the destination (or rejects).
TransferResult ReceiveFile(IConnection& conn,
                           const AcceptFn& accept,
                           ProgressFn onProgress = nullptr);

} // namespace AnchorPoint
