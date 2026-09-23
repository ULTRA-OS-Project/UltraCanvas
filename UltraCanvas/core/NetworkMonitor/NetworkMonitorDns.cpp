// core/NetworkMonitor/NetworkMonitorDns.cpp
// RFC 1035 message parsing and building. Every read is bounds-checked and
// every compression pointer must point backwards, which rules out the loops
// a hostile response could carry; a message that breaks a rule is refused
// whole rather than half-parsed.
//
// Version: 0.4.0
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitorDns.h"
#include "NetworkMonitor/NetworkMonitorAddress.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace UltraCanvas {
namespace NetworkMonitorDns {
namespace {

constexpr std::size_t kHeaderBytes = 12;
constexpr std::size_t kMaxNameBytes = 253;

uint16_t Read16(const unsigned char* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t Read32(const unsigned char* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

void Put16(std::vector<unsigned char>& out, uint16_t value) {
    out.push_back(static_cast<unsigned char>(value >> 8));
    out.push_back(static_cast<unsigned char>(value & 0xFF));
}

void Put32(std::vector<unsigned char>& out, uint32_t value) {
    Put16(out, static_cast<uint16_t>(value >> 16));
    Put16(out, static_cast<uint16_t>(value & 0xFFFF));
}

// Reads a possibly compressed name at `offset`, appending its text to
// `name`, and advances `offset` past it in the *original* stream (a pointer
// ends the name's bytes there). Pointers may only go backwards - to an
// earlier offset than the one being read - so no loop is possible.
bool ReadName(const unsigned char* data, std::size_t size, std::size_t& offset, std::string& name) {
    std::size_t cursor = offset;
    std::size_t endOfBytes = 0;     // where the name's own bytes end, once known
    bool followed = false;
    int hops = 0;
    std::size_t lowestPointer = size;
    while (true) {
        if (cursor >= size) return false;
        const unsigned char length = data[cursor];
        if ((length & 0xC0) == 0xC0) {
            if (cursor + 1 >= size) return false;
            const std::size_t target = ((length & 0x3F) << 8) | data[cursor + 1];
            if (target >= cursor || target >= lowestPointer) return false;   // forwards or repeated: refuse
            lowestPointer = target;
            if (!followed) { endOfBytes = cursor + 2; followed = true; }
            if (++hops > 32) return false;
            cursor = target;
            continue;
        }
        if (length & 0xC0) return false;   // 0x40 / 0x80 are reserved
        ++cursor;
        if (length == 0) {
            if (!followed) endOfBytes = cursor;
            break;
        }
        if (cursor + length > size) return false;
        if (!name.empty()) name += '.';
        for (unsigned char i = 0; i < length; ++i) {
            name += static_cast<char>(std::tolower(static_cast<unsigned char>(data[cursor + i])));
        }
        if (name.size() > kMaxNameBytes) return false;
        cursor += length;
    }
    offset = endOfBytes;
    return true;
}

bool SkipName(const unsigned char* data, std::size_t size, std::size_t& offset) {
    std::string ignored;
    return ReadName(data, size, offset, ignored);
}

void PutName(std::vector<unsigned char>& out, const std::string& name) {
    std::size_t start = 0;
    while (start < name.size()) {
        std::size_t dot = name.find('.', start);
        if (dot == std::string::npos) dot = name.size();
        const std::size_t length = std::min<std::size_t>(dot - start, 63);
        out.push_back(static_cast<unsigned char>(length));
        out.insert(out.end(), name.begin() + static_cast<std::ptrdiff_t>(start),
                   name.begin() + static_cast<std::ptrdiff_t>(start + length));
        start = dot + 1;
    }
    out.push_back(0);
}

bool ParseIPv4Text(const std::string& text, unsigned char out[4]) {
    int part = 0;
    int value = -1;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '.') {
            if (value < 0 || value > 255 || part > 3) return false;
            out[part++] = static_cast<unsigned char>(value);
            value = -1;
        } else if (std::isdigit(static_cast<unsigned char>(text[i]))) {
            value = (value < 0 ? 0 : value * 10) + (text[i] - '0');
            if (value > 255) return false;
        } else {
            return false;
        }
    }
    return part == 4;
}

// Text -> 16 bytes for the response builder. Handles "::" and the
// IPv4-mapped tail; enough for fixtures, not a general parser.
bool ParseIPv6Text(const std::string& text, unsigned char out[16]) {
    std::vector<uint16_t> head, tail;
    bool seenGap = false;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        if (pos == text.size()) break;
        if (text.compare(pos, 2, "::") == 0) {
            if (seenGap) return false;
            seenGap = true;
            pos += 2;
            continue;
        }
        if (text[pos] == ':') { ++pos; continue; }
        std::size_t end = text.find(':', pos);
        if (end == std::string::npos) end = text.size();
        const std::string group = text.substr(pos, end - pos);
        if (group.find('.') != std::string::npos) {
            unsigned char v4[4];
            if (!ParseIPv4Text(group, v4)) return false;
            std::vector<uint16_t>& into = seenGap ? tail : head;
            into.push_back(static_cast<uint16_t>((v4[0] << 8) | v4[1]));
            into.push_back(static_cast<uint16_t>((v4[2] << 8) | v4[3]));
        } else {
            if (group.empty() || group.size() > 4) return false;
            uint16_t value = 0;
            for (char c : group) {
                if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
                value = static_cast<uint16_t>(value * 16 + (std::isdigit(static_cast<unsigned char>(c))
                            ? c - '0' : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10));
            }
            (seenGap ? tail : head).push_back(value);
        }
        pos = end;
    }
    if (head.size() + tail.size() > 8 || (!seenGap && head.size() != 8)) return false;
    std::vector<uint16_t> groups(8, 0);
    for (std::size_t i = 0; i < head.size(); ++i) groups[i] = head[i];
    for (std::size_t i = 0; i < tail.size(); ++i) groups[8 - tail.size() + i] = tail[i];
    for (std::size_t i = 0; i < 8; ++i) {
        out[2 * i] = static_cast<unsigned char>(groups[i] >> 8);
        out[2 * i + 1] = static_cast<unsigned char>(groups[i] & 0xFF);
    }
    return true;
}

} // namespace

