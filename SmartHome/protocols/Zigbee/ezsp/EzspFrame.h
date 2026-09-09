// protocols/Zigbee/ezsp/EzspFrame.h
// EZSP frame encoding, the layer that rides inside ASH DATA frames.
//
// Like AshCodec, this is pure byte manipulation and is tested on its own.
//
// EZSP has two frame formats. Up to version 7 the header is three bytes; from
// version 8 it is five, with a 16-bit frame id. A host cannot know which to use
// until it has asked, and the version command itself has to be asked in the
// legacy format for exactly that reason — it is the one command whose encoding
// is fixed.
//
// Author: UltraCanvas Framework

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace UltraCanvas {
namespace SmartHome {
namespace Ezsp {

// The handful of frame ids this backend needs. The full set is in UG100.
enum FrameId : uint16_t {
    kVersion            = 0x0000,
    kGetConfigValue     = 0x0052,
    kSetConfigValue     = 0x0053,
    kNetworkInit        = 0x0017,
    kFormNetwork        = 0x001E,
    kLeaveNetwork       = 0x0020,
    kPermitJoining      = 0x0022,
    kGetNetworkParams   = 0x0028,
    kSendUnicast        = 0x0034,
    kSendMulticast      = 0x0038,
    kIncomingMessage    = 0x0045,
    kStackStatus        = 0x0019,
    kGetEui64           = 0x0026,
    kGetNodeId          = 0x0027,
};

struct Frame {
    uint8_t Sequence = 0;
    uint16_t Id = 0;
    bool IsResponse = false;
    bool Overflow = false;      // the NCP dropped callbacks before this one
    bool Truncated = false;
    std::vector<uint8_t> Parameters;
};

// The version command, in the legacy three-byte header every EZSP version
// understands. `desiredVersion` is the protocol version the host would like.
std::vector<uint8_t> EncodeVersionCommand(uint8_t sequence, uint8_t desiredVersion);

// Reads the answer to the version command: the NCP's protocol version, its
// stack type and stack version.
struct VersionResponse {
    uint8_t ProtocolVersion = 0;
    uint8_t StackType = 0;
    uint16_t StackVersion = 0;
};
std::optional<VersionResponse> DecodeVersionResponse(const std::vector<uint8_t>& frame);

// Encodes a command in the format matching `protocolVersion` (v8 and later get
// the five-byte header, everything earlier the three-byte one).
std::vector<uint8_t> EncodeCommand(uint8_t protocolVersion, uint8_t sequence,
                                   uint16_t frameId,
                                   const std::vector<uint8_t>& parameters);

// Decodes a frame received from the NCP, using the same version rule.
std::optional<Frame> DecodeFrame(uint8_t protocolVersion,
                                 const std::vector<uint8_t>& raw);

// ===== APS =====
//
// EmberApsFrame, the addressing block every EZSP message carries. Eleven bytes,
// laid out per UG100: profile, cluster, source endpoint, destination endpoint,
// options, group id, sequence.
struct ApsFrame {
    uint16_t ProfileId = 0;
    uint16_t ClusterId = 0;
    uint8_t SourceEndpoint = 0;
    uint8_t DestinationEndpoint = 0;
    uint16_t Options = 0;
    uint16_t GroupId = 0;
    uint8_t Sequence = 0;
};
constexpr size_t kApsFrameSize = 11;

// EmberApsOption bits that matter here.
enum ApsOption : uint16_t {
    kApsRetry               = 0x0040,
    kApsEnableRouteDiscovery = 0x0100,
    kApsEnableAddressDiscovery = 0x1000,
};

// EmberOutgoingMessageType.
enum OutgoingType : uint8_t {
    kOutgoingDirect = 0x00,
    kOutgoingViaAddressTable = 0x01,
    kOutgoingViaBinding = 0x02,
    kOutgoingMulticast = 0x03,
    kOutgoingBroadcast = 0x04,
};

void AppendApsFrame(std::vector<uint8_t>& out, const ApsFrame& aps);
std::optional<ApsFrame> ReadApsFrame(const std::vector<uint8_t>& data, size_t offset);

// Parameters for sendUnicast (0x0034): type, destination, APS frame, message
// tag, length, contents.
std::vector<uint8_t> EncodeSendUnicastParams(uint16_t destination, const ApsFrame& aps,
                                             uint8_t messageTag,
                                             const std::vector<uint8_t>& contents);
// Parameters for sendMulticast (0x0038): APS frame (group id inside it), hops,
// non-member radius, message tag, length, contents.
std::vector<uint8_t> EncodeSendMulticastParams(const ApsFrame& aps, uint8_t hops,
                                               uint8_t nonMemberRadius, uint8_t messageTag,
                                               const std::vector<uint8_t>& contents);

// incomingMessageHandler (0x0045) parameters, unpacked.
struct IncomingMessage {
    uint8_t Type = 0;
    ApsFrame Aps;
    uint8_t LastHopLqi = 0;
    int8_t LastHopRssi = 0;
    uint16_t Sender = 0;
    uint8_t BindingIndex = 0;
    uint8_t AddressIndex = 0;
    std::vector<uint8_t> Contents;
};
std::optional<IncomingMessage> DecodeIncomingMessage(const std::vector<uint8_t>& params);

// ===== ZDO =====
//
// Zigbee Device Objects ride in APS profile 0x0000 on endpoint 0. Every frame
// begins with a transaction sequence number, which is how a response is matched
// to its request.
namespace Zdo {

constexpr uint16_t kProfile = 0x0000;
constexpr uint8_t kEndpoint = 0x00;
constexpr uint8_t kStatusSuccess = 0x00;

enum Cluster : uint16_t {
    kIeeeAddrReq    = 0x0001, kIeeeAddrRsp    = 0x8001,
    kNodeDescReq    = 0x0002, kNodeDescRsp    = 0x8002,
    kSimpleDescReq  = 0x0004, kSimpleDescRsp  = 0x8004,
    kActiveEpReq    = 0x0005, kActiveEpRsp    = 0x8005,
    kDeviceAnnce    = 0x0013,
    kBindReq        = 0x0021, kBindRsp        = 0x8021,
    kUnbindReq      = 0x0022, kUnbindRsp      = 0x8022,
    kMgmtLeaveReq   = 0x0034, kMgmtLeaveRsp   = 0x8034,
};

// Requests. Each takes the transaction sequence number first.
std::vector<uint8_t> EncodeActiveEpReq(uint8_t tsn, uint16_t nwk);
std::vector<uint8_t> EncodeSimpleDescReq(uint8_t tsn, uint16_t nwk, uint8_t endpoint);
std::vector<uint8_t> EncodeNodeDescReq(uint8_t tsn, uint16_t nwk);
std::vector<uint8_t> EncodeIeeeAddrReq(uint8_t tsn, uint16_t nwk);
std::vector<uint8_t> EncodeBindReq(uint8_t tsn, uint64_t srcIeee, uint8_t srcEp,
                                   uint16_t cluster, uint64_t dstIeee, uint8_t dstEp);
std::vector<uint8_t> EncodeUnbindReq(uint8_t tsn, uint64_t srcIeee, uint8_t srcEp,
                                     uint16_t cluster, uint64_t dstIeee, uint8_t dstEp);
std::vector<uint8_t> EncodeMgmtLeaveReq(uint8_t tsn, uint64_t ieee, bool rejoin,
                                        bool removeChildren);

// Responses. The status byte follows the TSN in every one; the decoders return
// nothing when the frame is too short to hold what its cluster promises.
struct ActiveEpRsp { uint8_t Tsn; uint8_t Status; uint16_t Nwk; std::vector<uint8_t> Endpoints; };
struct SimpleDescRsp {
    uint8_t Tsn; uint8_t Status; uint16_t Nwk;
    uint8_t Endpoint; uint16_t ProfileId; uint16_t DeviceId; uint8_t DeviceVersion;
    std::vector<uint16_t> InputClusters; std::vector<uint16_t> OutputClusters;
};
struct NodeDescRsp {
    uint8_t Tsn; uint8_t Status; uint16_t Nwk;
    uint8_t LogicalType;        // 0 coordinator, 1 router, 2 end device
    uint16_t ManufacturerCode;
};
struct IeeeAddrRsp { uint8_t Tsn; uint8_t Status; uint64_t Ieee; uint16_t Nwk; };
struct StatusRsp { uint8_t Tsn; uint8_t Status; };   // bind / unbind / leave
struct DeviceAnnce { uint8_t Tsn; uint16_t Nwk; uint64_t Ieee; uint8_t Capability; };

std::optional<ActiveEpRsp> DecodeActiveEpRsp(const std::vector<uint8_t>& f);
std::optional<SimpleDescRsp> DecodeSimpleDescRsp(const std::vector<uint8_t>& f);
std::optional<NodeDescRsp> DecodeNodeDescRsp(const std::vector<uint8_t>& f);
std::optional<IeeeAddrRsp> DecodeIeeeAddrRsp(const std::vector<uint8_t>& f);
std::optional<StatusRsp> DecodeStatusRsp(const std::vector<uint8_t>& f);
std::optional<DeviceAnnce> DecodeDeviceAnnce(const std::vector<uint8_t>& f);

}  // namespace Zdo

// ===== ZCL =====
//
// Just enough of the ZCL frame to route an incoming message: the header, and
// the attribute list carried by a Report Attributes or Read Attributes
// Response, which is where nearly every sensor value arrives.
namespace Zcl {

enum GlobalCommand : uint8_t {
    kReadAttributesResponse = 0x01,
    kReportAttributes = 0x0A,
};

struct Header {
    uint8_t FrameControl = 0;
    bool ClusterSpecific = false;
    bool ManufacturerSpecific = false;
    bool FromServer = false;       // direction bit: server to client
    uint16_t ManufacturerCode = 0;
    uint8_t Tsn = 0;
    uint8_t CommandId = 0;
    size_t PayloadOffset = 0;      // where the command payload begins
};
std::optional<Header> DecodeHeader(const std::vector<uint8_t>& frame);

struct Attribute {
    uint16_t Id = 0;
    uint8_t DataType = 0;
    uint8_t Status = 0;            // Read Attributes Response only
    std::vector<uint8_t> Value;    // raw, in the data type's own encoding
};

// Width of a fixed-size ZCL data type, or nothing for variable-length and
// unknown types. Used to walk an attribute list without a per-type parser.
std::optional<size_t> FixedWidth(uint8_t dataType);

// Walks the attribute records following the ZCL header. `withStatus` is true
// for a Read Attributes Response, whose records carry a status byte before the
// type. Stops at the first record it cannot size, returning what it has.
std::vector<Attribute> DecodeAttributes(const std::vector<uint8_t>& frame,
                                        size_t offset, bool withStatus);

}  // namespace Zcl

// Little-endian helpers; EZSP parameters are little-endian throughout.
void AppendU16(std::vector<uint8_t>& out, uint16_t value);
void AppendU32(std::vector<uint8_t>& out, uint32_t value);
uint16_t ReadU16(const std::vector<uint8_t>& data, size_t offset);
uint32_t ReadU32(const std::vector<uint8_t>& data, size_t offset);

}  // namespace Ezsp
}  // namespace SmartHome
}  // namespace UltraCanvas
