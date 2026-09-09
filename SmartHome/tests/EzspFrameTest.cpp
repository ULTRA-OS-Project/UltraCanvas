// SmartHome/tests/EzspFrameTest.cpp
// Exercises the EZSP, APS, ZDO and ZCL codecs without a radio.
//
// Every expected byte string here was written from the specification (UG100
// for EZSP and the APS frame, the Zigbee ZDP tables for ZDO, ZCL 2.6 for the
// frame header and attribute records), not from what the code produces, so a
// codec that agrees with its own decoder but not with the wire cannot pass.
//
// Author: UltraCanvas Framework

#include "EzspFrame.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas::SmartHome::Ezsp;

static int failures = 0;

static void Check(bool condition, const char* what) {
    if (condition) { std::printf("ok  %s\n", what); }
    else           { std::printf("FAIL %s\n", what); ++failures; }
}

static std::string Hex(const std::vector<uint8_t>& v) {
    std::string s;
    char buf[4];
    for (uint8_t b : v) { std::snprintf(buf, sizeof buf, "%02X ", b); s += buf; }
    return s;
}

static void CheckBytes(const std::vector<uint8_t>& got, const std::vector<uint8_t>& want,
                       const char* what) {
    Check(got == want, what);
    if (got != want) {
        std::printf("     got  %s\n     want %s\n", Hex(got).c_str(), Hex(want).c_str());
    }
}

