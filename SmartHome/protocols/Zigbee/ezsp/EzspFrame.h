// protocols/Zigbee/ezsp/EzspFrame.h
// EZSP frame encoding, the layer that rides inside ASH DATA frames.
//
// Like AshCodec, this is pure byte manipulation and is tested on its own.
//
// EZSP has two frame formats. Up to version 7 the header is three bytes; from
// version 8 it is five, with a 16-bit frame id. A host cannot know which to use
// until it has asked, and the version command itself has to be asked in the
// legacy format for exactly that reason — it is the one command whose encoding
// is fixed.
//
// Author: UltraCanvas Framework

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace UltraCanvas {
namespace SmartHome {
namespace Ezsp {

// The handful of frame ids this backend needs. The full set is in UG100.
enum FrameId : uint16_t {
    kVersion            = 0x0000,
    kGetConfigValue     = 0x0052,
    kSetConfigValue     = 0x0053,
    kNetworkInit        = 0x0017,
    kFormNetwork        = 0x001E,
    kLeaveNetwork       = 0x0020,
    kPermitJoining      = 0x0022,
    kGetNetworkParams   = 0x0028,
    kSendUnicast        = 0x0034,
    kSendMulticast      = 0x0038,
    kIncomingMessage    = 0x0045,
    kStackStatus        = 0x0019,
    kGetEui64           = 0x0026,
    kGetNodeId          = 0x0027,
};

struct Frame {
    uint8_t Sequence = 0;
    uint16_t Id = 0;
    bool IsResponse = false;
    bool Overflow = false;      // the NCP dropped callbacks before this one
    bool Truncated = false;
    std::vector<uint8_t> Parameters;
};

// The version command, in the legacy three-byte header every EZSP version
// understands. `desiredVersion` is the protocol version the host would like.
std::vector<uint8_t> EncodeVersionCommand(uint8_t sequence, uint8_t desiredVersion);

// Reads the answer to the version command: the NCP's protocol version, its
// stack type and stack version.
struct VersionResponse {
    uint8_t ProtocolVersion = 0;
    uint8_t StackType = 0;
    uint16_t StackVersion = 0;
};
std::optional<VersionResponse> DecodeVersionResponse(const std::vector<uint8_t>& frame);

// Encodes a command in the format matching `protocolVersion` (v8 and later get
// the five-byte header, everything earlier the three-byte one).
std::vector<uint8_t> EncodeCommand(uint8_t protocolVersion, uint8_t sequence,
                                   uint16_t frameId,
                                   const std::vector<uint8_t>& parameters);

// Decodes a frame received from the NCP, using the same version rule.
std::optional<Frame> DecodeFrame(uint8_t protocolVersion,
                                 const std::vector<uint8_t>& raw);

// Little-endian helpers; EZSP parameters are little-endian throughout.
void AppendU16(std::vector<uint8_t>& out, uint16_t value);
void AppendU32(std::vector<uint8_t>& out, uint32_t value);
uint16_t ReadU16(const std::vector<uint8_t>& data, size_t offset);
uint32_t ReadU32(const std::vector<uint8_t>& data, size_t offset);

}  // namespace Ezsp
}  // namespace SmartHome
}  // namespace UltraCanvas
