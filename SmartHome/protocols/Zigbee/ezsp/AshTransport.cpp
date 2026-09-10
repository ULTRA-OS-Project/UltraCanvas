// protocols/Zigbee/ezsp/AshTransport.cpp
// Author: UltraCanvas Framework

#include "AshTransport.h"

#include <cerrno>
#include <cstring>

#ifndef _WIN32
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#endif

namespace UltraCanvas {
namespace SmartHome {
namespace Ash {

namespace {

#ifndef _WIN32
speed_t BaudConstant(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:     return B115200;   // what every EZSP NCP ships with
    }
}
#endif

}  // namespace

Transport::~Transport() {
    Close();
}

std::string Transport::LastError() const {
    std::lock_guard<std::mutex> lock(errorMutex);
    return lastError;
}

void Transport::SetError(const std::string& message) {
    std::lock_guard<std::mutex> lock(errorMutex);
    lastError = message;
}

Transport::Stats Transport::GetStats() const {
    std::lock_guard<std::mutex> lock(statsMutex);
    return stats;
}

bool Transport::Open(const TransportConfig& cfg) {
#ifdef _WIN32
    SetError("ASH transport is POSIX-only for now");
    return false;
#else
    if (open) return true;
    config = cfg;

    fd = ::open(config.SerialPort.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        SetError("cannot open " + config.SerialPort + ": " + std::strerror(errno));
        return false;
    }

    termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        SetError(std::string("tcgetattr: ") + std::strerror(errno));
        ::close(fd);
        fd = -1;
        return false;
    }

    cfsetispeed(&tty, BaudConstant(config.BaudRate));
    cfsetospeed(&tty, BaudConstant(config.BaudRate));

    // 8N1, no parity, no break handling, no modem control lines, receiver on.
    tty.c_cflag &= ~static_cast<tcflag_t>(PARENB | CSTOPB | CSIZE);
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    if (config.RtsCts) tty.c_cflag |= CRTSCTS;
    else               tty.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);

    // Raw in both directions. ASH carries bytes that a line discipline would
    // otherwise interpret — 0x11 and 0x13 are XON/XOFF, and canonical mode
    // would hold frames back waiting for a newline that never comes.
    tty.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ECHOE | ECHONL | ISIG);
    tty.c_iflag &= ~static_cast<tcflag_t>(IXON | IXOFF | IXANY | INLCR | ICRNL |
                                          IGNBRK | BRKINT | PARMRK | ISTRIP);
    tty.c_oflag &= ~static_cast<tcflag_t>(OPOST | ONLCR);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        SetError(std::string("tcsetattr: ") + std::strerror(errno));
        ::close(fd);
        fd = -1;
        return false;
    }
    tcflush(fd, TCIOFLUSH);

    reader.Reset();
    nextFrameNumber = 0;
    ackNumber = 0;
    linkDown = false;
    running = true;
    open = true;
    receiveThread = std::thread(&Transport::ReceiveLoop, this);

    // The NCP may be mid-session from a previous host; RST puts both ends back
    // to a known point, and RSTACK is how it says it got there.
    bool sawRstAck = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        awaitingAck = false;
        resetAcknowledged = false;
        lock.unlock();

        if (!WriteRaw(EncodeReset())) {
            SetError("could not write the reset frame");
            Close();
            return false;
        }

        lock.lock();
        sawRstAck = ackCondition.wait_for(
            lock, std::chrono::milliseconds(config.ResetTimeoutMs),
            [this] { return ackNumber == 0 && !linkDown && resetAcknowledged; });
    }

    if (!sawRstAck) {
        SetError("no RSTACK from the NCP on " + config.SerialPort +
                 " — wrong port, wrong baud rate, or nothing listening");
        Close();
        return false;
    }
    return true;
#endif
}

void Transport::Close() {
    running = false;
    if (receiveThread.joinable()) receiveThread.join();
#ifndef _WIN32
    if (fd >= 0) { ::close(fd); fd = -1; }
#endif
    open = false;
    ackCondition.notify_all();
}

bool Transport::WriteRaw(const std::vector<uint8_t>& bytes) {
#ifdef _WIN32
    (void)bytes;
    return false;
#else
    size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.data() + written, bytes.size() - written);
        if (n > 0) { written += static_cast<size_t>(n); continue; }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        return false;
    }
    return true;
#endif
}

