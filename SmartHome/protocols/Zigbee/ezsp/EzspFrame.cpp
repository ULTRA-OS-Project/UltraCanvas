// protocols/Zigbee/ezsp/EzspFrame.cpp
// Author: UltraCanvas Framework

#include "EzspFrame.h"

namespace UltraCanvas {
namespace SmartHome {
namespace Ezsp {

namespace {
// Frame control bits that matter here.
constexpr uint8_t kResponseBit = 0x80;
constexpr uint8_t kOverflowBit = 0x01;
constexpr uint8_t kTruncatedBit = 0x02;
}  // namespace

void AppendU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void AppendU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

uint16_t ReadU16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 1 >= data.size()) return 0;
    return static_cast<uint16_t>(data[offset] |
                                 (static_cast<uint16_t>(data[offset + 1]) << 8));
}

uint32_t ReadU32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 3 >= data.size()) return 0;
    uint32_t value = 0;
    for (int i = 3; i >= 0; --i) {
        value = (value << 8) | data[offset + static_cast<size_t>(i)];
    }
    return value;
}

std::vector<uint8_t> EncodeVersionCommand(uint8_t sequence, uint8_t desiredVersion) {
    // Legacy header: sequence, frame control, frame id (one byte), then the
    // desired version as the only parameter.
    return {sequence, 0x00, static_cast<uint8_t>(kVersion), desiredVersion};
}

std::optional<VersionResponse> DecodeVersionResponse(const std::vector<uint8_t>& frame) {
    // A legacy-format response: three header bytes then four parameter bytes.
    if (frame.size() < 7) return std::nullopt;
    VersionResponse response;
    response.ProtocolVersion = frame[3];
    response.StackType = frame[4];
    response.StackVersion = static_cast<uint16_t>(
        frame[5] | (static_cast<uint16_t>(frame[6]) << 8));
    return response;
}

std::vector<uint8_t> EncodeCommand(uint8_t protocolVersion, uint8_t sequence,
                                   uint16_t frameId,
                                   const std::vector<uint8_t>& parameters) {
    std::vector<uint8_t> frame;
    if (protocolVersion >= 8) {
        frame.reserve(parameters.size() + 5);
        frame.push_back(sequence);
        frame.push_back(0x00);      // frame control low
        frame.push_back(0x01);      // frame control high: EZSP v8 frame format
        AppendU16(frame, frameId);
    } else {
        frame.reserve(parameters.size() + 3);
        frame.push_back(sequence);
        frame.push_back(0x00);
        frame.push_back(static_cast<uint8_t>(frameId & 0xFF));
    }
    frame.insert(frame.end(), parameters.begin(), parameters.end());
    return frame;
}

std::optional<Frame> DecodeFrame(uint8_t protocolVersion,
                                 const std::vector<uint8_t>& raw) {
    const size_t headerSize = protocolVersion >= 8 ? 5u : 3u;
    if (raw.size() < headerSize) return std::nullopt;

    Frame frame;
    frame.Sequence = raw[0];
    const uint8_t control = raw[1];
    frame.IsResponse = (control & kResponseBit) != 0;
    frame.Overflow = (control & kOverflowBit) != 0;
    frame.Truncated = (control & kTruncatedBit) != 0;

    if (protocolVersion >= 8) {
        frame.Id = static_cast<uint16_t>(raw[3] | (static_cast<uint16_t>(raw[4]) << 8));
    } else {
        frame.Id = raw[2];
    }
    frame.Parameters.assign(raw.begin() + static_cast<long>(headerSize), raw.end());
    return frame;
}

}  // namespace Ezsp
}  // namespace SmartHome
}  // namespace UltraCanvas

// ============================================================================
// APS
// ============================================================================

