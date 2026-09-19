// core/NetworkMonitor/NetworkMonitorProcfs.cpp
// The /proc/net table parser. See the header for the format; the one thing
// worth repeating is that the kernel prints addresses in host byte order, so
// "0100007F" is the bytes 7F 00 00 01 read back from a little-endian word,
// i.e. 127.0.0.1 - not 1.0.0.127.
//
// Version: 0.1.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitorProcfs.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace UltraCanvas {
namespace NetworkMonitorProcfs {
namespace {

bool ParseHexWord(const std::string& text, std::size_t offset, uint32_t& out) {
    if (offset + 8 > text.size()) return false;
    uint32_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        const char c = text[offset + i];
        unsigned digit;
        if (c >= '0' && c <= '9')      digit = static_cast<unsigned>(c - '0');
        else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
        else return false;
        value = (value << 4) | digit;
    }
    out = value;
    return true;
}

// A host-order word back to the four bytes it was read from, low byte first.
void WordToBytes(uint32_t word, unsigned char* bytes) {
    bytes[0] = static_cast<unsigned char>(word & 0xFF);
    bytes[1] = static_cast<unsigned char>((word >> 8) & 0xFF);
    bytes[2] = static_cast<unsigned char>((word >> 16) & 0xFF);
    bytes[3] = static_cast<unsigned char>((word >> 24) & 0xFF);
}

std::string FormatIPv4(const unsigned char* b) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return buffer;
}

// RFC 5952 text form: lower-case hex, the longest run of zero groups (at
// least two long) collapsed once, leftmost on a tie. Written here rather than
// through inet_ntop so the parser stays free of platform headers and behaves
// identically on every host the test runs on.
std::string FormatIPv6(const unsigned char* b) {
    // An IPv4-mapped address (::ffff:a.b.c.d) is what a dual-stack socket
    // reports for an IPv4 peer; print it the way inet_ntop does, so it reads
    // as the IPv4 address it is and the loopback test can recognise it.
    bool mapped = true;
    for (int i = 0; i < 10; ++i) if (b[i] != 0) { mapped = false; break; }
    if (mapped && b[10] == 0xFF && b[11] == 0xFF) return "::ffff:" + FormatIPv4(b + 12);

    uint16_t groups[8];
    for (int i = 0; i < 8; ++i) {
        groups[i] = static_cast<uint16_t>((b[2 * i] << 8) | b[2 * i + 1]);
    }
    int bestStart = -1, bestLength = 0;
    for (int i = 0; i < 8;) {
        if (groups[i] != 0) { ++i; continue; }
        int j = i;
        while (j < 8 && groups[j] == 0) ++j;
        if (j - i > bestLength) { bestStart = i; bestLength = j - i; }
        i = j;
    }
    if (bestLength < 2) bestStart = -1;

    std::string text;
    char buffer[8];
    for (int i = 0; i < 8;) {
        if (i == bestStart) {
            text += "::";
            i += bestLength;
            continue;
        }
        if (!text.empty() && text.back() != ':') text += ':';
        std::snprintf(buffer, sizeof buffer, "%x", groups[i]);
        text += buffer;
        ++i;
    }
    return text;
}

} // namespace

bool DecodeAddress(const std::string& field, NetworkAddressFamily family,
                   std::string& outAddress, uint16_t& outPort) {
    const std::size_t colon = field.find(':');
    if (colon == std::string::npos || colon + 5 != field.size()) return false;
    const std::size_t addressLength = family == NetworkAddressFamily::IPv4 ? 8 : 32;
    if (colon != addressLength) return false;

    unsigned char bytes[16] = {0};
    for (std::size_t word = 0; word < addressLength / 8; ++word) {
        uint32_t value;
        if (!ParseHexWord(field, word * 8, value)) return false;
        WordToBytes(value, bytes + word * 4);
    }
    uint32_t port;
    {
        // Four hex digits; reuse the word parser on a zero-padded copy.
        const std::string padded = "0000" + field.substr(colon + 1);
        if (!ParseHexWord(padded, 0, port)) return false;
    }
    outAddress = family == NetworkAddressFamily::IPv4 ? FormatIPv4(bytes) : FormatIPv6(bytes);
    outPort = static_cast<uint16_t>(port);
    return true;
}

NetworkConnectionState StateFromCode(unsigned code, NetworkTransport transport) {
    if (transport == NetworkTransport::Udp) {
        return code == 1 ? NetworkConnectionState::Established
                         : NetworkConnectionState::Unconnected;
    }
    switch (code) {
        case 1:  return NetworkConnectionState::Established;
        case 2:  return NetworkConnectionState::SynSent;
        case 3:  return NetworkConnectionState::SynReceived;
        case 4:  return NetworkConnectionState::FinWait1;
        case 5:  return NetworkConnectionState::FinWait2;
        case 6:  return NetworkConnectionState::TimeWait;
        case 7:  return NetworkConnectionState::Closed;
        case 8:  return NetworkConnectionState::CloseWait;
        case 9:  return NetworkConnectionState::LastAck;
        case 10: return NetworkConnectionState::Listening;
        case 11: return NetworkConnectionState::Closing;
        case 12: return NetworkConnectionState::SynReceived;  // TCP_NEW_SYN_RECV
        default: return NetworkConnectionState::Unknown;
    }
}

std::size_t ParseTable(const std::string& text, NetworkTransport transport,
                       NetworkAddressFamily family,
                       std::vector<NetworkConnection>& out) {
    // Columns, whitespace-separated:
    //   sl local_address rem_address st tx_queue:rx_queue tr:tm->when
    //   retrnsmt uid timeout inode ...
    // The header row's first token is "sl"; a data row's is "<n>:".
    std::size_t appended = 0;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string slot, local, remote, state, queues, timer, retransmit, uid, timeout, inode;
        if (!(fields >> slot >> local >> remote >> state >> queues >> timer
                     >> retransmit >> uid >> timeout >> inode)) {
            continue;
        }
        if (slot.empty() || slot.back() != ':') continue;

        NetworkConnection connection;
        connection.transport = transport;
        connection.family = family;
        if (!DecodeAddress(local, family, connection.localAddress, connection.localPort)) continue;
        if (!DecodeAddress(remote, family, connection.remoteAddress, connection.remotePort)) continue;

        char* end = nullptr;
        const unsigned long stateCode = std::strtoul(state.c_str(), &end, 16);
        if (end == state.c_str()) continue;
        connection.state = StateFromCode(static_cast<unsigned>(stateCode), transport);

        connection.ownerUid = static_cast<uint32_t>(std::strtoul(uid.c_str(), nullptr, 10));
        connection.socketInode = std::strtoull(inode.c_str(), nullptr, 10);

        out.push_back(std::move(connection));
        ++appended;
    }
    return appended;
}

} // namespace NetworkMonitorProcfs
} // namespace UltraCanvas