bool Transport::Send(const std::vector<uint8_t>& ezspFrame) {
    if (!open) { SetError("transport is not open"); return false; }

    std::unique_lock<std::mutex> lock(mutex);
    const uint8_t frameNumber = nextFrameNumber;

    for (int attempt = 0; attempt <= config.MaxRetries; ++attempt) {
        awaitingAck = true;
        awaitingFrameNumber = frameNumber;
        nakReceived = false;

        const std::vector<uint8_t> wire =
            EncodeData(ezspFrame, frameNumber, ackNumber, attempt > 0);
        lock.unlock();
        const bool written = WriteRaw(wire);
        lock.lock();

        if (!written) {
            awaitingAck = false;
            SetError("serial write failed");
            return false;
        }
        {
            std::lock_guard<std::mutex> statsLock(statsMutex);
            ++stats.FramesSent;
            if (attempt > 0) ++stats.Retransmissions;
        }

        const bool acked = ackCondition.wait_for(
            lock, std::chrono::milliseconds(config.AckTimeoutMs),
            [this] { return !awaitingAck || nakReceived || linkDown; });

        if (linkDown) { awaitingAck = false; return false; }
        if (acked && !awaitingAck && !nakReceived) {
            nextFrameNumber = static_cast<uint8_t>((frameNumber + 1) & 0x07);
            return true;
        }
        // A NAK asks for this frame again immediately; a timeout means it was
        // lost. Both retry, which is why they share this path.
    }

    awaitingAck = false;
    SetError("no acknowledgement after " + std::to_string(config.MaxRetries) +
             " retries — the NCP has stopped answering");
    if (onLinkDown) onLinkDown(LastError());
    return false;
}

void Transport::HandleFrame(const Frame& frame) {
    switch (frame.Type) {
        case FrameType::Data: {
            {
                std::lock_guard<std::mutex> statsLock(statsMutex);
                ++stats.FramesReceived;
            }
            // Acknowledge the frame after this one, which is what ASH's ackNum
            // means: everything below it has been received.
            std::vector<uint8_t> payload;
            {
                std::lock_guard<std::mutex> lock(mutex);
                ackNumber = static_cast<uint8_t>((frame.FrameNumber + 1) & 0x07);
                // The NCP piggybacks its acknowledgement of our traffic here.
                if (awaitingAck &&
                    frame.AckNumber == ((awaitingFrameNumber + 1) & 0x07)) {
                    awaitingAck = false;
                }
                payload = frame.Payload;
            }
            WriteRaw(EncodeAck(ackNumber));
            ackCondition.notify_all();
            if (onFrame && !payload.empty()) onFrame(payload);
            break;
        }

        case FrameType::Ack: {
            std::lock_guard<std::mutex> lock(mutex);
            if (awaitingAck && frame.AckNumber == ((awaitingFrameNumber + 1) & 0x07)) {
                awaitingAck = false;
            }
            ackCondition.notify_all();
            break;
        }

        case FrameType::Nak: {
            {
                std::lock_guard<std::mutex> statsLock(statsMutex);
                ++stats.Naks;
            }
            std::lock_guard<std::mutex> lock(mutex);
            nakReceived = true;
            ackCondition.notify_all();
            break;
        }

        case FrameType::RstAck: {
            std::lock_guard<std::mutex> lock(mutex);
            resetAcknowledged = true;
            ackNumber = 0;
            nextFrameNumber = 0;
            ackCondition.notify_all();
            break;
        }

        case FrameType::Error: {
            std::lock_guard<std::mutex> lock(mutex);
            linkDown = true;
            ackCondition.notify_all();
            SetError("NCP reported an ASH error, code " +
                     std::to_string(static_cast<int>(frame.ResetCode)));
            if (onLinkDown) onLinkDown(LastError());
            break;
        }

        case FrameType::Rst:
        case FrameType::Invalid:
            break;
    }
}

void Transport::ReceiveLoop() {
#ifndef _WIN32
    uint8_t buffer[512];
    while (running) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(fd, &readable);
        timeval timeout{0, 50 * 1000};   // 50 ms, so Close() is responsive

        const int ready = ::select(fd + 1, &readable, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) continue;

        const ssize_t n = ::read(fd, buffer, sizeof buffer);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            break;   // the port went away
        }

        const size_t before = reader.DiscardedFrames();
        for (const Frame& frame : reader.Feed(buffer, static_cast<size_t>(n))) {
            HandleFrame(frame);
        }
        if (reader.DiscardedFrames() > before) {
            std::lock_guard<std::mutex> statsLock(statsMutex);
            stats.CrcFailures += reader.DiscardedFrames() - before;
        }
    }
#endif
}

}  // namespace Ash
}  // namespace SmartHome
}  // namespace UltraCanvas