std::string NormalizeName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while (!out.empty() && out.back() == '.') out.pop_back();
    return out;
}

bool Parse(const unsigned char* data, std::size_t size, Message& out) {
    out = Message();
    if (!data || size < kHeaderBytes) return false;
    out.id = Read16(data);
    const uint16_t flags = Read16(data + 2);
    out.isResponse = (flags & 0x8000) != 0;
    out.truncated = (flags & 0x0200) != 0;
    out.rcode = static_cast<uint8_t>(flags & 0x000F);
    const uint16_t questions = Read16(data + 4);
    out.answerCount = Read16(data + 6);

    std::size_t offset = kHeaderBytes;
    for (uint16_t q = 0; q < questions; ++q) {
        std::string name;
        if (!ReadName(data, size, offset, name)) return false;
        if (offset + 4 > size) return false;
        if (q == 0) {
            out.questionName = name;
            out.questionType = Read16(data + offset);
        }
        offset += 4;
    }
    for (uint16_t a = 0; a < out.answerCount; ++a) {
        Answer answer;
        if (!ReadName(data, size, offset, answer.name)) return false;
        if (offset + 10 > size) return false;
        answer.type = Read16(data + offset);
        const uint16_t rrClass = Read16(data + offset + 2);
        answer.ttl = Read32(data + offset + 4);
        const uint16_t length = Read16(data + offset + 8);
        offset += 10;
        if (offset + length > size) return false;
        if (rrClass == kClassIn) {
            if (answer.type == kTypeA && length == 4) {
                answer.address = NetworkMonitorAddress::FormatIPv4(data + offset);
                out.answers.push_back(std::move(answer));
            } else if (answer.type == kTypeAaaa && length == 16) {
                answer.address = NetworkMonitorAddress::FormatIPv6(data + offset);
                out.answers.push_back(std::move(answer));
            } else if (answer.type == kTypeCname) {
                std::size_t target = offset;
                if (!ReadName(data, size, target, answer.target)) return false;
                out.answers.push_back(std::move(answer));
            }
        }
        offset += length;
    }
    return true;
}