namespace UltraCanvas {
namespace SmartHome {
namespace Ezsp {

void AppendApsFrame(std::vector<uint8_t>& out, const ApsFrame& aps) {
    AppendU16(out, aps.ProfileId);
    AppendU16(out, aps.ClusterId);
    out.push_back(aps.SourceEndpoint);
    out.push_back(aps.DestinationEndpoint);
    AppendU16(out, aps.Options);
    AppendU16(out, aps.GroupId);
    out.push_back(aps.Sequence);
}

std::optional<ApsFrame> ReadApsFrame(const std::vector<uint8_t>& d, size_t o) {
    if (o + kApsFrameSize > d.size()) return std::nullopt;
    ApsFrame aps;
    aps.ProfileId = ReadU16(d, o);
    aps.ClusterId = ReadU16(d, o + 2);
    aps.SourceEndpoint = d[o + 4];
    aps.DestinationEndpoint = d[o + 5];
    aps.Options = ReadU16(d, o + 6);
    aps.GroupId = ReadU16(d, o + 8);
    aps.Sequence = d[o + 10];
    return aps;
}

std::vector<uint8_t> EncodeSendUnicastParams(uint16_t destination, const ApsFrame& aps,
                                             uint8_t messageTag,
                                             const std::vector<uint8_t>& contents) {
    std::vector<uint8_t> p;
    p.reserve(contents.size() + kApsFrameSize + 5);
    p.push_back(kOutgoingDirect);
    AppendU16(p, destination);
    AppendApsFrame(p, aps);
    p.push_back(messageTag);
    p.push_back(static_cast<uint8_t>(contents.size()));
    p.insert(p.end(), contents.begin(), contents.end());
    return p;
}

std::vector<uint8_t> EncodeSendMulticastParams(const ApsFrame& aps, uint8_t hops,
                                               uint8_t nonMemberRadius, uint8_t messageTag,
                                               const std::vector<uint8_t>& contents) {
    std::vector<uint8_t> p;
    p.reserve(contents.size() + kApsFrameSize + 4);
    AppendApsFrame(p, aps);
    p.push_back(hops);
    p.push_back(nonMemberRadius);
    p.push_back(messageTag);
    p.push_back(static_cast<uint8_t>(contents.size()));
    p.insert(p.end(), contents.begin(), contents.end());
    return p;
}

std::optional<IncomingMessage> DecodeIncomingMessage(const std::vector<uint8_t>& p) {
    // type(1) aps(11) lqi(1) rssi(1) sender(2) bindingIndex(1) addressIndex(1)
    // length(1) = 19 bytes before the contents.
    constexpr size_t kHeader = 1 + kApsFrameSize + 1 + 1 + 2 + 1 + 1 + 1;
    if (p.size() < kHeader) return std::nullopt;

    IncomingMessage m;
    m.Type = p[0];
    auto aps = ReadApsFrame(p, 1);
    if (!aps) return std::nullopt;
    m.Aps = *aps;
    size_t o = 1 + kApsFrameSize;
    m.LastHopLqi = p[o++];
    m.LastHopRssi = static_cast<int8_t>(p[o++]);
    m.Sender = ReadU16(p, o); o += 2;
    m.BindingIndex = p[o++];
    m.AddressIndex = p[o++];
    const size_t length = p[o++];
    // A length that overruns the frame is corruption, not a long message.
    if (o + length > p.size()) return std::nullopt;
    m.Contents.assign(p.begin() + static_cast<long>(o),
                      p.begin() + static_cast<long>(o + length));
    return m;
}

// ============================================================================
// ZDO
// ============================================================================

namespace Zdo {

namespace {
void AppendU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int shift = 0; shift < 64; shift += 8) out.push_back(static_cast<uint8_t>(v >> shift));
}
uint64_t ReadU64(const std::vector<uint8_t>& d, size_t o) {
    if (o + 8 > d.size()) return 0;
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | d[o + static_cast<size_t>(i)];
    return v;
}
}  // namespace

std::vector<uint8_t> EncodeActiveEpReq(uint8_t tsn, uint16_t nwk) {
    std::vector<uint8_t> f{tsn};
    AppendU16(f, nwk);
    return f;
}

std::vector<uint8_t> EncodeSimpleDescReq(uint8_t tsn, uint16_t nwk, uint8_t endpoint) {
    std::vector<uint8_t> f{tsn};
    AppendU16(f, nwk);
    f.push_back(endpoint);
    return f;
}

std::vector<uint8_t> EncodeNodeDescReq(uint8_t tsn, uint16_t nwk) {
    return EncodeActiveEpReq(tsn, nwk);   // identical layout
}

std::vector<uint8_t> EncodeIeeeAddrReq(uint8_t tsn, uint16_t nwk) {
    std::vector<uint8_t> f{tsn};
    AppendU16(f, nwk);
    f.push_back(0x00);   // request type: single device response
    f.push_back(0x00);   // start index
    return f;
}

std::vector<uint8_t> EncodeBindReq(uint8_t tsn, uint64_t srcIeee, uint8_t srcEp,
                                   uint16_t cluster, uint64_t dstIeee, uint8_t dstEp) {
    std::vector<uint8_t> f{tsn};
    AppendU64(f, srcIeee);
    f.push_back(srcEp);
    AppendU16(f, cluster);
    f.push_back(0x03);   // destination address mode: 64-bit address + endpoint
    AppendU64(f, dstIeee);
    f.push_back(dstEp);
    return f;
}

std::vector<uint8_t> EncodeUnbindReq(uint8_t tsn, uint64_t srcIeee, uint8_t srcEp,
                                     uint16_t cluster, uint64_t dstIeee, uint8_t dstEp) {
    return EncodeBindReq(tsn, srcIeee, srcEp, cluster, dstIeee, dstEp);   // same layout
}

std::vector<uint8_t> EncodeMgmtLeaveReq(uint8_t tsn, uint64_t ieee, bool rejoin,
                                        bool removeChildren) {
    std::vector<uint8_t> f{tsn};
    AppendU64(f, ieee);
    f.push_back(static_cast<uint8_t>((removeChildren ? 0x40 : 0) | (rejoin ? 0x80 : 0)));
    return f;
}

std::optional<ActiveEpRsp> DecodeActiveEpRsp(const std::vector<uint8_t>& f) {
    if (f.size() < 5) return std::nullopt;
    ActiveEpRsp r;
    r.Tsn = f[0]; r.Status = f[1]; r.Nwk = ReadU16(f, 2);
    const size_t count = f[4];
    if (5 + count > f.size()) return std::nullopt;
    r.Endpoints.assign(f.begin() + 5, f.begin() + 5 + static_cast<long>(count));
    return r;
}

std::optional<SimpleDescRsp> DecodeSimpleDescRsp(const std::vector<uint8_t>& f) {
    if (f.size() < 5) return std::nullopt;
    SimpleDescRsp r;
    r.Tsn = f[0]; r.Status = f[1]; r.Nwk = ReadU16(f, 2);
    const size_t length = f[4];
    if (r.Status != kStatusSuccess) {
        // A failure carries no descriptor; return the status and nothing else.
        r.Endpoint = 0; r.ProfileId = 0; r.DeviceId = 0; r.DeviceVersion = 0;
        return r;
    }
    // Descriptor: endpoint(1) profile(2) device(2) version(1) inCount(1) in[] outCount(1) out[]
    size_t o = 5;
    if (length < 7 || o + 7 > f.size()) return std::nullopt;
    r.Endpoint = f[o++];
    r.ProfileId = ReadU16(f, o); o += 2;
    r.DeviceId = ReadU16(f, o); o += 2;
    r.DeviceVersion = static_cast<uint8_t>(f[o++] & 0x0F);
    size_t inCount = f[o++];
    if (o + inCount * 2 + 1 > f.size()) return std::nullopt;
    for (size_t i = 0; i < inCount; ++i) { r.InputClusters.push_back(ReadU16(f, o)); o += 2; }
    size_t outCount = f[o++];
    if (o + outCount * 2 > f.size()) return std::nullopt;
    for (size_t i = 0; i < outCount; ++i) { r.OutputClusters.push_back(ReadU16(f, o)); o += 2; }
    return r;
}

std::optional<NodeDescRsp> DecodeNodeDescRsp(const std::vector<uint8_t>& f) {
    if (f.size() < 4) return std::nullopt;
    NodeDescRsp r;
    r.Tsn = f[0]; r.Status = f[1]; r.Nwk = ReadU16(f, 2);
    r.LogicalType = 0xFF; r.ManufacturerCode = 0;
    if (r.Status != kStatusSuccess) return r;
    // Node descriptor is 13 bytes: byte 0 low three bits are the logical type,
    // manufacturer code is at bytes 3-4.
    if (f.size() < 4 + 13) return std::nullopt;
    r.LogicalType = static_cast<uint8_t>(f[4] & 0x07);
    r.ManufacturerCode = ReadU16(f, 4 + 3);
    return r;
}

std::optional<IeeeAddrRsp> DecodeIeeeAddrRsp(const std::vector<uint8_t>& f) {
    if (f.size() < 2) return std::nullopt;
    IeeeAddrRsp r;
    r.Tsn = f[0]; r.Status = f[1]; r.Ieee = 0; r.Nwk = 0;
    if (r.Status != kStatusSuccess) return r;
    if (f.size() < 12) return std::nullopt;
    r.Ieee = ReadU64(f, 2);
    r.Nwk = ReadU16(f, 10);
    return r;
}

std::optional<StatusRsp> DecodeStatusRsp(const std::vector<uint8_t>& f) {
    if (f.size() < 2) return std::nullopt;
    return StatusRsp{f[0], f[1]};
}

std::optional<DeviceAnnce> DecodeDeviceAnnce(const std::vector<uint8_t>& f) {
    // tsn(1) nwk(2) ieee(8) capability(1)
    if (f.size() < 12) return std::nullopt;
    DeviceAnnce a;
    a.Tsn = f[0];
    a.Nwk = ReadU16(f, 1);
    a.Ieee = ReadU64(f, 3);
    a.Capability = f[11];
    return a;
}

}  // namespace Zdo

// ============================================================================
// ZCL
// ============================================================================

namespace Zcl {

std::optional<Header> DecodeHeader(const std::vector<uint8_t>& frame) {
    if (frame.size() < 3) return std::nullopt;
    Header h;
    h.FrameControl = frame[0];
    h.ClusterSpecific = (h.FrameControl & 0x03) == 0x01;
    h.ManufacturerSpecific = (h.FrameControl & 0x04) != 0;
    h.FromServer = (h.FrameControl & 0x08) != 0;
    size_t o = 1;
    if (h.ManufacturerSpecific) {
        if (frame.size() < 5) return std::nullopt;
        h.ManufacturerCode = ReadU16(frame, o);
        o += 2;
    }
    h.Tsn = frame[o++];
    h.CommandId = frame[o++];
    h.PayloadOffset = o;
    return h;
}

std::optional<size_t> FixedWidth(uint8_t t) {
    // ZCL specification, table 2-10. Ranges first, then the singles.
    if (t >= 0x08 && t <= 0x0F) return t - 0x07;    // 8..64-bit data
    if (t >= 0x18 && t <= 0x1F) return t - 0x17;    // bitmap8..64
    if (t >= 0x20 && t <= 0x27) return t - 0x1F;    // uint8..64
    if (t >= 0x28 && t <= 0x2F) return t - 0x27;    // int8..64
    switch (t) {
        case 0x10: return 1;                        // bool
        case 0x30: return 1;                        // enum8
        case 0x31: return 2;                        // enum16
        case 0x38: return 2;                        // semi-precision float
        case 0x39: return 4;                        // single
        case 0x3A: return 8;                        // double
        case 0xE0: case 0xE1: case 0xE2: return 4;  // time of day, date, UTC
        case 0xE8: case 0xE9: return 2;             // cluster id, attribute id
        case 0xEA: return 4;                        // BACnet OID
        case 0xF0: return 8;                        // EUI64
        case 0xF1: return 16;                       // 128-bit key
        case 0x00: case 0xFF: return 0;             // no data / unknown
        default: return std::nullopt;               // strings, arrays, structs…
    }
}

std::vector<Attribute> DecodeAttributes(const std::vector<uint8_t>& f, size_t o, bool withStatus) {
    std::vector<Attribute> out;
    while (o + 2 <= f.size()) {
        Attribute a;
        a.Id = ReadU16(f, o); o += 2;
        if (withStatus) {
            if (o >= f.size()) break;
            a.Status = f[o++];
            // An unsuccessful record carries no type or value.
            if (a.Status != 0x00) { out.push_back(a); continue; }
        }
        if (o >= f.size()) break;
        a.DataType = f[o++];

        size_t width = 0;
        if (auto fixed = FixedWidth(a.DataType)) {
            width = *fixed;
        } else if (a.DataType == 0x41 || a.DataType == 0x42) {
            // octet / character string: one-byte length prefix; 0xFF is "invalid"
            if (o >= f.size()) break;
            const uint8_t len = f[o];
            width = (len == 0xFF) ? 1 : 1 + static_cast<size_t>(len);
        } else if (a.DataType == 0x43 || a.DataType == 0x44) {
            // long octet / character string: two-byte length prefix
            if (o + 2 > f.size()) break;
            const uint16_t len = ReadU16(f, o);
            width = (len == 0xFFFF) ? 2 : 2 + static_cast<size_t>(len);
        } else {
            // Arrays, structures, sets: not sized here. Stop rather than
            // misread everything that follows.
            break;
        }
        if (o + width > f.size()) break;
        a.Value.assign(f.begin() + static_cast<long>(o), f.begin() + static_cast<long>(o + width));
        o += width;
        out.push_back(a);
    }
    return out;
}

}  // namespace Zcl
}  // namespace Ezsp
}  // namespace SmartHome
}  // namespace UltraCanvas
