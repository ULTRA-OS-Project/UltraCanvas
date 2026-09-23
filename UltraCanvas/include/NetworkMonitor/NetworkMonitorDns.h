// include/NetworkMonitor/NetworkMonitorDns.h
// The DNS wire format (RFC 1035), as far as a name source needs it: parse a
// message into its question and its A / AAAA / CNAME answers, following
// compression pointers safely, and build a query or a response for a test.
// Pure functions over bytes - no sockets, no I/O - so the local DNS proxy's
// parsing is tested from fixture bytes on every platform. Internal to the
// module; applications use NetworkMonitorNames.h.
//
// Version: 0.4.0
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "NetworkMonitor/NetworkMonitor.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace NetworkMonitorDns {

constexpr uint16_t kTypeA     = 1;
constexpr uint16_t kTypeCname = 5;
constexpr uint16_t kTypeAaaa  = 28;
constexpr uint16_t kClassIn   = 1;
// A UDP message is at most 512 bytes without EDNS; the proxy accepts up to
// this much in case the client advertises a larger buffer.
constexpr std::size_t kMaxMessageBytes = 4096;

struct Answer {
    std::string name;       // the owner name, lower-case, no trailing dot
    uint16_t    type = 0;
    uint32_t    ttl = 0;
    std::string address;    // for A / AAAA: the address in text
    std::string target;     // for CNAME: the canonical name
};

struct Message {
    uint16_t id = 0;
    bool     isResponse = false;
    bool     truncated = false;
    uint8_t  rcode = 0;
    std::string questionName;   // lower-case, no trailing dot; empty when no question
    uint16_t questionType = 0;
    std::vector<Answer> answers; // the answer section's A / AAAA / CNAME records only
    uint16_t answerCount = 0;    // as the header says, every type counted
};

// Parses the header, the first question and the answer section. Returns
// false on a malformed message (a pointer loop, a label past the end);
// records of other types are skipped, not failed on.
bool Parse(const unsigned char* data, std::size_t size, Message& out);

// What a response tells a name table: every A / AAAA answer, whether it is
// owned by the question name or reached through a CNAME chain from it,
// maps to the question name - the one the application asked for. False
// when the response carries no address for the question.
bool ToObservation(const Message& message, DnsObservation& out);

// A query for `name` of `type` with the recursion-desired bit set; a
// response to it carrying `answers`. For tests and for the proxy's own
// probes. Names are encoded without compression.
std::vector<unsigned char> BuildQuery(uint16_t id, const std::string& name, uint16_t type);
std::vector<unsigned char> BuildResponse(uint16_t id, const std::string& name, uint16_t type,
                                         const std::vector<Answer>& answers, uint8_t rcode = 0);

// "WWW.Example.COM." -> "www.example.com". The form every name is stored in.
std::string NormalizeName(const std::string& name);

} // namespace NetworkMonitorDns
} // namespace UltraCanvas
