// include/NetworkMonitor/NetworkMonitorAddress.h
// Address bytes to text, shared by every backend so that a peer prints the
// same way whether it came from /proc/net, netlink, IP Helper or libproc -
// and so the tests' expectations hold on every platform. Pure; no platform
// headers, so it is also what the parser tests exercise.
//
// Not public surface: applications use NetworkMonitor.h.
//
// Version: 0.2.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace UltraCanvas {
namespace NetworkMonitorAddress {

// Four bytes in network (memory) order: 7F 00 00 01 is "127.0.0.1".
std::string FormatIPv4(const unsigned char* bytes);

// Sixteen bytes in network order, RFC 5952 text: lower-case hex, the longest
// run of zero groups (two or more) collapsed once, leftmost on a tie; an
// IPv4-mapped address in mixed notation ("::ffff:127.0.0.1"), as inet_ntop
// prints it, so IsLoopback() can recognise it.
std::string FormatIPv6(const unsigned char* bytes);

} // namespace NetworkMonitorAddress
} // namespace UltraCanvas
