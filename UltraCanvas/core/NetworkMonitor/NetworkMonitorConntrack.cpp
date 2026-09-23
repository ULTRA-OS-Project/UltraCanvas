// core/NetworkMonitor/NetworkMonitorConntrack.cpp
// nfnetlink_conntrack message parsing over raw bytes. Attribute walking
// with every length checked; a nested attribute is one whose type carries
// the NLA_F_NESTED bit, and the types are masked before comparison.
//
// Version: 0.5.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitorConntrack.h"
#include "NetworkMonitor/NetworkMonitorAddress.h"

#include <cstring>

namespace UltraCanvas {
namespace NetworkMonitorConntrack {
namespace {

constexpr std::size_t kNetlinkHeaderBytes = 16;   // struct nlmsghdr
constexpr std::size_t kGenericHeaderBytes = 4;    // struct nfgenmsg
constexpr std::size_t kAttributeHeaderBytes = 4;  // struct nlattr
constexpr uint16_t kTypeMask = 0x3FFF;

// Attribute types, from linux/netfilter/nfnetlink_conntrack.h.
constexpr uint16_t kCtaTupleOrig = 1;
constexpr uint16_t kCtaCountersOrig = 9;
constexpr uint16_t kCtaCountersReply = 10;
constexpr uint16_t kCtaTupleIp = 1;
constexpr uint16_t kCtaTupleProto = 2;
constexpr uint16_t kCtaIpV4Src = 1;
constexpr uint16_t kCtaIpV4Dst = 2;
constexpr uint16_t kCtaIpV6Src = 3;
constexpr uint16_t kCtaIpV6Dst = 4;
constexpr uint16_t kCtaProtoNum = 1;
constexpr uint16_t kCtaProtoSrcPort = 2;
constexpr uint16_t kCtaProtoDstPort = 3;
constexpr uint16_t kCtaCountersBytes = 2;

constexpr uint8_t kProtoTcp = 6;
constexpr uint8_t kProtoUdp = 17;

struct Attribute {
    uint16_t type = 0;
    const unsigned char* payload = nullptr;
    std::size_t length = 0;
};

// Walks the attributes in [data, data + size), calling `visit` for each.
// Netlink attributes are 4-byte aligned; a length shorter than its header
// or past the end ends the walk with false.
template <typename Visitor>
bool ForEachAttribute(const unsigned char* data, std::size_t size, Visitor visit) {
    std::size_t offset = 0;
    while (offset + kAttributeHeaderBytes <= size) {
        uint16_t length, type;
        std::memcpy(&length, data + offset, 2);
        std::memcpy(&type, data + offset + 2, 2);
        if (length < kAttributeHeaderBytes || offset + length > size) return false;
        Attribute attribute;
        attribute.type = static_cast<uint16_t>(type & kTypeMask);
        attribute.payload = data + offset + kAttributeHeaderBytes;
        attribute.length = length - kAttributeHeaderBytes;
        if (!visit(attribute)) return false;
        offset += (static_cast<std::size_t>(length) + 3) & ~static_cast<std::size_t>(3);
    }
    return true;
}

uint16_t BigEndian16(const unsigned char* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint64_t BigEndian64(const unsigned char* p) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value = (value << 8) | p[i];
    return value;
}

bool ParseTuple(const Attribute& tuple, Flow& flow) {
    return ForEachAttribute(tuple.payload, tuple.length, [&](const Attribute& part) {
        if (part.type == kCtaTupleIp) {
            return ForEachAttribute(part.payload, part.length, [&](const Attribute& ip) {
                if ((ip.type == kCtaIpV4Src || ip.type == kCtaIpV4Dst) && ip.length == 4) {
                    flow.family = NetworkAddressFamily::IPv4;
                    (ip.type == kCtaIpV4Src ? flow.sourceAddress : flow.destinationAddress) =
                        NetworkMonitorAddress::FormatIPv4(ip.payload);
                } else if ((ip.type == kCtaIpV6Src || ip.type == kCtaIpV6Dst) && ip.length == 16) {
                    flow.family = NetworkAddressFamily::IPv6;
                    (ip.type == kCtaIpV6Src ? flow.sourceAddress : flow.destinationAddress) =
                        NetworkMonitorAddress::FormatIPv6(ip.payload);
                }
                return true;
            });
        }
        if (part.type == kCtaTupleProto) {
            return ForEachAttribute(part.payload, part.length, [&](const Attribute& proto) {
                if (proto.type == kCtaProtoNum && proto.length >= 1) {
                    flow.protocol = proto.payload[0];
                    flow.transport = flow.protocol == kProtoTcp ? NetworkTransport::Tcp
                                   : flow.protocol == kProtoUdp ? NetworkTransport::Udp
                                                                : NetworkTransport::Other;
                } else if (proto.type == kCtaProtoSrcPort && proto.length >= 2) {
                    flow.sourcePort = BigEndian16(proto.payload);
                } else if (proto.type == kCtaProtoDstPort && proto.length >= 2) {
                    flow.destinationPort = BigEndian16(proto.payload);
                }
                return true;
            });
        }
        return true;
    });
}

bool ParseCounters(const Attribute& counters, std::optional<uint64_t>& bytes) {
    return ForEachAttribute(counters.payload, counters.length, [&](const Attribute& counter) {
        if (counter.type == kCtaCountersBytes && counter.length == 8) bytes = BigEndian64(counter.payload);
        return true;
    });
}

} // namespace

bool Parse(const unsigned char* data, std::size_t size, Flow& out) {
    out = Flow();
    if (!data || size < kNetlinkHeaderBytes + kGenericHeaderBytes) return false;
    uint32_t length;
    uint16_t type, flags;
    std::memcpy(&length, data, 4);
    std::memcpy(&type, data + 4, 2);
    std::memcpy(&flags, data + 6, 2);
    if (length < kNetlinkHeaderBytes + kGenericHeaderBytes || length > size) return false;
    if ((type >> 8) != kSubsystemConntrack) return false;
    const uint8_t message = static_cast<uint8_t>(type & 0xFF);
    if (message == kMessageDestroy) {
        out.message = Message::Destroy;
    } else if (message == kMessageNew) {
        out.message = (flags & kFlagCreate) && (flags & kFlagExcl) ? Message::New : Message::Update;
    } else {
        return false;
    }
    // nfgenmsg: family, version, res_id. The tuple attributes repeat the
    // family in their own terms, so only the attributes are read.
    const unsigned char* attributes = data + kNetlinkHeaderBytes + kGenericHeaderBytes;
    const std::size_t attributesSize = length - kNetlinkHeaderBytes - kGenericHeaderBytes;
    bool sawTuple = false;
    const bool walked = ForEachAttribute(attributes, attributesSize, [&](const Attribute& attribute) {
        if (attribute.type == kCtaTupleOrig) { sawTuple = true; return ParseTuple(attribute, out); }
        if (attribute.type == kCtaCountersOrig) return ParseCounters(attribute, out.bytesOriginal);
        if (attribute.type == kCtaCountersReply) return ParseCounters(attribute, out.bytesReply);
        return true;
    });
    return walked && sawTuple && !out.sourceAddress.empty() && !out.destinationAddress.empty();
}

} // namespace NetworkMonitorConntrack
} // namespace UltraCanvas
