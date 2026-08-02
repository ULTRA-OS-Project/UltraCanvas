// include/Plugins/Diagrams/UltraCanvasPacketTemplates.h
// Built-in protocol field tables for the packet structure diagram.
//
// These are PLAIN STATIC DATA - field names, offsets and lengths - and nothing
// else. There is no parsing code here, no protocol behaviour, and no
// dependency on Plugins/UltraNet in either direction: a drawing widget must
// not link a socket library, and a networking plugin must not link a UI model.
//
// Each table is pinned to its specification by Tests/PacketLayoutTest.cpp
// (e.g. RTP: version @ bits 0-1, payload type @ 9-15, SSRC @ 64-95). That test
// is the drift protection - it pins the template to the RFC rather than to
// another module's private implementation.
//
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Diagrams/UltraCanvasPacketModel.h"
#include <vector>

namespace UltraCanvas {
namespace PacketTemplates {

// ===== Internet protocols =====
    PacketModel IPv4();        // RFC 791  - 20 byte header
    PacketModel IPv6();        // RFC 8200 - 40 byte fixed header
    PacketModel TCP();         // RFC 793  - 20 byte header
    PacketModel UDP();         // RFC 768  - 8 byte header
    PacketModel ICMP();        // RFC 792  - 8 byte header
    PacketModel ARP();         // RFC 826  - 28 bytes for IPv4 over Ethernet
    PacketModel Ethernet();    // Ethernet II - 14 byte header

// ===== Media transport =====
    PacketModel RTP();         // RFC 3550 s5.1 - 12 byte fixed header

// Compose several single-layer models into one layered diagram, the way
// image 1 shows an IP header and a TCP header in a single figure.
    PacketModel Stack(const std::vector<PacketModel>& layers);

} // namespace PacketTemplates
} // namespace UltraCanvas
