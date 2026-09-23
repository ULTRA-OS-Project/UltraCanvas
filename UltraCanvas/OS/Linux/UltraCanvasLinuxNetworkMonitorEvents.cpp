// OS/Linux/UltraCanvasLinuxNetworkMonitorEvents.cpp
// The Linux connection tracker as an event source. nf_conntrack reports
// every tracked connection over NETLINK_NETFILTER as it is created and as
// it is destroyed - with the bytes each direction moved, when accounting
// is on - and this source subscribes to the NEW and DESTROY groups and
// turns the messages (NetworkMonitorConntrack.h) into events. No driver,
// no packet capture, no polling; what it needs is CAP_NET_ADMIN (root)
// for the subscription, and a tracker that is *active*: the kernel only
// registers the conntrack hooks once a firewall rule asks for them, so on
// a machine with no firewall the socket binds and nothing ever arrives.
// The source says so rather than sitting silent. It never adds a rule;
// the monitor observes.
//
// The tracker knows tuples, not processes; the registry attributes each
// event from the socket table. It also cannot tell which side is local,
// so the tuple goes out in the original direction (the side that sent the
// first packet) and the registry's lookup, which tries both orientations,
// turns a connection that came to a listener here into an Accepted.
//
// Version: 0.5.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitorConntrack.h"
#include "NetworkMonitor/NetworkMonitorEvents.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <linux/netlink.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef NETLINK_NETFILTER
#define NETLINK_NETFILTER 12
#endif

namespace UltraCanvas {
namespace {

const char* const kConntrackCountPath = "/proc/sys/net/netfilter/nf_conntrack_count";
const char* const kConntrackAcctPath = "/proc/sys/net/netfilter/nf_conntrack_acct";

std::string ReadSysctl(const char* path) {
    std::ifstream file(path);
    std::string value;
    std::getline(file, value);
    return value;
}

class ConntrackEventSource : public IConnectionEventSource {
public:
    ~ConntrackEventSource() override { Stop(); }

    std::string Name() const override { return "nf_conntrack"; }
    bool ReportsBytes() const override { return accounting_; }
    bool IsRunning() const override { return running_.load(); }

    std::string LastError() const override {
        {
            std::lock_guard<std::mutex> lock(errorMutex_);
            if (!lastError_.empty()) return lastError_;
        }
        // Bound, healthy, and nothing ever came: the tracker is idle.
        if (running_.load() && events_.load() == 0 && ReadSysctl(kConntrackCountPath) == "0") {
            return "the connection tracker is idle - no firewall rule has activated it, so it "
                   "reports nothing (a rule that matches conntrack state activates it)";
        }
        return std::string();
    }

    NetworkMonitorResult Start(std::function<void(const NetworkConnectionEvent&)> onEvent) override {
        if (running_.load()) return NetworkMonitorResult::Ok();
        if (::access(kConntrackCountPath, R_OK) != 0) {
            return NetworkMonitorResult::Error(NetworkMonitorResultCode::NotSupported,
                "nf_conntrack is not loaded on this kernel (no /proc/sys/net/netfilter/nf_conntrack_count).");
        }
        fd_ = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_NETFILTER);
        if (fd_ < 0) {
            const int error = errno;
            return NetworkMonitorResult::Error(
                error == EPERM || error == EACCES ? NetworkMonitorResultCode::PermissionDenied
                                                  : NetworkMonitorResultCode::IoError,
                std::string("Opening the netfilter netlink socket: ") + std::strerror(error));
        }
        sockaddr_nl address{};
        address.nl_family = AF_NETLINK;
        address.nl_groups = NetworkMonitorConntrack::kGroupNew | NetworkMonitorConntrack::kGroupDestroy;
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
            const int error = errno;
            ::close(fd_);
            fd_ = -1;
            return NetworkMonitorResult::Error(
                error == EPERM || error == EACCES ? NetworkMonitorResultCode::PermissionDenied
                                                  : NetworkMonitorResultCode::IoError,
                std::string("Subscribing to conntrack events: ") + std::strerror(error) +
                (error == EPERM || error == EACCES ? " (CAP_NET_ADMIN - run as root)" : ""));
        }
        // A busy tracker can burst; a larger receive buffer loses fewer.
        int bufferBytes = 4 * 1024 * 1024;
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUFFORCE, &bufferBytes, sizeof bufferBytes);
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &bufferBytes, sizeof bufferBytes);
        accounting_ = ReadSysctl(kConntrackAcctPath) == "1";
        onEvent_ = std::move(onEvent);
        stop_ = false;
        running_ = true;
        worker_ = std::thread([this] { Run(); });
        return NetworkMonitorResult::Ok();
    }

    void Stop() override {
        if (!running_.load()) return;
        stop_ = true;
        if (worker_.joinable()) worker_.join();
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
        running_ = false;
    }

