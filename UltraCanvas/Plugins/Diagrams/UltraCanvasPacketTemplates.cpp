// Plugins/Diagrams/UltraCanvasPacketTemplates.cpp
// Built-in protocol field tables. Static data only - no parsing, no I/O, no
// networking dependency. Pinned to their specs by Tests/PacketLayoutTest.cpp.
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasPacketTemplates.h"

namespace UltraCanvas {
namespace PacketTemplates {

// Layer tints, chosen to stay legible with dark text.
    static const Color kLinkColor(206, 226, 206, 255);
    static const Color kNetColor (198, 219, 239, 255);
    static const Color kTransColor(222, 213, 235, 255);
    static const Color kMediaColor(253, 224, 200, 255);

// =============================================================================
// RFC 791 - IPv4
// =============================================================================

    PacketModel IPv4() {
        PacketModel m;
        m.title = "IPv4 Header";
        m.bitsPerRow = 32;

        PacketLayer& l = m.AddLayer("ipv4", "IP Header");
        l.hasColor = true;
        l.color = kNetColor;

        l.AddField("Version", 4).SetDescription("IP version, always 4");
        l.AddField("IHL", 4).SetDescription("Internet Header Length, in 32-bit words");
        PacketField& ds = l.AddField("Type of Service", 8);
        ds.AddChild("DSCP", 6);
        ds.AddChild("ECN", 2);
        l.AddField("Total Length", 16).SetDescription("Header + data, in bytes");
        l.AddField("Identification", 16);
        PacketField& flags = l.AddField("Flags", 3);
        flags.AddChild("R", 1);
        flags.AddChild("DF", 1);
        flags.AddChild("MF", 1);
        l.AddField("Fragment Offset", 13);
        l.AddField("Time to Live", 8);
        l.AddField("Protocol", 8).SetDescription("Next-layer protocol number");
        l.AddField("Header Checksum", 16, PacketFieldKind::Checksum);
        l.AddField("Source Address", 32);
        l.AddField("Destination Address", 32);
        return m;
    }

// =============================================================================
// RFC 8200 - IPv6 fixed header
// =============================================================================

    PacketModel IPv6() {
        PacketModel m;
        m.title = "IPv6 Header";
        m.bitsPerRow = 32;

        PacketLayer& l = m.AddLayer("ipv6", "IPv6 Header");
        l.hasColor = true;
        l.color = kNetColor;

        l.AddField("Version", 4);
        l.AddField("Traffic Class", 8);
        l.AddField("Flow Label", 20);
        l.AddField("Payload Length", 16);
        l.AddField("Next Header", 8);
        l.AddField("Hop Limit", 8);
        l.AddField("Source Address", 128);
        l.AddField("Destination Address", 128);
        return m;
    }

// =============================================================================
// RFC 793 - TCP
// =============================================================================

    PacketModel TCP() {
        PacketModel m;
        m.title = "TCP Header";
        m.bitsPerRow = 32;

        PacketLayer& l = m.AddLayer("tcp", "TCP");
        l.hasColor = true;
        l.color = kTransColor;

        l.AddField("Source Port", 16);
        l.AddField("Destination Port", 16);
        l.AddField("Sequence Number", 32);
        l.AddField("Acknowledgement Number", 32);
        l.AddField("Data Offset", 4);
        l.AddField("Reserved", 6, PacketFieldKind::Reserved);
        l.AddField("U", 1, PacketFieldKind::Flag).SetDescription("URG - urgent pointer valid");
        l.AddField("A", 1, PacketFieldKind::Flag).SetDescription("ACK - acknowledgement valid");
        l.AddField("P", 1, PacketFieldKind::Flag).SetDescription("PSH - push");
        l.AddField("R", 1, PacketFieldKind::Flag).SetDescription("RST - reset");
        l.AddField("S", 1, PacketFieldKind::Flag).SetDescription("SYN - synchronise");
        l.AddField("F", 1, PacketFieldKind::Flag).SetDescription("FIN - finish");
        l.AddField("Window", 16);
        l.AddField("Checksum", 16, PacketFieldKind::Checksum);
        l.AddField("Urgent Pointer", 16);
        return m;
    }

// =============================================================================
// RFC 768 - UDP
// =============================================================================

