// protocols/Zigbee/ezsp/AshCodec.cpp
// Author: UltraCanvas Framework

#include "AshCodec.h"

namespace UltraCanvas {
namespace SmartHome {
namespace Ash {

namespace {

// A DATA frame body is control + payload + 2 CRC bytes, and the NCP's buffer
// bounds the payload. Anything longer is a framing error, not a long frame.
constexpr size_t kMaxPayload = 132;
constexpr size_t kMaxBody = kMaxPayload + 3;

bool IsReserved(uint8_t byte) {
    return byte == kFlag || byte == kEscape || byte == kXOn ||
           byte == kXOff || byte == kSubstitute || byte == kCancel;
}

void AppendCrc(std::vector<uint8_t>& body) {
    const uint16_t crc = Crc16(body.data(), body.size());
    body.push_back(static_cast<uint8_t>(crc >> 8));     // high byte first
    body.push_back(static_cast<uint8_t>(crc & 0xFF));
}

std::vector<uint8_t> Finish(std::vector<uint8_t> body) {
    AppendCrc(body);
    std::vector<uint8_t> out = Stuff(body);
    out.push_back(kFlag);
    return out;
}

}  // namespace

uint16_t Crc16(const uint8_t* data, size_t length, uint16_t seed) {
    uint16_t crc = seed;
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

void Randomise(std::vector<uint8_t>& data) {
    // UG101: seed 0x42, and after each byte shift right, XORing 0xB8 back in
    // when the bit shifted out was set.
    uint8_t rand = 0x42;
    for (auto& byte : data) {
        byte ^= rand;
        rand = (rand & 0x01) ? static_cast<uint8_t>((rand >> 1) ^ 0xB8)
                             : static_cast<uint8_t>(rand >> 1);
    }
}

std::vector<uint8_t> Stuff(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out;
    out.reserve(data.size() + 8);
    for (uint8_t byte : data) {
        if (IsReserved(byte)) {
            out.push_back(kEscape);
            out.push_back(static_cast<uint8_t>(byte ^ 0x20));
        } else {
            out.push_back(byte);
        }
    }
    return out;
}

std::optional<std::vector<uint8_t>> Unstuff(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out;
    out.reserve(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] != kEscape) {
            out.push_back(data[i]);
            continue;
        }
        // An escape as the final byte means the frame was cut short.
        if (++i >= data.size()) return std::nullopt;
        out.push_back(static_cast<uint8_t>(data[i] ^ 0x20));
    }
    return out;
}

std::vector<uint8_t> EncodeData(const std::vector<uint8_t>& ezspFrame,
                                uint8_t frameNumber, uint8_t ackNumber,
                                bool retransmitted) {
    std::vector<uint8_t> body;
    body.reserve(ezspFrame.size() + 3);
    body.push_back(static_cast<uint8_t>(((frameNumber & 0x07) << 4) |
                                        (retransmitted ? 0x08 : 0x00) |
                                        (ackNumber & 0x07)));
    std::vector<uint8_t> payload = ezspFrame;
    Randomise(payload);
    body.insert(body.end(), payload.begin(), payload.end());
    return Finish(std::move(body));
}

std::vector<uint8_t> EncodeAck(uint8_t ackNumber, bool notReady) {
    return Finish({static_cast<uint8_t>(0x80 | (notReady ? 0x08 : 0x00) |
                                        (ackNumber & 0x07))});
}

std::vector<uint8_t> EncodeNak(uint8_t ackNumber, bool notReady) {
    return Finish({static_cast<uint8_t>(0xA0 | (notReady ? 0x08 : 0x00) |
                                        (ackNumber & 0x07))});
}

std::vector<uint8_t> EncodeReset() {
    // A reset is preceded by CANCEL so the NCP throws away any partial frame it
    // was in the middle of when the host restarted.
    std::vector<uint8_t> out{kCancel};
    const std::vector<uint8_t> frame = Finish({0xC0});
    out.insert(out.end(), frame.begin(), frame.end());
    return out;
}

std::optional<Frame> DecodeFrame(const std::vector<uint8_t>& stuffedBody) {
    auto unstuffed = Unstuff(stuffedBody);
    if (!unstuffed) return std::nullopt;

    std::vector<uint8_t>& body = *unstuffed;
    // Control byte plus two CRC bytes is the shortest legal frame.
    if (body.size() < 3 || body.size() > kMaxBody) return std::nullopt;

    const uint16_t received = static_cast<uint16_t>(
        (static_cast<uint16_t>(body[body.size() - 2]) << 8) | body[body.size() - 1]);
    if (Crc16(body.data(), body.size() - 2) != received) return std::nullopt;

    body.resize(body.size() - 2);

    Frame frame;
    frame.Control = body[0];

    if ((frame.Control & 0x80) == 0) {
        frame.Type = FrameType::Data;
        frame.FrameNumber = static_cast<uint8_t>((frame.Control >> 4) & 0x07);
        frame.Retransmitted = (frame.Control & 0x08) != 0;
        frame.AckNumber = static_cast<uint8_t>(frame.Control & 0x07);
        frame.Payload.assign(body.begin() + 1, body.end());
        Randomise(frame.Payload);   // its own inverse
        return frame;
    }

    switch (frame.Control & 0xE0) {
        case 0x80:
            frame.Type = FrameType::Ack;
            frame.AckNumber = static_cast<uint8_t>(frame.Control & 0x07);
            frame.NotReady = (frame.Control & 0x08) != 0;
            return frame;
        case 0xA0:
            frame.Type = FrameType::Nak;
            frame.AckNumber = static_cast<uint8_t>(frame.Control & 0x07);
            frame.NotReady = (frame.Control & 0x08) != 0;
            return frame;
        case 0xC0:
            // RST, RSTACK and ERROR share the top bits and differ in the low
            // ones; the latter two carry a version byte and a reason code.
            if (frame.Control == 0xC0) {
                frame.Type = FrameType::Rst;
            } else if (frame.Control == 0xC1) {
                frame.Type = FrameType::RstAck;
                if (body.size() >= 3) frame.ResetCode = body[2];
            } else if (frame.Control == 0xC2) {
                frame.Type = FrameType::Error;
                if (body.size() >= 3) frame.ResetCode = body[2];
            } else {
                return std::nullopt;
            }
            return frame;
        default:
            return std::nullopt;
    }
}

std::vector<Frame> Reader::Feed(const uint8_t* data, size_t length) {
    std::vector<Frame> frames;
    for (size_t i = 0; i < length; ++i) {
        const uint8_t byte = data[i];

        // CANCEL abandons whatever was being assembled. SUBSTITUTE means the
        // line garbled a byte, so the rest of this frame is untrustworthy and
        // is dropped at the next flag rather than decoded.
        if (byte == kCancel || byte == kSubstitute) {
            if (!partial.empty()) ++cancelled;
            partial.clear();
            continue;
        }
        if (byte == kXOn || byte == kXOff) continue;   // flow control, not data

        if (byte != kFlag) {
            // A frame that never ends would otherwise grow without bound on a
            // line that is producing noise.
            if (partial.size() < kMaxBody * 2) partial.push_back(byte);
            continue;
        }

        if (partial.empty()) continue;   // flag with nothing before it
        if (auto frame = DecodeFrame(partial)) frames.push_back(*frame);
        else ++discarded;
        partial.clear();
    }
    return frames;
}

void Reader::Reset() {
    partial.clear();
    discarded = 0;
    cancelled = 0;
}

}  // namespace Ash
}  // namespace SmartHome
}  // namespace UltraCanvas