bool ToObservation(const Message& message, DnsObservation& out) {
    out = DnsObservation();
    if (!message.isResponse || message.rcode != 0 || message.questionName.empty()) return false;
    // The names that stand for the question: the question itself and
    // whatever its CNAME chain reaches, however the records are ordered.
    std::set<std::string> aliases = { message.questionName };
    bool grew = true;
    while (grew) {
        grew = false;
        for (const auto& answer : message.answers) {
            if (answer.type == kTypeCname && aliases.count(answer.name) && !aliases.count(answer.target)) {
                aliases.insert(answer.target);
                grew = true;
            }
        }
    }
    uint32_t ttl = 0;
    bool anyTtl = false;
    for (const auto& answer : message.answers) {
        if (answer.address.empty() || !aliases.count(answer.name)) continue;
        if (std::find(out.addresses.begin(), out.addresses.end(), answer.address) == out.addresses.end()) {
            out.addresses.push_back(answer.address);
        }
        if (!anyTtl || answer.ttl < ttl) { ttl = answer.ttl; anyTtl = true; }
    }
    if (out.addresses.empty()) return false;
    out.queryName = message.questionName;
    out.ttlSeconds = anyTtl ? static_cast<int>(std::min<uint32_t>(ttl, 0x7FFFFFFF)) : 0;
    return true;
}

std::vector<unsigned char> BuildQuery(uint16_t id, const std::string& name, uint16_t type) {
    std::vector<unsigned char> out;
    Put16(out, id);
    Put16(out, 0x0100);   // standard query, recursion desired
    Put16(out, 1); Put16(out, 0); Put16(out, 0); Put16(out, 0);
    PutName(out, NormalizeName(name));
    Put16(out, type);
    Put16(out, kClassIn);
    return out;
}

std::vector<unsigned char> BuildResponse(uint16_t id, const std::string& name, uint16_t type,
                                         const std::vector<Answer>& answers, uint8_t rcode) {
    std::vector<unsigned char> out;
    Put16(out, id);
    Put16(out, static_cast<uint16_t>(0x8180 | (rcode & 0x0F)));   // response, RD, RA
    Put16(out, 1);
    Put16(out, static_cast<uint16_t>(answers.size()));
    Put16(out, 0); Put16(out, 0);
    PutName(out, NormalizeName(name));
    Put16(out, type);
    Put16(out, kClassIn);
    for (const auto& answer : answers) {
        PutName(out, NormalizeName(answer.name));
        Put16(out, answer.type);
        Put16(out, kClassIn);
        Put32(out, answer.ttl);
        if (answer.type == kTypeCname) {
            std::vector<unsigned char> target;
            PutName(target, NormalizeName(answer.target));
            Put16(out, static_cast<uint16_t>(target.size()));
            out.insert(out.end(), target.begin(), target.end());
        } else if (answer.type == kTypeAaaa) {
            unsigned char bytes[16] = {};
            ParseIPv6Text(answer.address, bytes);
            Put16(out, 16);
            out.insert(out.end(), bytes, bytes + 16);
        } else {
            unsigned char bytes[4] = {};
            ParseIPv4Text(answer.address, bytes);
            Put16(out, 4);
            out.insert(out.end(), bytes, bytes + 4);
        }
    }
    return out;
}

} // namespace NetworkMonitorDns
} // namespace UltraCanvas
