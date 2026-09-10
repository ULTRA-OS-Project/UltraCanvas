// SmartHome/tests/AshCodecTest.cpp
// Exercises the ASH framing without a radio.
//
// ASH is the one part of the Zigbee transport that can be verified properly
// before hardware exists: every rule is a function from bytes to bytes. The CRC
// is checked against the published CRC-16/CCITT-FALSE check value rather than
// against itself, so a wrong polynomial or seed cannot pass by agreeing with
// its own mistake.
//
// Author: UltraCanvas Framework

#include "AshCodec.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace UltraCanvas::SmartHome::Ash;

static int failures = 0;

static void Check(bool condition, const char* what) {
    if (condition) { std::printf("ok  %s\n", what); }
    else           { std::printf("FAIL %s\n", what); ++failures; }
}

int main() {
    // ---- CRC, against the standard check value ----
    {
        const char* check = "123456789";
        const uint16_t crc = Crc16(reinterpret_cast<const uint8_t*>(check), 9);
        // CRC-16/CCITT-FALSE of "123456789" is 0x29B1. This is the published
        // check constant for the algorithm, so it catches a wrong polynomial,
        // seed, or bit order — none of which a round-trip test would notice.
        Check(crc == 0x29B1, "CRC-16/CCITT-FALSE matches the published check value");
        if (crc != 0x29B1) std::printf("     got 0x%04X, expected 0x29B1\n", crc);
    }

    // ---- randomisation is its own inverse ----
    {
        std::vector<uint8_t> data{0x00, 0xFF, 0x42, 0x7E, 0x7D, 0x11, 0x13, 0x18, 0x1A};
        const std::vector<uint8_t> original = data;
        Randomise(data);
        Check(data != original, "randomisation actually changes the bytes");
        Randomise(data);
        Check(data == original, "randomisation applied twice returns the original");
    }

    // ---- stuffing removes every reserved byte ----
    {
        const std::vector<uint8_t> reserved{0x7E, 0x7D, 0x11, 0x13, 0x18, 0x1A};
        const std::vector<uint8_t> stuffed = Stuff(reserved);
        bool clean = true;
        for (uint8_t b : stuffed) {
            if (b != 0x7D && (b == 0x7E || b == 0x11 || b == 0x13 || b == 0x18 || b == 0x1A)) {
                clean = false;
            }
        }
        Check(clean, "no reserved byte survives stuffing except the escape itself");
        auto back = Unstuff(stuffed);
        Check(back && *back == reserved, "unstuffing restores the original bytes");

        // A frame cut off directly after an escape is malformed, not empty.
        auto truncated = Unstuff({0x41, 0x7D});
        Check(!truncated, "an escape with nothing after it is rejected");
    }

    // ---- DATA frames round-trip, sequence numbers included ----
    {
        const std::vector<uint8_t> ezsp{0x00, 0x00, 0x04, 0x02, 0x11, 0x30};
        const std::vector<uint8_t> wire = EncodeData(ezsp, 3, 5, true);
        Check(wire.back() == kFlag, "a frame ends with the flag byte");

        // Strip the flag; DecodeFrame takes the body.
        std::vector<uint8_t> body(wire.begin(), wire.end() - 1);
        auto frame = DecodeFrame(body);
        Check(frame.has_value(), "a DATA frame decodes");
        if (frame) {
            Check(frame->Type == FrameType::Data, "  type is DATA");
            Check(frame->FrameNumber == 3, "  frame number survives");
            Check(frame->AckNumber == 5, "  ack number survives");
            Check(frame->Retransmitted, "  the retransmit bit survives");
            Check(frame->Payload == ezsp, "  the payload comes back de-randomised");
        }
    }

    // ---- a corrupted frame is rejected, not misread ----
    {
        const std::vector<uint8_t> wire = EncodeData({0xAA, 0xBB, 0xCC}, 1, 2);
        std::vector<uint8_t> body(wire.begin(), wire.end() - 1);
        body[2] ^= 0xFF;                       // flip a payload byte
        Check(!DecodeFrame(body).has_value(), "a payload bit flip fails the CRC");

        std::vector<uint8_t> shortBody{0x80, 0x11};
        Check(!DecodeFrame(shortBody).has_value(), "a frame too short to hold a CRC is rejected");
    }

    // ---- control frames ----
    {
        // Bind the encoded frame before slicing it: calling EncodeAck twice in
        // one initialiser makes two different temporaries and the iterators
        // would come from different vectors.
        const std::vector<uint8_t> ackWire = EncodeAck(4);
        auto ack = DecodeFrame({ackWire.begin(), ackWire.end() - 1});
        Check(ack && ack->Type == FrameType::Ack && ack->AckNumber == 4, "ACK round-trips");

        const std::vector<uint8_t> nakWire = EncodeNak(6, true);
        auto nak = DecodeFrame({nakWire.begin(), nakWire.end() - 1});
        Check(nak && nak->Type == FrameType::Nak && nak->AckNumber == 6 && nak->NotReady,
              "NAK round-trips, not-ready included");

        const std::vector<uint8_t> rst = EncodeReset();
        Check(rst.front() == kCancel, "a reset is preceded by CANCEL");
    }

    // ---- the stream reader ----
    {
        Reader reader;
        const std::vector<uint8_t> a = EncodeData({0x01, 0x02}, 0, 0);
        const std::vector<uint8_t> b = EncodeAck(1);

        // Two frames in one read.
        std::vector<uint8_t> both = a;
        both.insert(both.end(), b.begin(), b.end());
        auto frames = reader.Feed(both.data(), both.size());
        Check(frames.size() == 2, "two frames in one buffer both come out");

        // The same two frames split mid-frame across reads — the case a real
        // serial port produces constantly.
        reader.Reset();
        size_t split = a.size() / 2;
        auto first = reader.Feed(both.data(), split);
        Check(first.empty(), "a partial frame yields nothing yet");
        auto rest = reader.Feed(both.data() + split, both.size() - split);
        Check(rest.size() == 2, "the frame completes across a read boundary");

        // CANCEL abandons what came before it.
        reader.Reset();
        std::vector<uint8_t> cancelled{0x11, 0x22, kCancel};
        cancelled.insert(cancelled.end(), b.begin(), b.end());
        auto afterCancel = reader.Feed(cancelled.data(), cancelled.size());
        Check(afterCancel.size() == 1 && reader.CancelledFrames() == 1,
              "CANCEL discards the partial frame and the next one still arrives");

        // Garbage between flags is counted and dropped, not returned.
        reader.Reset();
        std::vector<uint8_t> junk{0x01, 0x02, 0x03, kFlag};
        auto none = reader.Feed(junk.data(), junk.size());
        Check(none.empty() && reader.DiscardedFrames() == 1,
              "an undecodable frame is dropped and counted");
    }

    if (failures == 0) {
        std::puts("\nPASS - ASH framing behaves");
        return 0;
    }
    std::printf("\n%d check(s) failed\n", failures);
    return 1;
}
