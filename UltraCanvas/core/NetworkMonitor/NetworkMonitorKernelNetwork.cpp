// core/NetworkMonitor/NetworkMonitorKernelNetwork.cpp
// Microsoft-Windows-Kernel-Network payload parsing over raw bytes: the
// template is chosen from the event id, every field read at its offset,
// the whole length checked first. Ports are in network order in the
// payload (the manifest's win:Port), integers in the host's.
//
// Version: 0.9.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitorKernelNetwork.h"
#include "NetworkMonitor/NetworkMonitorAddress.h"

#include <cstring>

namespace UltraCanvas {
namespace NetworkMonitorKernelNetwork {
namespace {

// What sits between sport and seqnum in each template.
constexpr std::size_t kTypeGroup1Extra = 0;                       // receive, disconnect
constexpr std::size_t kTypeGroup2Extra = 2 + 2 + 2 + 2 + 4 + 2 + 2; // connect, accept: the TCP options
constexpr std::size_t kSendExtra = 4 + 4;                         // send: startime, endtime

struct Template {
    Kind        kind;
    bool        ipv6;
    std::size_t extra;
};

bool TemplateFor(uint16_t eventId, Template& out) {
    switch (eventId) {
        case kTcp4Send:       out = { Kind::Send,       false, kSendExtra };       return true;
        case kTcp4Receive:    out = { Kind::Receive,    false, kTypeGroup1Extra }; return true;
        case kTcp4Connect:    out = { Kind::Connect,    false, kTypeGroup2Extra }; return true;
        case kTcp4Disconnect: out = { Kind::Disconnect, false, kTypeGroup1Extra }; return true;
        case kTcp4Accept:     out = { Kind::Accept,     false, kTypeGroup2Extra }; return true;
        case kTcp6Send:       out = { Kind::Send,       true,  kSendExtra };       return true;
        case kTcp6Receive:    out = { Kind::Receive,    true,  kTypeGroup1Extra }; return true;
        case kTcp6Connect:    out = { Kind::Connect,    true,  kTypeGroup2Extra }; return true;
        case kTcp6Disconnect: out = { Kind::Disconnect, true,  kTypeGroup1Extra }; return true;
        case kTcp6Accept:     out = { Kind::Accept,     true,  kTypeGroup2Extra }; return true;
        default: return false;
    }
}

} // namespace

bool Parse(uint16_t eventId, const unsigned char* data, std::size_t size, std::size_t pointerBytes, Event& out) {
    Template layout;
    if (!TemplateFor(eventId, layout)) return false;
    if (pointerBytes != 4 && pointerBytes != 8) return false;
    const std::size_t addressBytes = layout.ipv6 ? 16 : 4;
    const std::size_t needed = 4 + 4 + 2 * addressBytes + 2 + 2 + layout.extra + 4 + pointerBytes;
    if (!data || size < needed) return false;

    out = Event();
    out.kind = layout.kind;
    out.ipv6 = layout.ipv6;
    std::size_t offset = 0;
    std::memcpy(&out.pid, data + offset, 4); offset += 4;
    std::memcpy(&out.size, data + offset, 4); offset += 4;
    out.remoteAddress = layout.ipv6 ? NetworkMonitorAddress::FormatIPv6(data + offset)
                                    : NetworkMonitorAddress::FormatIPv4(data + offset);
    offset += addressBytes;
    out.localAddress = layout.ipv6 ? NetworkMonitorAddress::FormatIPv6(data + offset)
                                   : NetworkMonitorAddress::FormatIPv4(data + offset);
    offset += addressBytes;
    out.remotePort = static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]); offset += 2;
    out.localPort = static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]); offset += 2;
    offset += layout.extra;   // the template's own fields
    offset += 4;              // seqnum
    out.connectionId = 0;
    std::memcpy(&out.connectionId, data + offset, pointerBytes);
    return true;
}

} // namespace NetworkMonitorKernelNetwork
} // namespace UltraCanvas