private:
    void Run() {
        std::vector<unsigned char> buffer(64 * 1024);
        while (!stop_.load()) {
            pollfd waiting{};
            waiting.fd = fd_;
            waiting.events = POLLIN;
            const int ready = ::poll(&waiting, 1, 200);
            if (ready < 0) {
                if (errno == EINTR) continue;
                std::lock_guard<std::mutex> lock(errorMutex_);
                lastError_ = std::string("poll failed: ") + std::strerror(errno);
                return;
            }
            if (ready == 0) continue;
            const ssize_t got = ::recv(fd_, buffer.data(), buffer.size(), 0);
            if (got < 0) {
                if (errno == EAGAIN || errno == EINTR) continue;
                if (errno == ENOBUFS) { ++dropped_; continue; }   // the kernel overran us
                std::lock_guard<std::mutex> lock(errorMutex_);
                lastError_ = std::string("recv failed: ") + std::strerror(errno);
                return;
            }
            // One datagram may carry several messages.
            std::size_t offset = 0;
            while (offset + 16 <= static_cast<std::size_t>(got)) {
                uint32_t length;
                std::memcpy(&length, buffer.data() + offset, 4);
                if (length < 16 || offset + length > static_cast<std::size_t>(got)) break;
                Handle(buffer.data() + offset, length);
                offset += (length + 3) & ~static_cast<std::size_t>(3);
            }
        }
    }

    void Handle(const unsigned char* data, std::size_t size) {
        NetworkMonitorConntrack::Flow flow;
        if (!NetworkMonitorConntrack::Parse(data, size, flow)) return;
        if (flow.message == NetworkMonitorConntrack::Message::Update ||
            flow.message == NetworkMonitorConntrack::Message::Other) {
            return;
        }
        if (flow.transport == NetworkTransport::Other) return;
        NetworkConnectionEvent e;
        e.kind = flow.message == NetworkMonitorConntrack::Message::Destroy ? NetworkEventKind::Closed
                                                                            : NetworkEventKind::Opened;
        e.transport = flow.transport;
        e.family = flow.family;
        e.localAddress = flow.sourceAddress;       // the original direction; the
        e.localPort = flow.sourcePort;             // registry may turn it round
        e.remoteAddress = flow.destinationAddress;
        e.remotePort = flow.destinationPort;
        if (e.kind == NetworkEventKind::Closed) {
            e.bytesSent = flow.bytesOriginal;
            e.bytesReceived = flow.bytesReply;
        }
        e.observedAtMs = NetworkMonitor_NowMs();
        e.sourceName = Name();
        ++events_;
        if (onEvent_) onEvent_(e);
    }

    int fd_ = -1;
    bool accounting_ = false;
    std::function<void(const NetworkConnectionEvent&)> onEvent_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<int64_t> events_{0};
    std::atomic<int64_t> dropped_{0};
    mutable std::mutex errorMutex_;
    std::string lastError_;
};

} // namespace

std::unique_ptr<IConnectionEventSource> NetworkMonitor_CreateSystemEventSource() {
    return std::make_unique<ConntrackEventSource>();
}

} // namespace UltraCanvas
