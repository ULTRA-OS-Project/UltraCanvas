// protocols/Zigbee/ezsp/AshCodec.h
// ASH (Asynchronous Serial Host) framing, per Silicon Labs UG101/UG115.
//
// ASH is what carries EZSP between a host and an EM35x/EFR32 NCP over a serial
// line. This header is the pure half of it: framing, byte stuffing, the CRC and
// the data randomisation, with no serial port and no state. That separation is
// deliberate — every rule here is a function from bytes to bytes, so it can be
// tested exhaustively without a radio attached, which is the only way any of
// this gets verified before hardware exists.
//
// Author: UltraCanvas Framework

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace UltraCanvas {
namespace SmartHome {
namespace Ash {

// Reserved bytes. These may never appear inside a frame body, which is what the
// escaping below exists to guarantee.
enum : uint8_t {
    kFlag       = 0x7E,   // ends every frame
    kEscape     = 0x7D,   // next byte is stuffed
    kXOn        = 0x11,
    kXOff       = 0x13,
    kSubstitute = 0x18,   // marks a byte received with an error
    kCancel     = 0x1A,   // discard the partial frame before this
};

enum class FrameType {
    Data,
    Ack,
    Nak,
    Rst,
    RstAck,
    Error,
    Invalid,
};

struct Frame {
    FrameType Type = FrameType::Invalid;
    uint8_t Control = 0;
    // DATA only. frmNum is this frame's sequence, ackNum acknowledges the other
    // side up to (ackNum - 1), and reTx marks a retransmission.
    uint8_t FrameNumber = 0;
    uint8_t AckNumber = 0;
    bool Retransmitted = false;
    bool NotReady = false;          // ACK/NAK: receiver cannot take more yet
    std::vector<uint8_t> Payload;   // DATA: the EZSP frame, de-randomised
    uint8_t ResetCode = 0;          // RSTACK/ERROR
};

// ===== CRC =====

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR.
// ASH appends it high byte first.
uint16_t Crc16(const uint8_t* data, size_t length, uint16_t seed = 0xFFFF);

// ===== Data randomisation =====

// ASH XORs a DATA frame's body with a fixed pseudo-random sequence so that long
// runs of one value cannot look like reserved bytes. The sequence is its own
// inverse, so the same call both applies and removes it.
void Randomise(std::vector<uint8_t>& data);

// ===== Byte stuffing =====

// Replaces reserved bytes with kEscape followed by the byte XOR 0x20.
std::vector<uint8_t> Stuff(const std::vector<uint8_t>& data);
// Reverses Stuff. Returns nothing if the input ends mid-escape.
std::optional<std::vector<uint8_t>> Unstuff(const std::vector<uint8_t>& data);

// ===== Framing =====

// Each returns a complete frame, stuffed and flag-terminated, ready to write.
std::vector<uint8_t> EncodeData(const std::vector<uint8_t>& ezspFrame,
                                uint8_t frameNumber, uint8_t ackNumber,
                                bool retransmitted = false);
std::vector<uint8_t> EncodeAck(uint8_t ackNumber, bool notReady = false);
std::vector<uint8_t> EncodeNak(uint8_t ackNumber, bool notReady = false);
std::vector<uint8_t> EncodeReset();

// Decodes one frame body — the bytes between flags, still stuffed. Returns
// nothing when the frame is malformed or its CRC does not match, which is the
// same thing as far as a caller is concerned: ask for a retransmission.
std::optional<Frame> DecodeFrame(const std::vector<uint8_t>& stuffedBody);

// ===== Stream =====

// Feeds raw serial bytes in and yields whole frames. A serial read can split a
// frame anywhere, so something has to hold the partial one; this does, and
// nothing else in the transport has to think about it.
class Reader {
public:
    // Returns every frame completed by these bytes. Frames that fail to decode
    // are counted and dropped rather than returned.
    std::vector<Frame> Feed(const uint8_t* data, size_t length);

    size_t DiscardedFrames() const { return discarded; }
    size_t CancelledFrames() const { return cancelled; }
    void Reset();

private:
    std::vector<uint8_t> partial;
    size_t discarded = 0;
    size_t cancelled = 0;
};

}  // namespace Ash
}  // namespace SmartHome
}  // namespace UltraCanvas
