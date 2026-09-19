// include/NetworkMonitor/NetworkMonitorProcfs.h
// Parser for the Linux /proc/net/{tcp,tcp6,udp,udp6} table format. Pure
// string work with no I/O, kept in the core rather than the Linux backend so
// the unit test drives it from fixture text on every platform and the backend
// is left with only the reads and the /proc walk.
//
// Not public surface: applications use NetworkMonitor.h.
//
// Version: 0.1.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "NetworkMonitor/NetworkMonitor.h"

#include <cstddef>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace NetworkMonitorProcfs {

// Decodes one "ADDRESS:PORT" field as the kernel prints it - the address as
// one (IPv4) or four (IPv6) 32-bit words in host byte order, each as eight
// upper-case hex digits, the port as four. "0100007F:0050" is 127.0.0.1:80;
// "00000000000000000000000001000000:0016" is [::1]:22.
//
// Host byte order is the machine's, which every Linux target this framework
// builds for has little-endian. False on malformed input.
bool DecodeAddress(const std::string& field, NetworkAddressFamily family,
                   std::string& outAddress, uint16_t& outPort);

// The `st` column: the TCP state machine's numbering from
// <linux/tcp_states.h>. A UDP socket reports TCP_CLOSE (7) when unconnected
// and TCP_ESTABLISHED (1) after connect(), which is what Unconnected /
// Established mean for it.
NetworkConnectionState StateFromCode(unsigned code, NetworkTransport transport);

// Parses one whole table (the file's text, header line included) and appends
// a NetworkConnection per data row to `out`, with ownerUid and socketInode
// filled and `process` left empty for the caller to resolve. Returns the
// number of rows appended; malformed rows are skipped, never fatal.
std::size_t ParseTable(const std::string& text, NetworkTransport transport,
                       NetworkAddressFamily family,
                       std::vector<NetworkConnection>& out);

} // namespace NetworkMonitorProcfs
} // namespace UltraCanvas
