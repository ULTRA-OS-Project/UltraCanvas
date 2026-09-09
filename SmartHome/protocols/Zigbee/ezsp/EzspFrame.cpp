// protocols/Zigbee/ezsp/EzspFrame.cpp
// Author: UltraCanvas Framework

#include "EzspFrame.h"

namespace UltraCanvas {
namespace SmartHome {
namespace Ezsp {

namespace {
// Frame control bits that matter here.
constexpr uint8_t kResponseBit = 0x80;
constexpr uint8_t kOverflowBit = 0x01;
constexpr uint8_t kTruncatedBit = 0x02;
}  // namespace

void AppendU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void AppendU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

uint16_t ReadU16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 1 >= data.size()) return 0;
    return static_cast<uint16_t>(data[offset] |
                                 (static_cast<uint16_t>(data[offset + 1]) << 8));
}

uint32_t ReadU32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 3 >= data.size()) return 0;
    uint32_t value = 0;
    for (int i = 3; i >= 0; --i) {
        value = (value << 8) | data[offset + static_cast<size_t>(i)];
    }
    return value;
}

std::vector<uint8_t> EncodeVersionCommand(uint8_t sequence, uint8_t desiredVersion) {
    // Legacy header: sequence, frame control, frame id (one byte), then the
    // desired version as the only parameter.
    return {sequence, 0x00, static_cast<uint8_t>(kVersion), desiredVersion};
}

std::optional<VersionResponse> DecodeVersionResponse(const std::vector<uint8_t>& frame) {
    // A legacy-format response: three header bytes then four parameter bytes.
    if (frame.size() < 7) return std::nullopt;
    VersionResponse response;
    response.ProtocolVersion = frame[3];
    response.StackType = frame[4];
    response.StackVersion = static_cast<uint16_t>(
        frame[5] | (static_cast<uint16_t>(frame[6]) << 8));
    return response;
}

std::vector<uint8_t> EncodeCommand(uint8_t protocolVersion, uint8_t sequence,
                                   uint16_t frameId,
                                   const std::vector<uint8_t>& parameters) {
    std::vector<uint8_t> frame;
    if (protocolVersion >= 8) {
        frame.reserve(parameters.size() + 5);
        frame.push_back(sequence);
        frame.push_back(0x00);      // frame control low
        frame.push_back(0x01);      // frame control high: EZSP v8 frame format
        AppendU16(frame, frameId);
    } else {
        frame.reserve(parameters.size() + 3);
        frame.push_back(sequence);
        frame.push_back(0x00);
        frame.push_back(static_cast<uint8_t>(frameId & 0xFF));
    }
    frame.insert(frame.end(), parameters.begin(), parameters.end());
    return frame;
}

std::optional<Frame> DecodeFrame(uint8_t protocolVersion,
                                 const std::vector<uint8_t>& raw) {
    const size_t headerSize = protocolVersion >= 8 ? 5u : 3u;
    if (raw.size() < headerSize) return std::nullopt;

    Frame frame;
    frame.Sequence = raw[0];
    const uint8_t control = raw[1];
    frame.IsResponse = (control & kResponseBit) != 0;
    frame.Overflow = (control & kOverflowBit) != 0;
    frame.Truncated = (control & kTruncatedBit) != 0;

    if (protocolVersion >= 8) {
        frame.Id = static_cast<uint16_t>(raw[3] | (static_cast<uint16_t>(raw[4]) << 8));
    } else {
        frame.Id = raw[2];
    }
    frame.Parameters.assign(raw.begin() + static_cast<long>(headerSize), raw.end());
    return frame;
}

}  // namespace Ezsp
}  // namespace SmartHome
}  // namespace UltraCanvas
