// include/NetworkMonitor/NetworkMonitorConntrack.h
// The Linux connection tracker's netlink messages (nfnetlink_conntrack),
// parsed as bytes: the original-direction tuple, the reply-direction
// counters, and whether the message is a NEW or a DESTROY. Pure functions
// with the protocol's constants spelled out, so the parser compiles and is
// tested from fixture bytes on every platform; only the socket that
// receives the messages is Linux (OS/Linux/UltraCanvasLinuxNetworkMonitorEvents.cpp).
// Internal to the module.
//
// Version: 0.5.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "NetworkMonitor/NetworkMonitor.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace UltraCanvas {
namespace NetworkMonitorConntrack {

// nfnetlink subsystem and message types (linux/netfilter/nfnetlink.h,
// nfnetlink_conntrack.h), and the netlink flags a NEW carries.
constexpr uint8_t  kSubsystemConntrack = 1;
constexpr uint8_t  kMessageNew = 0;       // also UPDATE, without the flags below
constexpr uint8_t  kMessageDestroy = 2;
constexpr uint16_t kFlagCreate = 0x400;   // NLM_F_CREATE
constexpr uint16_t kFlagExcl = 0x200;     // NLM_F_EXCL
// Multicast groups to bind for NEW and DESTROY (1 << (NFNLGRP_x - 1)).
constexpr uint32_t kGroupNew = 1u << 0;
constexpr uint32_t kGroupDestroy = 1u << 2;

enum class Message { Other, New, Update, Destroy };

struct Flow {
    Message              message = Message::Other;
    NetworkTransport     transport = NetworkTransport::Other;
    uint8_t              protocol = 0;       // IPPROTO_*
    NetworkAddressFamily family = NetworkAddressFamily::IPv4;
    // The original direction: the side that sent the first packet.
    std::string          sourceAddress;
    uint16_t             sourcePort = 0;
    std::string          destinationAddress;
    uint16_t             destinationPort = 0;
    // Bytes in each direction, present when the tracker accounts them
    // (net.netfilter.nf_conntrack_acct = 1) and the message carries them.
    std::optional<uint64_t> bytesOriginal;
    std::optional<uint64_t> bytesReply;
};

// Parses one netlink message (header included). False when it is not a
// conntrack message, or is malformed.
bool Parse(const unsigned char* data, std::size_t size, Flow& out);

} // namespace NetworkMonitorConntrack
} // namespace UltraCanvas