    PacketModel UDP() {
        PacketModel m;
        m.title = "UDP Header";
        m.bitsPerRow = 32;

        PacketLayer& l = m.AddLayer("udp", "UDP");
        l.hasColor = true;
        l.color = kTransColor;

        l.AddField("Source Port", 16);
        l.AddField("Destination Port", 16);
        l.AddField("Length", 16).SetDescription("Header + data, in bytes");
        l.AddField("Checksum", 16, PacketFieldKind::Checksum);
        return m;
    }

// =============================================================================
// RFC 792 - ICMP
// =============================================================================

    PacketModel ICMP() {
        PacketModel m;
        m.title = "ICMP Header";
        m.bitsPerRow = 32;

        PacketLayer& l = m.AddLayer("icmp", "ICMP");
        l.hasColor = true;
        l.color = kNetColor;

        l.AddField("Type", 8);
        l.AddField("Code", 8);
        l.AddField("Checksum", 16, PacketFieldKind::Checksum);
        l.AddField("Rest of Header", 32);
        return m;
    }

// =============================================================================
// RFC 826 - ARP (IPv4 over Ethernet)
// =============================================================================

    PacketModel ARP() {
        PacketModel m;
        m.title = "ARP Packet";
        m.bitsPerRow = 32;

        PacketLayer& l = m.AddLayer("arp", "ARP");
        l.hasColor = true;
        l.color = kLinkColor;

        l.AddField("Hardware Type", 16);
        l.AddField("Protocol Type", 16);
        l.AddField("HLEN", 8);
        l.AddField("PLEN", 8);
        l.AddField("Operation", 16);
        l.AddField("Sender Hardware Address", 48);
        l.AddField("Sender Protocol Address", 32);
        l.AddField("Target Hardware Address", 48);
        l.AddField("Target Protocol Address", 32);
        return m;
    }

// =============================================================================
// Ethernet II
// =============================================================================

    PacketModel Ethernet() {
        PacketModel m;
        m.title = "Ethernet II Header";
        m.bitsPerRow = 32;

        PacketLayer& l = m.AddLayer("eth", "Ethernet");
        l.hasColor = true;
        l.color = kLinkColor;

        l.AddField("Destination MAC", 48);
        l.AddField("Source MAC", 48);
        l.AddField("EtherType", 16);
        return m;
    }

// =============================================================================
// RFC 3550 s5.1 - RTP fixed header
// =============================================================================

    PacketModel RTP() {
        PacketModel m;
        m.title = "RTP Header";
        m.bitsPerRow = 32;

        PacketLayer& l = m.AddLayer("rtp", "RTP");
        l.hasColor = true;
        l.color = kMediaColor;

        l.AddField("Version", 2).SetDescription("Always 2");
        l.AddField("P", 1, PacketFieldKind::Flag).SetDescription("Padding");
        l.AddField("X", 1, PacketFieldKind::Flag).SetDescription("Extension header present");
        l.AddField("CSRC Count", 4);
        l.AddField("M", 1, PacketFieldKind::Flag).SetDescription("Marker");
        l.AddField("Payload Type", 7);
        l.AddField("Sequence Number", 16);
        l.AddField("Timestamp", 32);
        l.AddField("SSRC", 32).SetDescription("Synchronisation source identifier");
        return m;
    }

// =============================================================================
// COMPOSITION
// =============================================================================

    PacketModel Stack(const std::vector<PacketModel>& parts) {
        PacketModel out;
        if (parts.empty()) return out;

        out.bitsPerRow = parts.front().bitsPerRow;
        out.numbering = parts.front().numbering;
        out.endianness = parts.front().endianness;

        for (const auto& part : parts) {
            for (const auto& layer : part.layers) {
                out.layers.push_back(layer);
            }
        }
        return out;
    }

} // namespace PacketTemplates
} // namespace UltraCanvas
