// protocols/Zigbee/ezsp/AshTransport.h
// The stateful half of ASH: a serial port, the reset handshake, sequence
// numbers, acknowledgement and retransmission.
//
// AshCodec.h holds every rule that is a pure function over bytes and is tested
// on its own. What is left here is the part that genuinely needs a port and a
// clock, kept as small as possible for that reason.
//
// Author: UltraCanvas Framework

#pragma once

#include "AshCodec.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace UltraCanvas {
namespace SmartHome {
namespace Ash {

struct TransportConfig {
    std::string SerialPort = "/dev/ttyUSB0";
    int BaudRate = 115200;
    bool RtsCts = false;              // NCPs are commonly configured either way
    int AckTimeoutMs = 800;           // UG101 starts at 800 ms and adapts
    int MaxRetries = 3;               // then the link is declared down
    int ResetTimeoutMs = 2500;        // waiting for RSTACK after RST
};

// Received EZSP frames, and link-down notification. Both are called from the
// receive thread.
using OnEzspFrame = std::function<void(const std::vector<uint8_t>&)>;
using OnLinkDown = std::function<void(const std::string& reason)>;

class Transport {
public:
    Transport() = default;
    ~Transport();

    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    void SetOnEzspFrame(OnEzspFrame callback) { onFrame = std::move(callback); }
    void SetOnLinkDown(OnLinkDown callback) { onLinkDown = std::move(callback); }

    // Opens the port, resets the NCP and waits for RSTACK. False means the port
    // would not open or the NCP never answered; LastError() says which.
    bool Open(const TransportConfig& config);
    void Close();
    bool IsOpen() const { return open; }

    // Queues an EZSP frame and returns once it has been acknowledged, or false
    // if the retries ran out. Blocking is deliberate: EZSP is
    // request/response and a caller that carried on regardless would interleave
    // its own commands.
    bool Send(const std::vector<uint8_t>& ezspFrame);

    std::string LastError() const;

    // Diagnostics, useful when a link misbehaves in the field.
    struct Stats {
        uint64_t FramesSent = 0;
        uint64_t FramesReceived = 0;
        uint64_t Retransmissions = 0;
        uint64_t CrcFailures = 0;
        uint64_t Naks = 0;
    };
    Stats GetStats() const;

private:
    void ReceiveLoop();
    bool WriteRaw(const std::vector<uint8_t>& bytes);
    void HandleFrame(const Frame& frame);
    void SetError(const std::string& message);

    int fd = -1;
    std::atomic<bool> open{false};
    std::atomic<bool> running{false};

    TransportConfig config;
    Reader reader;
    std::thread receiveThread;

    // ASH numbers frames modulo 8 in each direction.
    uint8_t nextFrameNumber = 0;
    uint8_t ackNumber = 0;          // what we have acknowledged from the NCP

    mutable std::mutex mutex;
    std::condition_variable ackCondition;
    bool awaitingAck = false;
    uint8_t awaitingFrameNumber = 0;
    bool nakReceived = false;
    bool linkDown = false;
    bool resetAcknowledged = false;   // RSTACK seen since the last RST

    mutable std::mutex errorMutex;
    std::string lastError;

    mutable std::mutex statsMutex;
    Stats stats;

    OnEzspFrame onFrame;
    OnLinkDown onLinkDown;
};

}  // namespace Ash
}  // namespace SmartHome
}  // namespace UltraCanvas