int main() {
    // ---- EZSP headers ----
    {
        // Legacy header: sequence, frame control 0x00, frame id.
        CheckBytes(EncodeVersionCommand(0x05, 8), {0x05, 0x00, 0x00, 0x08},
                   "version command is legacy three-byte header + desired version");
        // v8 header: sequence, control low 0x00, control high 0x01, id16 LE.
        CheckBytes(EncodeCommand(8, 0x07, kSendUnicast, {0xAA}),
                   {0x07, 0x00, 0x01, 0x34, 0x00, 0xAA},
                   "v8 command has the five-byte header with a 16-bit id");
        CheckBytes(EncodeCommand(7, 0x07, kPermitJoining, {0xFF}),
                   {0x07, 0x00, 0x22, 0xFF},
                   "v7 command keeps the three-byte header");

        // Version response, legacy layout: seq, ctrl(0x80 = response), id,
        // protocolVersion, stackType, stackVersion(2).
        auto v = DecodeVersionResponse({0x05, 0x80, 0x00, 0x08, 0x02, 0x34, 0x12});
        Check(v && v->ProtocolVersion == 8 && v->StackType == 2 && v->StackVersion == 0x1234,
              "version response decodes protocol, stack type and stack version");

        // A v8 callback with the overflow bit set in control low.
        auto f = DecodeFrame(8, {0x00, 0x81, 0x01, 0x45, 0x00, 0x01});
        Check(f && f->IsResponse && f->Overflow && f->Id == kIncomingMessage &&
              f->Parameters == std::vector<uint8_t>{0x01},
              "v8 frame decode reads response bit, overflow bit, id and parameters");
    }

    // ---- APS frame ----
    {
        ApsFrame aps;
        aps.ProfileId = 0x0104; aps.ClusterId = 0x0006;
        aps.SourceEndpoint = 1; aps.DestinationEndpoint = 3;
        aps.Options = 0x0140; aps.GroupId = 0; aps.Sequence = 0x2A;
        std::vector<uint8_t> out;
        AppendApsFrame(out, aps);
        CheckBytes(out, {0x04, 0x01, 0x06, 0x00, 0x01, 0x03, 0x40, 0x01, 0x00, 0x00, 0x2A},
                   "APS frame is 11 bytes: profile, cluster, src ep, dst ep, options, group, seq");
        auto back = ReadApsFrame(out, 0);
        Check(back && back->ProfileId == 0x0104 && back->ClusterId == 6 &&
              back->SourceEndpoint == 1 && back->DestinationEndpoint == 3 &&
              back->Options == 0x0140 && back->Sequence == 0x2A,
              "APS frame round-trips");
        Check(!ReadApsFrame({0x00, 0x01}, 0), "short APS frame is rejected");

        // sendUnicast: type, destination, APS, tag, length, contents.
        auto params = EncodeSendUnicastParams(0x1234, aps, 0x2A, {0x10, 0x01, 0x01});
        std::vector<uint8_t> want{0x00, 0x34, 0x12};
        want.insert(want.end(), out.begin(), out.end());
        want.insert(want.end(), {0x2A, 0x03, 0x10, 0x01, 0x01});
        CheckBytes(params, want, "sendUnicast parameters: direct, dest, APS, tag, len, bytes");

        // sendBroadcast: destination, APS, radius, tag, length, contents.
        auto bparams = EncodeSendBroadcastParams(kBroadcastRouters, aps, 0, 0x2C, {0x01});
        std::vector<uint8_t> bwant{0xFC, 0xFF};
        bwant.insert(bwant.end(), out.begin(), out.end());
        bwant.insert(bwant.end(), {0x00, 0x2C, 0x01, 0x01});
        CheckBytes(bparams, bwant, "sendBroadcast parameters: dest, APS, radius, tag, len, bytes");

        // sendMulticast: APS (group inside), hops, radius, tag, length, contents.
        ApsFrame g = aps; g.GroupId = 0x0007;
        auto mparams = EncodeSendMulticastParams(g, 0, 7, 0x2B, {0x01});
        std::vector<uint8_t> gaps; AppendApsFrame(gaps, g);
        std::vector<uint8_t> mwant = gaps;
        mwant.insert(mwant.end(), {0x00, 0x07, 0x2B, 0x01, 0x01});
        CheckBytes(mparams, mwant, "sendMulticast parameters: APS, hops, radius, tag, len, bytes");
    }

    // ---- incomingMessageHandler ----
    {
        // type(1) aps(11) lqi(1) rssi(1) sender(2) bindingIdx(1) addrIdx(1) len(1) contents
        std::vector<uint8_t> params{
            0x00,                                                       // unicast
            0x04, 0x01, 0x02, 0x04, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x11, // APS, cluster 0x0402
            0xFF, 0xC4,                                                 // lqi 255, rssi -60
            0x56, 0x34,                                                 // sender 0x3456
            0xFF, 0xFF,                                                 // no binding / address index
            0x03, 0xAA, 0xBB, 0xCC};
        auto msg = DecodeIncomingMessage(params);
        Check(msg && msg->Type == 0 && msg->Aps.ClusterId == 0x0402 &&
              msg->LastHopLqi == 255 && msg->LastHopRssi == -60 && msg->Sender == 0x3456 &&
              msg->Contents == std::vector<uint8_t>{0xAA, 0xBB, 0xCC},
              "incomingMessageHandler unpacks type, APS, LQI, signed RSSI, sender, contents");

        // Length byte promises more than is there: reject, never read past the end.
        std::vector<uint8_t> truncated(params.begin(), params.end() - 1);
        Check(!DecodeIncomingMessage(truncated), "incoming message with short contents is rejected");
        Check(!DecodeIncomingMessage({0x00, 0x01}), "incoming message shorter than its header is rejected");
    }

    // ---- ZDO requests ----
    {
        CheckBytes(Zdo::EncodeActiveEpReq(0x10, 0x1234), {0x10, 0x34, 0x12},
                   "Active_EP_req: tsn, nwk");
        CheckBytes(Zdo::EncodeSimpleDescReq(0x11, 0x1234, 0x08), {0x11, 0x34, 0x12, 0x08},
                   "Simple_Desc_req: tsn, nwk, endpoint");
        CheckBytes(Zdo::EncodeNodeDescReq(0x12, 0x1234), {0x12, 0x34, 0x12},
                   "Node_Desc_req: tsn, nwk");
        // IEEE_addr_req: nwk, request type 0 (single), start index 0.
        CheckBytes(Zdo::EncodeIeeeAddrReq(0x13, 0x1234), {0x13, 0x34, 0x12, 0x00, 0x00},
                   "IEEE_addr_req: tsn, nwk, single-device request, start index 0");
        // Bind_req: src ieee(8 LE), src ep, cluster(2), dst addr mode 0x03,
        // dst ieee(8 LE), dst ep.
        CheckBytes(Zdo::EncodeBindReq(0x14, 0x0011223344556677ULL, 1, 0x0006,
                                      0x8899AABBCCDDEEFFULL, 2),
                   {0x14,
                    0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0x01,
                    0x06, 0x00, 0x03,
                    0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88, 0x02},
                   "Bind_req: tsn, src ieee LE, src ep, cluster, mode 3, dst ieee LE, dst ep");
        CheckBytes(Zdo::EncodeUnbindReq(0x15, 0x0011223344556677ULL, 1, 0x0006,
                                        0x8899AABBCCDDEEFFULL, 2),
                   {0x15,
                    0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0x01,
                    0x06, 0x00, 0x03,
                    0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88, 0x02},
                   "Unbind_req has the Bind_req layout");
        // Mgmt_Leave_req: ieee(8 LE), flags (bit6 remove children, bit7 rejoin).
        CheckBytes(Zdo::EncodeMgmtLeaveReq(0x16, 0x0011223344556677ULL, false, false),
                   {0x16, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0x00},
                   "Mgmt_Leave_req: tsn, ieee LE, flags clear");
        CheckBytes(Zdo::EncodeMgmtPermitJoiningReq(0x18, 0xFE), {0x18, 0xFE, 0x01},
                   "Mgmt_Permit_Joining_req: tsn, duration, TC significance 1");
        CheckBytes(Zdo::EncodeMgmtLeaveReq(0x17, 0x0011223344556677ULL, true, true),
                   {0x17, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0xC0},
                   "Mgmt_Leave_req: rejoin is bit 7, remove children bit 6");
    }

    // ---- ZDO responses ----
    {
        auto ep = Zdo::DecodeActiveEpRsp({0x10, 0x00, 0x34, 0x12, 0x02, 0x01, 0xF2});
        Check(ep && ep->Tsn == 0x10 && ep->Status == 0 && ep->Nwk == 0x1234 &&
              ep->Endpoints == std::vector<uint8_t>{0x01, 0xF2},
              "Active_EP_rsp: tsn, status, nwk, count, endpoints");
        Check(!Zdo::DecodeActiveEpRsp({0x10, 0x00, 0x34, 0x12, 0x05, 0x01}),
              "Active_EP_rsp with a count past the end is rejected");

        // Simple_Desc_rsp: tsn, status, nwk, length, then the descriptor:
        // endpoint, profile, device id, version(4 bits), in count, in list,
        // out count, out list.
        auto sd = Zdo::DecodeSimpleDescRsp({0x11, 0x00, 0x34, 0x12, 0x0E,
                                            0x01, 0x04, 0x01, 0x00, 0x01, 0x01,
                                            0x03, 0x00, 0x00, 0x03, 0x00, 0x06, 0x00,
                                            0x01, 0x19, 0x00});
        Check(sd && sd->Endpoint == 1 && sd->ProfileId == 0x0104 && sd->DeviceId == 0x0100 &&
              sd->InputClusters == std::vector<uint16_t>{0x0000, 0x0003, 0x0006} &&
              sd->OutputClusters == std::vector<uint16_t>{0x0019},
              "Simple_Desc_rsp: endpoint, profile, device id, input and output cluster lists");
        auto sdFail = Zdo::DecodeSimpleDescRsp({0x11, 0x82, 0x34, 0x12, 0x00});
        Check(sdFail && sdFail->Status == 0x82 && sdFail->InputClusters.empty(),
              "Simple_Desc_rsp with a failure status carries no descriptor");

        // Node_Desc_rsp: tsn, status, nwk, then the 13-byte descriptor whose
        // first byte's low 3 bits are the logical type and whose manufacturer
        // code is at offset 3.
        auto nd = Zdo::DecodeNodeDescRsp({0x12, 0x00, 0x34, 0x12,
                                          0x02, 0x40, 0x80, 0x7C, 0x11, 0x52, 0x52, 0x00,
                                          0x00, 0x2C, 0x52, 0x00, 0x00});
        Check(nd && nd->LogicalType == 2 && nd->ManufacturerCode == 0x117C,
              "Node_Desc_rsp: logical type from low bits, manufacturer code at offset 3");

        // IEEE_addr_rsp: tsn, status, ieee LE, nwk.
        auto ia = Zdo::DecodeIeeeAddrRsp({0x13, 0x00,
                                          0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
                                          0x34, 0x12});
        Check(ia && ia->Ieee == 0x0011223344556677ULL && ia->Nwk == 0x1234,
              "IEEE_addr_rsp: ieee little-endian, nwk");

        auto st = Zdo::DecodeStatusRsp({0x14, 0x00});
        Check(st && st->Tsn == 0x14 && st->Status == 0, "status response: tsn, status");
        Check(!Zdo::DecodeStatusRsp({0x14}), "status response needs both bytes");

        // Device_annce: tsn, nwk, ieee LE, capability.
        auto da = Zdo::DecodeDeviceAnnce({0x81, 0x34, 0x12,
                                          0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
                                          0x8E});
        Check(da && da->Nwk == 0x1234 && da->Ieee == 0x0011223344556677ULL &&
              da->Capability == 0x8E,
              "Device_annce: nwk, ieee little-endian, capability");
    }

    // ---- ZCL header ----
    {
        // Global, not manufacturer specific, server to client, tsn 0x21, cmd 0x0A.
        auto h = Zcl::DecodeHeader({0x08, 0x21, 0x0A, 0xAA});
        Check(h && !h->ClusterSpecific && !h->ManufacturerSpecific && h->FromServer &&
              h->Tsn == 0x21 && h->CommandId == 0x0A && h->PayloadOffset == 3,
              "ZCL header: direction bit, tsn, command, three-byte offset");
        // Manufacturer specific adds a two-byte code before the tsn.
        auto m = Zcl::DecodeHeader({0x05, 0x4C, 0x10, 0x21, 0x00});
        Check(m && m->ClusterSpecific && m->ManufacturerSpecific && m->ManufacturerCode == 0x104C &&
              m->Tsn == 0x21 && m->CommandId == 0x00 && m->PayloadOffset == 5,
              "ZCL header: manufacturer code inserted, five-byte offset");
        Check(!Zcl::DecodeHeader({0x04, 0x4C}), "ZCL manufacturer-specific header too short is rejected");
        Check(!Zcl::DecodeHeader({0x00, 0x21}), "ZCL header too short is rejected");
    }

    // ---- ZCL attribute records ----
    {
        Check(Zcl::FixedWidth(0x10) == 1 && Zcl::FixedWidth(0x21) == 2 &&
              Zcl::FixedWidth(0x29) == 2 && Zcl::FixedWidth(0x2B) == 4 &&
              Zcl::FixedWidth(0x39) == 4 && Zcl::FixedWidth(0xF0) == 8 &&
              Zcl::FixedWidth(0x1B) == 4 && Zcl::FixedWidth(0x0F) == 8,
              "fixed widths: bool 1, uint16 2, int16 2, int32 4, float 4, EUI64 8, bitmap32 4, data64 8");
        Check(!Zcl::FixedWidth(0x42) && !Zcl::FixedWidth(0x48) && !Zcl::FixedWidth(0x4C),
              "strings, arrays and structures have no fixed width");

        // Report Attributes from a temperature sensor: attr 0x0000 int16 = 2150
        // (21.50 °C), followed by attr 0x0010 uint8 = 7.
        std::vector<uint8_t> report{0x08, 0x21, 0x0A,
                                    0x00, 0x00, 0x29, 0x66, 0x08,
                                    0x10, 0x00, 0x20, 0x07};
        auto h = Zcl::DecodeHeader(report);
        auto attrs = Zcl::DecodeAttributes(report, h->PayloadOffset, false);
        Check(attrs.size() == 2 &&
              attrs[0].Id == 0x0000 && attrs[0].DataType == 0x29 &&
              attrs[0].Value == std::vector<uint8_t>{0x66, 0x08} &&
              attrs[1].Id == 0x0010 && attrs[1].DataType == 0x20 &&
              attrs[1].Value == std::vector<uint8_t>{0x07},
              "Report Attributes: two fixed-width records walk correctly");

        // Read Attributes Response from the Basic cluster: model identifier as a
        // character string, then an unsupported attribute, then a bool.
        std::vector<uint8_t> rsp{0x18, 0x22, 0x01,
                                 0x05, 0x00, 0x00, 0x42, 0x04, 'L', 'a', 'm', 'p',
                                 0x99, 0x00, 0x86,
                                 0x07, 0x00, 0x00, 0x30, 0x03};
        auto rh = Zcl::DecodeHeader(rsp);
        auto rattrs = Zcl::DecodeAttributes(rsp, rh->PayloadOffset, true);
        Check(rattrs.size() == 3 &&
              rattrs[0].Id == 0x0005 && rattrs[0].Status == 0 && rattrs[0].DataType == 0x42 &&
              rattrs[0].Value == std::vector<uint8_t>{0x04, 'L', 'a', 'm', 'p'} &&
              rattrs[1].Id == 0x0099 && rattrs[1].Status == 0x86 && rattrs[1].Value.empty() &&
              rattrs[2].Id == 0x0007 && rattrs[2].DataType == 0x30 &&
              rattrs[2].Value == std::vector<uint8_t>{0x03},
              "Read Attributes Response: string with length prefix, failed record, enum8");

        // A long string uses a two-byte length; an "invalid" string is just its marker.
        std::vector<uint8_t> longs{0x00, 0x00, 0x44, 0x02, 0x00, 'h', 'i',
                                   0x01, 0x00, 0x42, 0xFF,
                                   0x02, 0x00, 0x20, 0x09};
        auto lattrs = Zcl::DecodeAttributes(longs, 0, false);
        Check(lattrs.size() == 3 &&
              lattrs[0].Value == std::vector<uint8_t>{0x02, 0x00, 'h', 'i'} &&
              lattrs[1].Value == std::vector<uint8_t>{0xFF} &&
              lattrs[2].Value == std::vector<uint8_t>{0x09},
              "long string, invalid string marker, and the record after them");

        // A record cut short stops the walk without inventing a value; what came
        // before it is kept. An unsized type (array) stops it too.
        std::vector<uint8_t> cut{0x00, 0x00, 0x21, 0x34, 0x12, 0x01, 0x00, 0x23, 0xAA};
        auto cattrs = Zcl::DecodeAttributes(cut, 0, false);
        Check(cattrs.size() == 1 && cattrs[0].Value == std::vector<uint8_t>{0x34, 0x12},
              "truncated record ends the walk, keeping the complete ones");
        std::vector<uint8_t> arr{0x00, 0x00, 0x48, 0x20, 0x01, 0x00, 0x05, 0x00, 0x20, 0x01};
        Check(Zcl::DecodeAttributes(arr, 0, false).empty(),
              "an array record stops the walk rather than misreading what follows");
    }

    // ---- network configuration and formation ----
    {
        CheckBytes(EncodeSetConfigValueParams(kConfigStackProfile, 2), {0x0C, 0x02, 0x00},
                   "setConfigurationValue: id, value16");
        CheckBytes(EncodeSetPolicyParams(kPolicyTcKeyRequest, kDecisionAllowTcKeyRequestsSendCurrent),
                   {0x05, 0x51}, "setPolicy: policy, decision");
        CheckBytes(EncodeAddEndpointParams(1, 0x0104, 0x0005, 0, {0x0000}, {0x0006, 0x0008}),
                   {0x01, 0x04, 0x01, 0x05, 0x00, 0x00, 0x01, 0x02,
                    0x00, 0x00, 0x06, 0x00, 0x08, 0x00},
                   "addEndpoint: counts before the two bare cluster lists");
        Check(EncodeNetworkInitParams(4).empty() &&
              EncodeNetworkInitParams(8) == std::vector<uint8_t>{0x00, 0x00},
              "networkInit: no parameters before v6, a 16-bit bitmask after");
        std::vector<uint8_t> tk = EncodeAddTransientLinkKeyParams(0xFFFFFFFFFFFFFFFFULL,
                                                                  kZigbeeAllianceKey);
        Check(tk.size() == 24 && tk[0] == 0xFF && tk[7] == 0xFF &&
              std::string(tk.begin() + 8, tk.end()) == "ZigBeeAlliance09",
              "addTransientLinkKey: partner EUI64 then the 16-byte key");

        NetworkParameters n;
        n.ExtendedPanId = 0x0011223344556677ULL; n.PanId = 0x1A62; n.RadioTxPower = 8;
        n.RadioChannel = 15; n.JoinMethod = 0; n.NwkManagerId = 0; n.NwkUpdateId = 0;
        n.Channels = 1u << 15;
        std::vector<uint8_t> np;
        AppendNetworkParameters(np, n);
        CheckBytes(np, {0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
                        0x62, 0x1A, 0x08, 0x0F, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x80, 0x00, 0x00},
                   "EmberNetworkParameters is 20 bytes in UG100 order");
        auto nb = ReadNetworkParameters(np, 0);
        Check(nb && nb->ExtendedPanId == n.ExtendedPanId && nb->PanId == 0x1A62 &&
              nb->RadioChannel == 15 && nb->Channels == (1u << 15),
              "EmberNetworkParameters round-trips");

        std::vector<uint8_t> gnp{0x00, 0x01};
        gnp.insert(gnp.end(), np.begin(), np.end());
        auto r = DecodeNetworkParametersResponse(gnp);
        Check(r && r->Status == 0 && r->NodeType == 1 && r->Parameters.PanId == 0x1A62,
              "getNetworkParameters response: status, node type, parameters");
        auto rf = DecodeNetworkParametersResponse({0x93, 0x00});
        Check(rf && rf->Status == 0x93, "getNetworkParameters when not joined carries just the status");

        InitialSecurityState st;
        st.Bitmask = 0x0B04;
        st.PreconfiguredKey = kZigbeeAllianceKey;
        st.NetworkKey.fill(0xAB);
        st.NetworkKeySequenceNumber = 0;
        st.TrustCenterEui64 = 0;
        auto sp = EncodeInitialSecurityStateParams(st);
        Check(sp.size() == 43 && sp[0] == 0x04 && sp[1] == 0x0B && sp[2] == 'Z' && sp[17] == '9' &&
              sp[18] == 0xAB && sp[33] == 0xAB && sp[34] == 0x00 && sp[42] == 0x00,
              "EmberInitialSecurityState is 43 bytes: bitmask, two keys, sequence, TC EUI64");

        auto tcj = DecodeTrustCenterJoin({0x34, 0x12,
                                          0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
                                          0x02, 0x03, 0x00, 0x00});
        Check(tcj && tcj->NodeId == 0x1234 && tcj->Eui64 == 0x0011223344556677ULL &&
              tcj->Status == kDeviceUpdateLeft && tcj->Decision == 3 && tcj->ParentNodeId == 0,
              "trustCenterJoinHandler: node id, EUI64, device update, decision, parent");
        Check(!DecodeTrustCenterJoin({0x34, 0x12, 0x77}), "short trustCenterJoinHandler is rejected");
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
