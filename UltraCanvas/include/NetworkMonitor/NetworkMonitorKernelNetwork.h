// include/NetworkMonitor/NetworkMonitorKernelNetwork.h
// The Windows kernel's network events (Microsoft-Windows-Kernel-Network,
// the ETW provider), parsed as bytes: which TCP event an id is, and its
// payload - PID, size, the two endpoints and the connection id - read by
// the template each event uses. Pure functions with the provider's
// constants spelled out, so the parser compiles and is tested from fixture
// bytes on every platform; only the trace session that receives the events
// is Windows (OS/MSWindows/UltraCanvasWindowsNetworkMonitorEvents.cpp).
// Internal to the module.
//
// The three templates, the same as the classic TcpIp_V4 / TcpIp_V6 MOF
// classes the provider descends from, share a prefix - PID, size, daddr,
// saddr, dport, sport - and differ in what sits between the ports and the
// trailing seqnum and connid: nothing for a receive or a disconnect
// (TypeGroup1), the TCP options for a connect or an accept (mss, sackopt,
// tsopt, wsopt, rcvwin, rcvwinscale, sndwinscale: TypeGroup2), and the
// start and end time of a send (SendIPV4 / SendIPV6). Reading every event
// by TypeGroup1 gives a connect a connection id that no later event of
// the connection carries.
//
// Version: 0.9.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace UltraCanvas {
namespace NetworkMonitorKernelNetwork {

// Event ids. TCPv4: 10 send, 11 receive, 12 connect, 13 disconnect,
// 14 retransmit, 15 accept, 16 reconnect, 17 connect failed, 18 copy;
// TCPv6: 26-32 and 34 in the same order. UDP (42, 43, 58, 59) has no
// lifecycle and is not consumed, nor are retransmit, reconnect, copy and
// the failed connect, which carries no connection id.
constexpr uint16_t kTcp4Send = 10, kTcp4Receive = 11, kTcp4Connect = 12, kTcp4Disconnect = 13, kTcp4Accept = 15;
constexpr uint16_t kTcp6Send = 26, kTcp6Receive = 27, kTcp6Connect = 28, kTcp6Disconnect = 29, kTcp6Accept = 31;

enum class Kind { Other, Send, Receive, Connect, Disconnect, Accept };

struct Event {
    Kind        kind = Kind::Other;
    bool        ipv6 = false;
    uint32_t    pid = 0;
    uint32_t    size = 0;             // bytes, for a send or a receive
    std::string remoteAddress;        // daddr
    std::string localAddress;         // saddr
    uint16_t    remotePort = 0;       // dport
    uint16_t    localPort = 0;        // sport
    uint64_t    connectionId = 0;     // connid: the same in every event of one connection
};

// Parses one event's payload. pointerBytes is 4 or 8, from the event
// header (EVENT_HEADER_FLAG_32_BIT_HEADER). False for an id this module
// does not consume, a pointer size it does not know, or a payload shorter
// than its template.
bool Parse(uint16_t eventId, const unsigned char* data, std::size_t size, std::size_t pointerBytes, Event& out);

} // namespace NetworkMonitorKernelNetwork
} // namespace UltraCanvas
